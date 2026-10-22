###------------------------------------------------------------------------------------------------
# script:  bamf.curl.inc
# purpose: BAMF helper file for curl: HTTP requests with retries, and waiting for a service to answer
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( curl )
#          start_logic
#          curl_request "https://example.com/release.tgz" /tmp/release.tgz || exit_logic $?
#          curl_wait_for_http_status "http://localhost:8080/healthz" 200 12 10 || exit_logic $?
#
# notes:
#          - TLS certificates are always checked, unless BAMF_CURL_INSECURE=yes (not recommended)
#          - user names, passwords and request bodies are passed to curl in 0600 files, never on
#            the command line
#
# function list:
#          - curl_interpret_return
#          - curl_request
#          - curl_wait_for_http_status
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Dependencies
REQUIRED_EXECUTABLES+=('curl')

###------------------------------------------------------------------------------------------------
## Variables
declare -g CURL_DEFAULT_CONNECT_TIMEOUT="${CURL_DEFAULT_CONNECT_TIMEOUT:-30}"
declare -g CURL_DEFAULT_MAX_TIMEOUT="${CURL_DEFAULT_MAX_TIMEOUT:-7200}"     # 2 hours
declare -g CURL_DEFAULT_SPEED_TIMEOUT="${CURL_DEFAULT_SPEED_TIMEOUT:-30}"   # seconds below 1 byte per second before giving up
declare -g CURL_DEFAULT_LIMIT_RATE="${CURL_DEFAULT_LIMIT_RATE:-0}"          # 0 is unlimited
declare -g CURL_DEFAULT_RETRY_COUNT="${CURL_DEFAULT_RETRY_COUNT:-3}"
declare -g CURL_DEFAULT_RETRY_TIMER_MAX_SEC="${CURL_DEFAULT_RETRY_TIMER_MAX_SEC:-30}"
declare -g BAMF_CURL_INSECURE="${BAMF_CURL_INSECURE:-no}"
# HTTP status code of the last curl_request (000 when no response arrived)
declare -g CURL_LAST_HTTP_CODE=""

###------------------------------------------------------------------------------------------------
## FUNCTION: curl_interpret_return()
## - Logs what a curl exit code means, and whether trying again could help
## - Network problems (cannot resolve, cannot connect, timeouts, interrupted transfers) can be retried;
##   errors in the request itself (bad URL, unknown option, local file problems) cannot
## - Arguments
##   - $1: curl exit code
##   - $2: Variable name to pass yes or no to: is a retry worthwhile (optional)
function curl_interpret_return() {
    local __CI_CODE="${1:-}"
    local __CI_NAME=""
    local __CI_RETRY=no
    case "${__CI_CODE}" in
        0)  __CI_NAME="OK";;
        5)  __CI_NAME="COULDNT_RESOLVE_PROXY";      __CI_RETRY=yes;;
        6)  __CI_NAME="COULDNT_RESOLVE_HOST";       __CI_RETRY=yes;;
        7)  __CI_NAME="COULDNT_CONNECT";            __CI_RETRY=yes;;
        16) __CI_NAME="HTTP2";                      __CI_RETRY=yes;;
        18) __CI_NAME="PARTIAL_FILE";               __CI_RETRY=yes;;
        22) __CI_NAME="HTTP_RETURNED_ERROR";        __CI_RETRY=yes;;
        28) __CI_NAME="OPERATION_TIMEDOUT";         __CI_RETRY=yes;;
        35) __CI_NAME="SSL_CONNECT_ERROR";          __CI_RETRY=yes;;
        47) __CI_NAME="TOO_MANY_REDIRECTS";;
        52) __CI_NAME="GOT_NOTHING";                __CI_RETRY=yes;;
        55) __CI_NAME="SEND_ERROR";                 __CI_RETRY=yes;;
        56) __CI_NAME="RECV_ERROR";                 __CI_RETRY=yes;;
        92) __CI_NAME="HTTP2_STREAM";               __CI_RETRY=yes;;
        1)  __CI_NAME="UNSUPPORTED_PROTOCOL";;
        3)  __CI_NAME="URL_MALFORMAT";;
        23) __CI_NAME="WRITE_ERROR (could not write the output file)";;
        26) __CI_NAME="READ_ERROR (could not read a local file)";;
        33) __CI_NAME="RANGE_ERROR (the server cannot resume downloads)";;
        37) __CI_NAME="FILE_COULDNT_READ_FILE";;
        51|60) __CI_NAME="PEER_FAILED_VERIFICATION (the server's TLS certificate was not accepted)";;
        58) __CI_NAME="SSL_CERTPROBLEM (problem with the client certificate)";;
        67) __CI_NAME="LOGIN_DENIED";;
        77) __CI_NAME="SSL_CACERT_BADFILE";;
        *)  __CI_NAME="see 'man curl', EXIT CODES";;
    esac
    if [[ "${__CI_CODE}" == "0" ]]; then
        log "- curl returned: 0 [OK]"
    elif [[ "${__CI_RETRY}" == "yes" ]]; then
        log_warning "- curl returned: ${__CI_CODE} [${__CI_NAME}]"
    else
        log_error "- curl returned: ${__CI_CODE} [${__CI_NAME}]"
    fi
    if [[ -n "${2:-}" ]]; then
        _bamf_return "${2}" "${__CI_RETRY}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: curl_request()
## - Makes an HTTP request (GET, or POST when data is given) and saves the response body to a file,
##   retrying network errors and HTTP status codes 408, 429 and 5xx
## - Succeeds for HTTP status codes 200-299; otherwise logs the response body and returns E_CURL_FAILURE.
##   The status code of the last attempt is left in CURL_LAST_HTTP_CODE.
## - Arguments
##   - $1: URL
##   - $2: Output file (optional, a temporary file if empty)
##   - $3: Log the response body, yes or no (optional, defaults to no)
##   - $4: Data to POST (optional)
##   - $5: User name (optional)
##   - $6: Password (optional)
##   - $7: Client certificate file, PEM (optional)
##   - $8: Resume a partial download into the output file, yes or no (optional, defaults to yes)
##   - $9: Rate limit (optional, example: 1m; defaults to CURL_DEFAULT_LIMIT_RATE, unlimited)
##   - $10: Seconds below 1 byte per second before giving up (optional, defaults to CURL_DEFAULT_SPEED_TIMEOUT)
##   - $11: Maximum seconds for the whole transfer (optional, defaults to CURL_DEFAULT_MAX_TIMEOUT)
function curl_request() {
    local FUNCTION_DESCRIPTION="Curl (Request)"
    local TMP_URL="${1:-}"
    local TMP_FILE_OUTPUT="${2:-}"
    # shellcheck disable=SC2034 # read by name, through option_enabled
    local TMP_DISPLAY_OUTPUT="${3:-no}"
    local TMP_POST_DATA="${4:-}"
    local TMP_USERNAME="${5:-}"
    local TMP_PASSWORD="${6:-}"
    local TMP_CLIENT_CERTIFICATE="${7:-}"
    # shellcheck disable=SC2034 # read by name, through option_enabled
    local TMP_RESUME="${8:-yes}"
    local TMP_HOST=""
    local TMP_FILE_NETRC=""
    local TMP_FILE_POST=""
    local TMP_FILE_ERROR=""
    local TMP_WRITE_OUT=""
    local TMP_SIZE_DOWNLOAD="" TMP_SPEED_DOWNLOAD="" TMP_SIZE_UPLOAD=""
    local TMP_ATTEMPT=0
    local TMP_RETURNVAL=0
    local TMP_RETRY=no
    local -a TMP_OPTIONS=()
    CURL_LAST_HTTP_CODE=""
    if [[ -z "${TMP_URL}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: URL not specified"
        return $E_BAD_ARGS
    fi
    if [[ -n "${TMP_CLIENT_CERTIFICATE}" && ! -f "${TMP_CLIENT_CERTIFICATE}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: client certificate not found [${TMP_CLIENT_CERTIFICATE}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if [[ -z "${TMP_FILE_OUTPUT}" ]]; then
        generate_temp_file TMP_FILE_OUTPUT "curl output"
        # shellcheck disable=SC2034 # read by name, through option_enabled
        TMP_RESUME=no
    fi
    generate_temp_file TMP_FILE_ERROR "curl error output"
    TMP_OPTIONS=( --silent --show-error --location
        --connect-timeout "${CURL_DEFAULT_CONNECT_TIMEOUT}"
        --speed-limit 1 --speed-time "${10:-${CURL_DEFAULT_SPEED_TIMEOUT}}"
        --max-time "${11:-${CURL_DEFAULT_MAX_TIMEOUT}}"
        --limit-rate "${9:-${CURL_DEFAULT_LIMIT_RATE}}"
        --output "${TMP_FILE_OUTPUT}"
        --write-out '%{http_code} %{size_download} %{speed_download} %{size_upload}' )

    log_notice "${FUNCTION_DESCRIPTION}: started"
    log "- URL:              [${TMP_URL}]"
    log "- Output File:      [${TMP_FILE_OUTPUT}]"
    if option_enabled BAMF_CURL_INSECURE; then
        TMP_OPTIONS+=( --insecure )
        log_warning "- TLS certificate verification is disabled (BAMF_CURL_INSECURE)"
    fi
    if [[ -n "${TMP_USERNAME}" ]]; then
        # a netrc file keeps the password off the command line, where other users could see it
        TMP_HOST="${TMP_URL#*://}"
        TMP_HOST="${TMP_HOST%%[/:?#]*}"
        TMP_HOST="${TMP_HOST##*@}"
        generate_temp_file TMP_FILE_NETRC "curl credentials"
        printf 'machine %s login %s password %s\n' "${TMP_HOST}" "${TMP_USERNAME}" "${TMP_PASSWORD}" > "${TMP_FILE_NETRC}"
        TMP_OPTIONS+=( --netrc-file "${TMP_FILE_NETRC}" )
        log "- Credentials:      [${TMP_USERNAME}:${LOG_MASK}]"
    fi
    if [[ -n "${TMP_CLIENT_CERTIFICATE}" ]]; then
        TMP_OPTIONS+=( --cert-type PEM --cert "${TMP_CLIENT_CERTIFICATE}" )
        log "- Client Cert:      [${TMP_CLIENT_CERTIFICATE}]"
    fi
    if [[ -n "${TMP_POST_DATA}" ]]; then
        generate_temp_file_with_content TMP_FILE_POST "${TMP_POST_DATA}" "curl post data"
        TMP_OPTIONS+=( --data-binary "@${TMP_FILE_POST}" )
        log "- Method:           [POST]"
    fi
    if option_enabled TMP_RESUME; then
        TMP_OPTIONS+=( --continue-at - )
    fi

    while (( TMP_ATTEMPT < CURL_DEFAULT_RETRY_COUNT )); do
        TMP_ATTEMPT=$(( TMP_ATTEMPT + 1 ))
        (( TMP_ATTEMPT > 1 )) && call_sleep_random "${CURL_DEFAULT_RETRY_TIMER_MAX_SEC}"
        log "${FUNCTION_DESCRIPTION}: attempt ${TMP_ATTEMPT} of ${CURL_DEFAULT_RETRY_COUNT}"
        TMP_RETURNVAL=0
        TMP_WRITE_OUT="$(command curl "${TMP_OPTIONS[@]}" "${TMP_URL}" 2>"${TMP_FILE_ERROR}")" || TMP_RETURNVAL=$?
        read -r CURL_LAST_HTTP_CODE TMP_SIZE_DOWNLOAD TMP_SPEED_DOWNLOAD TMP_SIZE_UPLOAD <<< "${TMP_WRITE_OUT}" || true
        CURL_LAST_HTTP_CODE="${CURL_LAST_HTTP_CODE:-000}"
        if (( TMP_RETURNVAL != 0 )); then
            log_add_from_file "${TMP_FILE_ERROR}" "${FUNCTION_DESCRIPTION}: curl error output"
            curl_interpret_return "${TMP_RETURNVAL}" TMP_RETRY
            [[ "${TMP_RETRY}" == "yes" ]] && continue
            return $E_CURL_FAILURE
        fi
        if [[ "${CURL_LAST_HTTP_CODE}" =~ ^2[0-9][0-9]$ ]]; then
            log "- HTTP Status:      [${CURL_LAST_HTTP_CODE}]"
            if is_int "${TMP_SIZE_DOWNLOAD}" && (( TMP_SIZE_DOWNLOAD > 0 )); then
                log "- Downloaded:       [$(filesize_bytes_to_human_readable "${TMP_SIZE_DOWNLOAD}")] at [$(filesize_bytes_to_human_readable "${TMP_SPEED_DOWNLOAD%%.*}")/s]"
            fi
            if is_int "${TMP_SIZE_UPLOAD}" && (( TMP_SIZE_UPLOAD > 0 )); then
                log "- Uploaded:         [$(filesize_bytes_to_human_readable "${TMP_SIZE_UPLOAD}")]"
            fi
            option_enabled TMP_DISPLAY_OUTPUT && log_add_from_file "${TMP_FILE_OUTPUT}" "${FUNCTION_DESCRIPTION}: response"
            return 0
        fi
        log_add_from_file "${TMP_FILE_OUTPUT}" "${FUNCTION_DESCRIPTION}: response" 50
        case "${CURL_LAST_HTTP_CODE}" in
            408|429|5[0-9][0-9])
                log_warning "${FUNCTION_DESCRIPTION}: HTTP status ${CURL_LAST_HTTP_CODE}, trying again"
                # a failed response must not be resumed into the next attempt
                : > "${TMP_FILE_OUTPUT}";;
            *)
                log_error "${FUNCTION_DESCRIPTION}: HTTP status ${CURL_LAST_HTTP_CODE}"
                return $E_CURL_FAILURE;;
        esac
    done
    log_error "${FUNCTION_DESCRIPTION}: failed after ${CURL_DEFAULT_RETRY_COUNT} attempts"
    return $E_CURL_FAILURE
}

###------------------------------------------------------------------------------------------------
## FUNCTION: curl_wait_for_http_status()
## - Requests a URL until it answers with the expected HTTP status code, for example to wait for a
##   service to become healthy after it starts
## - Returns 0 when it does, E_OPERATION_TIMEOUT if it never does
## - Arguments
##   - $1: URL
##   - $2: Expected HTTP status code (optional, defaults to 200)
##   - $3: Number of attempts (optional, defaults to 12)
##   - $4: Seconds between attempts (optional, defaults to 10)
function curl_wait_for_http_status() {
    local FUNCTION_DESCRIPTION="Curl (Wait for HTTP Status)"
    local TMP_URL="${1:-}"
    local TMP_EXPECTED="${2:-200}"
    local TMP_ATTEMPTS="${3:-12}"
    local TMP_DELAY="${4:-10}"
    local TMP_ATTEMPT=0
    local TMP_HTTP_CODE=""
    local -a TMP_INSECURE=()
    if [[ -z "${TMP_URL}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: URL not specified"
        return $E_BAD_ARGS
    fi
    if ! is_int "${TMP_ATTEMPTS}" || ! is_int "${TMP_DELAY}"; then
        log_error "${FUNCTION_DESCRIPTION}: attempts and delay must be numbers"
        return $E_BAD_ARGS
    fi
    option_enabled BAMF_CURL_INSECURE && TMP_INSECURE=( --insecure )
    log_notice "${FUNCTION_DESCRIPTION}: waiting for [${TMP_URL}] to answer ${TMP_EXPECTED}"
    while (( TMP_ATTEMPT < TMP_ATTEMPTS )); do
        TMP_ATTEMPT=$(( TMP_ATTEMPT + 1 ))
        TMP_HTTP_CODE="$(command curl --silent --output /dev/null --max-time 10 "${TMP_INSECURE[@]+"${TMP_INSECURE[@]}"}" \
            --write-out '%{http_code}' "${TMP_URL}" 2>/dev/null)" || true
        if [[ "${TMP_HTTP_CODE}" == "${TMP_EXPECTED}" ]]; then
            log_status PASS "${TMP_URL} answered ${TMP_HTTP_CODE} (attempt ${TMP_ATTEMPT} of ${TMP_ATTEMPTS})"
            return 0
        fi
        log_status WAIT "${TMP_URL} answered ${TMP_HTTP_CODE:-nothing} (attempt ${TMP_ATTEMPT} of ${TMP_ATTEMPTS})"
        (( TMP_ATTEMPT < TMP_ATTEMPTS )) && command sleep "${TMP_DELAY}"
    done
    log_error "${FUNCTION_DESCRIPTION}: [${TMP_URL}] did not answer ${TMP_EXPECTED} after ${TMP_ATTEMPTS} attempts"
    return $E_OPERATION_TIMEOUT
}
