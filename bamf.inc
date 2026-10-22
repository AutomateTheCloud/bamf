###------------------------------------------------------------------------------------------------
# script:  bamf.inc
# purpose: BASH Application Management Framework (BAMF) core library
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          SELF_IDENTITY="my_script"            # optional, defaults to the script's file name
#          source "/path/to/bamf.inc" || exit 3
#          REQUIRED_SOURCE_FILES+=( aws )       # optional helper files (helpers/bamf.aws.inc), loaded by start_logic
#          start_logic
#          log "Hello"
#          exit_logic 0
#
# requirements:
#          - bash 4.2 or newer, GNU coreutils (date, mktemp, stat)
#
# conventions:
#          - functions that return a value take the name of a variable to store it in; when that
#            argument is optional and left empty, the value is printed instead
#          - result variable names must not start with two underscores (reserved for BAMF locals)
#          - functions return an exit code (see define_exit_code); 0 is success
#
# function list:
#          - add_element_to_array
#          - add_exit_function
#          - base64_decode
#          - base64_encode
#          - base64_encode_and_compress
#          - call_sleep
#          - call_sleep_random
#          - cleanup
#          - cleanup_quiet
#          - color_lookup
#          - color_text
#          - debug_color_text
#          - define_exit_code
#          - does_array_contain_element
#          - echo_key_value
#          - exec_script
#          - exit_logic
#          - exit_logic_quiet
#          - exit_logic_skip_cleanup
#          - exit_trap
#          - exit_trap_ensure_graceful
#          - explode_key_value_to_file
#          - file_update_owner
#          - file_update_permissions
#          - filesize_bytes_to_human_readable
#          - function_exists
#          - generate_temp_directory
#          - generate_temp_file
#          - generate_temp_file_with_content
#          - generate_uuid
#          - is_empty
#          - is_int
#          - is_running_as_root
#          - is_server_up
#          - json_escape
#          - json_extract_from_string
#          - json_safe
#          - line_break
#          - load_array_key_values_from_file
#          - load_array_key_values_from_yaml_file
#          - load_array_properties_from_file
#          - load_key_value_from_file
#          - load_property_from_file
#          - log
#          - log_add_from_file
#          - log_error
#          - log_file_content
#          - log_highlight
#          - log_load_color_child
#          - log_load_color_parent
#          - log_notice
#          - log_quiet
#          - log_status
#          - log_success
#          - log_warning
#          - lookup_exit_code
#          - option_enabled
#          - parse_yaml
#          - parse_yaml_to_file
#          - return_file_last_modify_timestamp
#          - return_file_md5sum
#          - return_file_mime
#          - return_file_sha1sum
#          - return_file_sha256sum
#          - return_filesize_of_file
#          - return_filesize_of_file_in_bytes
#          - return_header_string
#          - return_hostname
#          - return_parameter_string
#          - return_script_purpose
#          - return_script_version
#          - return_variable_value
#          - return_yaml_string
#          - run_silently
#          - source_include
#          - start_logic
#          - start_logic_quiet
#          - sync_disks
#          - time_elapsed
#          - timestamp_to_human_readable
#          - to_lower
#          - to_upper
#          - trim
#          - uptime_greater_than
#          - usage
#          - usage_banner
#          - usage_normalized_arguments
#          - verbose_display_date
#          - verify_array_key_values
#          - verify_dependencies
#          - version_to_integer
#          - write_dotenv_value
#          - write_key_value_to_file
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Load once
# a second source (for example from a helper file) is a no-op
if [[ -n "${BAMF_LOADED:-}" ]]; then
    return 0
fi

###------------------------------------------------------------------------------------------------
## Bash version check
if [ -z "${BASH_VERSINFO:-}" ] || [ "${BASH_VERSINFO[0]}" -lt 4 ] || { [ "${BASH_VERSINFO[0]}" -eq 4 ] && [ "${BASH_VERSINFO[1]}" -lt 2 ]; }; then
    echo "ERROR: BAMF requires bash 4.2 or newer" >&2
    # shellcheck disable=SC2317 # exit is reached when the file is executed instead of sourced
    return 3 2>/dev/null || exit 3
fi

###------------------------------------------------------------------------------------------------
## Self Identity
declare -g BAMF_LOADED=yes
# shellcheck disable=SC2034 # for scripts that source this file
declare -g BAMF_VERSION="1.0.0"
__BAMF_SOURCE="${BASH_SOURCE[0]}"
[[ "${__BAMF_SOURCE}" == */* ]] || __BAMF_SOURCE="./${__BAMF_SOURCE}"
declare -g BAMF_DIRECTORY
BAMF_DIRECTORY="$(cd "${__BAMF_SOURCE%/*}" && pwd)"
# shellcheck disable=SC2034 # for scripts that source this file
declare -g BAMF_FILE="${BAMF_DIRECTORY}/bamf.inc"
declare -g BAMF_HELPER_DIRECTORY="${BAMF_DIRECTORY}/helpers"
unset __BAMF_SOURCE

###------------------------------------------------------------------------------------------------
## Define Path
# the sbin directories are missing from the PATH of cron and some service managers
for __BAMF_PATH in /usr/local/sbin /usr/local/bin /usr/sbin /usr/bin; do
    if [[ ":${PATH}:" != *":${__BAMF_PATH}:"* ]]; then
        export PATH="${PATH}:${__BAMF_PATH}"
    fi
done
unset __BAMF_PATH

###------------------------------------------------------------------------------------------------
## Configuration
# Each setting can be given by the script (before sourcing bamf.inc) or by whoever runs it
# (BAMF_* environment variables, which win over the script's value)
if [[ -z "${SELF_IDENTITY:-}" ]]; then
    __BAMF_IDENTITY="${0##*/}"
    declare -g SELF_IDENTITY="${__BAMF_IDENTITY%.*}"
    unset __BAMF_IDENTITY
fi

# Color, first match wins:
#   1. BAMF_ENABLE_COLOR (yes or no), set by whoever runs the script
#   2. NO_COLOR (any value turns color off, see https://no-color.org)
#   3. ENABLE_COLOR (yes or no), the script's own default
#   4. on when the output is a terminal, off otherwise
if [[ -n "${BAMF_ENABLE_COLOR:-}" ]]; then
    ENABLE_COLOR="${BAMF_ENABLE_COLOR}"
elif [[ -n "${NO_COLOR:-}" ]]; then
    ENABLE_COLOR=no
elif [[ -z "${ENABLE_COLOR:-}" ]]; then
    if [[ -t 1 ]]; then
        ENABLE_COLOR=yes
    else
        ENABLE_COLOR=no
    fi
fi

# Timestamp on every log line, first match wins:
#   1. BAMF_ENABLE_TIMESTAMP (yes or no), set by whoever runs the script
#   2. ENABLE_TIMESTAMP (yes or no), the script's own default
#   3. on when the output is not a terminal (a log file, a pipe, CI, cron, CodeDeploy), where the
#      time is needed later; off on a terminal, where the output is read as it happens
if [[ -n "${BAMF_ENABLE_TIMESTAMP:-}" ]]; then
    ENABLE_TIMESTAMP="${BAMF_ENABLE_TIMESTAMP}"
elif [[ -z "${ENABLE_TIMESTAMP:-}" ]]; then
    if [[ -t 1 ]]; then
        ENABLE_TIMESTAMP=no
    else
        ENABLE_TIMESTAMP=yes
    fi
fi

# Temporary directory: BAMF_DIRECTORY_TEMPORARY, then DIRECTORY_TEMPORARY, then TMPDIR, then /tmp
__BAMF_DIRECTORY_TEMPORARY="${BAMF_DIRECTORY_TEMPORARY:-${DIRECTORY_TEMPORARY:-${TMPDIR:-/tmp}}}"
__BAMF_DIRECTORY_TEMPORARY="${__BAMF_DIRECTORY_TEMPORARY%/}"
# cleanup removes temporary files beneath this directory, so it must be a real, absolute path
if [[ "${__BAMF_DIRECTORY_TEMPORARY}" != /?* ]]; then
    echo "ERROR: temporary directory must be an absolute path other than / [${__BAMF_DIRECTORY_TEMPORARY}]" >&2
    # shellcheck disable=SC2317 # exit is reached when the file is executed instead of sourced
    return 3 2>/dev/null || exit 3
fi
if [[ ! -d "${__BAMF_DIRECTORY_TEMPORARY}" ]]; then
    if ! command mkdir -p "${__BAMF_DIRECTORY_TEMPORARY}" 2>/dev/null; then
        echo "ERROR: could not create temporary directory [${__BAMF_DIRECTORY_TEMPORARY}]" >&2
        # shellcheck disable=SC2317 # exit is reached when the file is executed instead of sourced
        return 3 2>/dev/null || exit 3
    fi
fi
declare -g DIRECTORY_TEMPORARY="${__BAMF_DIRECTORY_TEMPORARY}"
unset __BAMF_DIRECTORY_TEMPORARY

###------------------------------------------------------------------------------------------------
## Dependencies
# Scripts and helper files add to these arrays; start_logic loads and checks them
[[ -n "${REQUIRED_EXECUTABLES+set}" ]] || declare -ga REQUIRED_EXECUTABLES=()
[[ -n "${REQUIRED_SOURCE_FILES+set}" ]] || declare -ga REQUIRED_SOURCE_FILES=()
[[ -n "${ARGUMENTS_DESCRIPTION+set}" ]] || declare -ga ARGUMENTS_DESCRIPTION=()
REQUIRED_EXECUTABLES+=('date' 'mkdir' 'mktemp' 'rm')
declare -ga BAMF_LOADED_SOURCE_FILES=()

# Option letters whose values are masked in the script start log line (example: "sp" for -s <secret> -p <password>)
declare -g SENSITIVE_ARGUMENTS="${SENSITIVE_ARGUMENTS:-}"

###------------------------------------------------------------------------------------------------
## Temp Related
declare -ga TEMPORARY_FILES=()
declare -ga TEMPORARY_DIRECTORIES=()

###------------------------------------------------------------------------------------------------
## Exit Functions
# Functions the script registered with add_exit_function, run when the script exits
declare -ga BAMF_EXIT_FUNCTIONS=()
# shellcheck disable=SC2034 # read by the functions registered with add_exit_function
declare -g BAMF_EXIT_CODE=""

###------------------------------------------------------------------------------------------------
## Exit Codes
# Ranges:
#   0-80, 251-255  BAMF core
#   81-120, 151-200 BAMF core and helper files
#   121-150        bash and signals
#   201-250        free for your own scripts (define_exit_code E_MY_FAILURE 201 "What went wrong")
# bash 4.2 quirk: inside a function (when bamf.inc is sourced from one), "declare -gA NAME=()"
# creates the global array but later assignments do not reach it; declaring first and assigning
# separately works in every version
declare -gA BAMF_EXIT_CODE_NAMES
declare -gA BAMF_EXIT_CODE_DESCRIPTIONS
BAMF_EXIT_CODE_NAMES=()
BAMF_EXIT_CODE_DESCRIPTIONS=()

###------------------------------------------------------------------------------------------------
## FUNCTION: define_exit_code()
## - Defines a named exit code (declares the variable and registers it for lookup_exit_code)
## - Returns E_BAD_ARGS for an invalid definition, E_OBJECT_ALREADY_EXISTS if the number is used by another name
## - Arguments
##   - $1: Name (must start with E_, example: E_MY_FAILURE)
##   - $2: Number (0-255)
##   - $3: Description (optional)
function define_exit_code() {
    local TMP_NAME="${1:-}"
    local TMP_CODE="${2:-}"
    local TMP_DESCRIPTION="${3:-}"
    if [[ ! "${TMP_NAME}" =~ ^E_[A-Z0-9_]+$ ]] || [[ ! "${TMP_CODE}" =~ ^[0-9]{1,3}$ ]] || (( 10#${TMP_CODE} > 255 )); then
        printf 'ERROR: define_exit_code: invalid exit code definition [%s=%s]\n' "${TMP_NAME}" "${TMP_CODE}" >&2
        return 5 # E_BAD_ARGS
    fi
    TMP_CODE=$((10#${TMP_CODE}))
    if [[ -n "${BAMF_EXIT_CODE_NAMES[${TMP_CODE}]:-}" ]] && [[ "${BAMF_EXIT_CODE_NAMES[${TMP_CODE}]}" != "${TMP_NAME}" ]]; then
        printf 'ERROR: define_exit_code: exit code %s is already defined as %s [%s]\n' "${TMP_CODE}" "${BAMF_EXIT_CODE_NAMES[${TMP_CODE}]}" "${TMP_NAME}" >&2
        return 9 # E_OBJECT_ALREADY_EXISTS
    fi
    declare -g "${TMP_NAME}=${TMP_CODE}"
    BAMF_EXIT_CODE_NAMES[${TMP_CODE}]="${TMP_NAME}"
    BAMF_EXIT_CODE_DESCRIPTIONS[${TMP_CODE}]="${TMP_DESCRIPTION}"
    return 0
}

# Core
define_exit_code E_SUCCESS                      0 "Success"
define_exit_code E_BASH_GENERIC_FAILURE         1 "Generic failure (catch all)"
define_exit_code E_BASH_MISUSE_BUILTIN          2 "Misuse of shell builtins (BASH builtin)"
define_exit_code E_SCRIPT_MISCONFIGURED         3 "Script has failed its configuration check"
define_exit_code E_ILLEGAL_EXECUTION            4 "Script is not allowed to run under the conditions it was executed under"
define_exit_code E_BAD_ARGS                     5 "Bad or missing arguments"
define_exit_code E_NOT_AUTHORIZED               6 "User is not authorized to run this script"
define_exit_code E_MISSING_DEPENDENCIES         7 "Missing required dependency"
define_exit_code E_OBJECT_NOT_FOUND             8 "Object not found"
define_exit_code E_OBJECT_ALREADY_EXISTS        9 "Object already exists"
define_exit_code E_OBJECT_FAILED_TO_CREATE     10 "Could not create object"
define_exit_code E_PREMATURE_TERMINATION       11 "Script has prematurely terminated"
define_exit_code E_OFFLINE                     12 "Machine is offline and can not connect to the network"
define_exit_code E_SYSTEMCTL_FAILURE           13 "Systemctl execution failed"
define_exit_code E_LOG_RELATED_FAILURE         14 "A failure occurred related to processing logs"
define_exit_code E_CRON_CONFIGURATION_FAILURE  15 "There was a failure during the generation of the Cron Configuration files"
define_exit_code E_FILE_CONVERSION_FAILURE     16 "Failed to convert file to a different format"
define_exit_code E_CHOWN_FAILURE               21 "chown execution failed"
define_exit_code E_CHMOD_FAILURE               22 "chmod execution failed"
define_exit_code E_RSYNC_FAILURE               23 "Rsync execution failed"
define_exit_code E_CURL_FAILURE                24 "Curl execution failed"
define_exit_code E_SQLITE_FAILURE              25 "SQLite execution failed"
define_exit_code E_OPENSSL_FAILURE             26 "OpenSSL execution failed"
define_exit_code E_SSH_FAILURE                 27 "SSH execution failed"
define_exit_code E_SSH_CONNECTION_FAILURE      28 "An error occurred while processing request via SSH session"
define_exit_code E_PACKAGE_MANAGEMENT_FAILURE  29 "Package Management execution failed"
define_exit_code E_GIT_FAILURE                 30 "Git execution failed"
define_exit_code E_DATABASE_CONNECTION_FAILURE 31 "Failed to connect to database"
define_exit_code E_BASE64_CONVERSION_FAILURE   32 "Failed to perform Base64 conversion operation"
define_exit_code E_SERVICE_FAILURE             82 "Service failed to start"
define_exit_code E_XML_VALIDATION_FAILURE      83 "XML file failed validation"
define_exit_code E_DNS_FAILURE                100 "Failed to set DNS record"
define_exit_code E_SSL_FAILURE                110 "Failed to perform operation associated with SSL certificates"
define_exit_code E_CHECKSUM_MISMATCH          111 "Checksum Mismatch"
define_exit_code E_SUCCESS_ALT                251 "Script has exited without error, however in the case of this exit code, we wanted it to have a non-zero exit code"
define_exit_code E_SCRIPT_ALREADY_RUNNING     252 "Script already running (THERE CAN BE ONLY ONE)"
define_exit_code E_RESONANCE_CASCADE          254 "Script has errored in such a way that it should have never reached this point (This indicates a flaw in script logic and MUST be investigated)"
define_exit_code E_UNKNOWN_EXIT_CODE          255 "Unknown exit code"

# Bash and signals
define_exit_code E_OPERATION_TIMEOUT          124 "Requested operation timed out (normally returned on error when [timeout] command is used)"
define_exit_code E_BASH_CANNOT_EXECUTE_COMMAND 126 "Command invoked cannot execute (Likely permissions related) (BASH builtin)"
define_exit_code E_BASH_COMMAND_NOT_FOUND     127 "Command not found (BASH builtin)"
define_exit_code E_BASH_INVALID_EXIT          128 "Invalid argument to exit (BASH builtin)"
define_exit_code E_TRAP_SIGHUP                129 "Trap SIGHUP [1] (Hangup) - The controlling terminal has gone away"
define_exit_code E_TRAP_SIGINT                130 "Trap SIGINT [2] (Interrupt) - The user has pressed the interrupt key (usually CTRL+c or CTRL+DEL)"
define_exit_code E_TRAP_SIGQUIT               131 "Trap SIGQUIT [3] (Quit) - The user has pressed the quit key (usually CTRL+\\, CTRL+4, or SysRq Key)"
define_exit_code E_TRAP_SIGKILL               137 "Trap SIGKILL [9] (Kill) - Something has sent the sigkill signal to this script. This signal cannot be caught by normal means"
define_exit_code E_TRAP_SIGPIPE               141 "Trap SIGPIPE [13] (Broken Pipe) - An attempt to write to pipe with no readers"
define_exit_code E_TRAP_SIGTERM               143 "Trap SIGTERM [15] (Terminate) - This is the default signal sent by the kill command"
define_exit_code E_TRAP_SIGSTOP               147 "Trap SIGSTOP [19] (Stop) - Something has sent the stop signal to this script. This signal cannot be caught by normal means"
define_exit_code E_TRAP_SIGTSTP               148 "Trap SIGTSTP [20] (Keyboard Stop) - Stop typed at tty (usually CTRL+z)"

###------------------------------------------------------------------------------------------------
## Colors
COLOR_BLACK='\e[30m'
COLOR_BLUE='\e[0;34m'
COLOR_CYAN='\e[0;36m'
COLOR_DARK_GRAY='\e[0;90m'
COLOR_GREEN='\e[0;32m'
COLOR_LIGHT_BLUE='\e[1;34m'
COLOR_LIGHT_CYAN='\e[1;36m'
COLOR_LIGHT_GRAY='\e[0;37m'
COLOR_LIGHT_GREEN='\e[1;32m'
COLOR_LIGHT_MAGENTA='\e[1;35m'
COLOR_LIGHT_PURPLE='\e[1;35m'
COLOR_LIGHT_RED='\e[1;31m'
COLOR_LIGHT_YELLOW='\e[1;33m'
COLOR_MAGENTA='\e[0;35m'
COLOR_PURPLE='\e[0;35m'
COLOR_RED='\e[0;31m'
COLOR_WHITE='\e[1;37m'
COLOR_YELLOW='\e[0;33m'
COLOR_NONE='\e[0m'

###------------------------------------------------------------------------------------------------
## Logging
# every log line is written with a single printf builtin; no subshells or external binaries are
# used per log line. Message text is printed as-is: backslash sequences are never interpreted.
# LOG_COLOR_* variables hold the actual escape bytes (converted once when the palette is loaded).
# Timestamp format for log lines (with ENABLE_TIMESTAMP): BAMF_LOG_DATE_FORMAT, then the script's
# LOG_DISPLAY_DATE_FORMAT, then the default. Any strftime format (see man strftime), written by the
# printf builtin with no subprocess, which means no fractions of a second.
LOG_DISPLAY_DATE_FORMAT="${BAMF_LOG_DATE_FORMAT:-${LOG_DISPLAY_DATE_FORMAT:-%Y-%m-%d %H:%M:%S}}"
printf -v LOG_PAD '%.0s.' {1..60}
# Log lines start with "[identity]", padded with dots to the width of the identity column, so the
# messages line up. The column is as wide as the script's own name, or BAMF_LOG_IDENTITY_WIDTH when
# that is wider. exec_script passes the column width on to the scripts it runs, so a child's lines
# line up under its parent's; set BAMF_LOG_IDENTITY_WIDTH yourself to line up a whole set of scripts.
LOG_IDENTITY_WIDTH=0
DEFAULT_ADD_TO_LOG_LINE_LIMIT=1000
DEFAULT_SCREEN_WIDTH=120
LOG_MASK="********" # displayed in place of sensitive values (fixed length, the real length is not revealed)

LOG_COLOR_DATE=""
LOG_COLOR_IDENT=""
LOG_COLOR_MESSAGE_IMPORTANT=""
LOG_COLOR_MESSAGE=""
LOG_COLOR_NOTICE=""
LOG_COLOR_WARNING=""
LOG_COLOR_ERROR=""
LOG_COLOR_SUCCESS=""
LOG_COLOR_FILE_HEADER=""
LOG_COLOR_FILE=""
LOG_COLOR_HIGHLIGHT=""
LOG_COLOR_CLI=""
LOG_COLOR_DETAILS=""

LOG_PREFIX_PLAIN=""
LOG_PREFIX_COLOR=""
LOG_LINE_BREAK=""

###------------------------------------------------------------------------------------------------
## FUNCTION: add_element_to_array()
## - Appends one or more elements to the array with the specified name
## - Arguments
##   - $1: Array Name
##   - $2+: Element(s)
function add_element_to_array() {
    local __ARRAY_NAME="${1:-}"
    local __ELEMENT=""
    if [[ ! "${__ARRAY_NAME}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        log_error "Add Element to Array: invalid array name [${__ARRAY_NAME}]"
        return $E_BAD_ARGS
    fi
    shift
    for __ELEMENT in "$@"; do
        # the element is referenced, never expanded into the evaluated code
        eval "${__ARRAY_NAME}+=( \"\${__ELEMENT}\" )"
    done
}

###------------------------------------------------------------------------------------------------
## FUNCTION: add_exit_function()
## - Registers a function to run when the script exits, however it exits: exit_logic, an error under
##   set -e, the end of the script, or a signal such as Ctrl+C. Use it to undo what the script set up
##   outside the machine's temporary directory (a lock, a DNS record, a test resource in the cloud).
## - Functions run once each, the last registered first, before the temporary files are removed;
##   BAMF_EXIT_CODE holds the code the script is exiting with. A failing function is logged and does
##   not change the exit code.
## - Requires start_logic, which sets up the exit handling
## - Arguments
##   - $1: Function name
function add_exit_function() {
    if ! declare -F "${1:-}" >/dev/null 2>&1; then
        log_error "Add Exit Function: no function named [${1:-}]"
        return $E_BAD_ARGS
    fi
    if ! does_array_contain_element "${1}" "${BAMF_EXIT_FUNCTIONS[@]+"${BAMF_EXIT_FUNCTIONS[@]}"}"; then
        BAMF_EXIT_FUNCTIONS+=( "${1}" )
    fi
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: base64_decode()
## - Decodes a base64 string
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the decoded string is printed)
##   - $2: Encoded string
function base64_decode() {
    local __STRING=""
    if ! __STRING="$(printf '%s' "${2:-}" | command base64 -d 2>/dev/null)"; then
        log_error "Base64 Decode: input is not valid base64"
        return $E_BASE64_CONVERSION_FAILURE
    fi
    _bamf_return "${1:-}" "${__STRING}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: base64_encode()
## - Encodes a string to base64 (single line, the string is encoded exactly as given)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the encoded string is printed)
##   - $2: String
function base64_encode() {
    local __STRING=""
    __STRING="$(printf '%s' "${2:-}" | command base64 -w 0)"
    _bamf_return "${1:-}" "${__STRING}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: base64_encode_and_compress()
## - Compresses a string (gzip -9) and encodes the result to base64 (single line)
## - Decode with: printf '%s' "${STRING}" | base64 -d | gunzip
## - Returns E_MISSING_DEPENDENCIES if gzip is not installed
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the encoded string is printed)
##   - $2: String
function base64_encode_and_compress() {
    local __STRING=""
    if ! command -v gzip >/dev/null 2>&1; then
        log_error "Base64 Encode and Compress: gzip is not installed"
        return $E_MISSING_DEPENDENCIES
    fi
    __STRING="$(printf '%s' "${2:-}" | command gzip -9 -c -n | command base64 -w 0)"
    _bamf_return "${1:-}" "${__STRING}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: call_sleep()
## - Sleeps for the specified number of seconds and logs it
## - Arguments
##   - $1: Number of seconds to sleep
##   - $2: Reason (optional)
function call_sleep() {
    local TMP_SECONDS="${1:-}"
    local TMP_REASON_STRING=""
    local TMP_STRING_SECOND="seconds"
    if ! is_int "${TMP_SECONDS}" || (( TMP_SECONDS < 0 )); then
        log_error "Call Sleep: invalid number of seconds [${TMP_SECONDS}]"
        return $E_BAD_ARGS
    fi
    if [[ -n "${2:-}" ]]; then
        TMP_REASON_STRING=" [${2}]"
    fi
    if (( TMP_SECONDS == 1 )); then
        TMP_STRING_SECOND="second"
    fi
    log "- Sleeping for ${TMP_SECONDS} ${TMP_STRING_SECOND}${TMP_REASON_STRING}"
    command sleep "${TMP_SECONDS}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: call_sleep_random()
## - Sleeps for a random number of seconds between 1 and the specified maximum, and logs it
## - Arguments
##   - $1: Max sleep time in seconds (optional, defaults to 60; 0 does not sleep)
function call_sleep_random() {
    local TMP_TIMER_WINDOW="${1:-60}"
    local TMP_SLEEP_TIMER=0
    local TMP_STRING_SECOND="seconds"
    if ! is_int "${TMP_TIMER_WINDOW}" || (( TMP_TIMER_WINDOW < 0 )); then
        log_error "Call Sleep Random: invalid maximum number of seconds [${TMP_TIMER_WINDOW}]"
        return $E_BAD_ARGS
    fi
    if (( TMP_TIMER_WINDOW > 0 )); then
        TMP_SLEEP_TIMER=$(( (RANDOM % TMP_TIMER_WINDOW) + 1 ))
    fi
    if (( TMP_SLEEP_TIMER == 1 )); then
        TMP_STRING_SECOND="second"
    fi
    log "- Sleeping for ${TMP_SLEEP_TIMER} ${TMP_STRING_SECOND} (random, Base_Timer_Window::${TMP_TIMER_WINDOW})"
    command sleep "${TMP_SLEEP_TIMER}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: cleanup()
## - Removes the temporary files and directories created by generate_temp_file / generate_temp_directory
## - Only removes entries located beneath the temporary directory (or /tmp); anything else is
##   skipped with a warning, for safety
function cleanup() {
    local ELEMENT=""
    for ELEMENT in "${TEMPORARY_FILES[@]+"${TEMPORARY_FILES[@]}"}"; do
        if _bamf_is_temporary_path "${ELEMENT}"; then
            command rm -f -- "${ELEMENT}" 2>/dev/null || true
        else
            log_warning "Cleanup: Could not remove generated temporary file for safety reasons, file is not located in a known temporary directory [${ELEMENT}]"
        fi
    done
    for ELEMENT in "${TEMPORARY_DIRECTORIES[@]+"${TEMPORARY_DIRECTORIES[@]}"}"; do
        if _bamf_is_temporary_path "${ELEMENT}"; then
            command rm -rf -- "${ELEMENT}" 2>/dev/null || true
        else
            log_warning "Cleanup: Could not remove generated temporary directory for safety reasons, directory is not located in a known temporary directory [${ELEMENT}]"
        fi
    done
    TEMPORARY_FILES=()
    TEMPORARY_DIRECTORIES=()
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: cleanup_quiet()
## - Same as cleanup, without logging
function cleanup_quiet() {
    cleanup >/dev/null 2>&1
}

###------------------------------------------------------------------------------------------------
## FUNCTION: color_lookup()
## - Prints the color definition for a color name (an unknown name is printed back unchanged)
## - Arguments
##   - $1: color name (example: light_cyan)
function color_lookup() {
    case "${1,,}" in
        black)        printf '%s' "${COLOR_BLACK}";;
        blue)         printf '%s' "${COLOR_BLUE}";;
        cyan)         printf '%s' "${COLOR_CYAN}";;
        dark_gray)    printf '%s' "${COLOR_DARK_GRAY}";;
        green)        printf '%s' "${COLOR_GREEN}";;
        light_blue)   printf '%s' "${COLOR_LIGHT_BLUE}";;
        light_cyan)   printf '%s' "${COLOR_LIGHT_CYAN}";;
        light_gray)   printf '%s' "${COLOR_LIGHT_GRAY}";;
        light_green)  printf '%s' "${COLOR_LIGHT_GREEN}";;
        light_magenta) printf '%s' "${COLOR_LIGHT_MAGENTA}";;
        light_purple) printf '%s' "${COLOR_LIGHT_PURPLE}";;
        light_red)    printf '%s' "${COLOR_LIGHT_RED}";;
        light_yellow) printf '%s' "${COLOR_LIGHT_YELLOW}";;
        magenta)      printf '%s' "${COLOR_MAGENTA}";;
        purple)       printf '%s' "${COLOR_PURPLE}";;
        red)          printf '%s' "${COLOR_RED}";;
        white)        printf '%s' "${COLOR_WHITE}";;
        yellow)       printf '%s' "${COLOR_YELLOW}";;
        none)         printf '%s' "${COLOR_NONE}";;
        *)            printf '%s' "${1:-}";;
    esac
}

###------------------------------------------------------------------------------------------------
## FUNCTION: color_text()
## - Prints text gently swaddled in radiant color
## - The text is printed as-is (backslash sequences in the text are not interpreted)
## - When color is disabled (ENABLE_COLOR), the text is printed without color codes
## - Arguments
##   - $1: color (example: "${COLOR_YELLOW}" or "$(color_lookup yellow)")
##   - $2: text
##   - $3: color to resume (optional, defaults to the log message color)
function color_text() {
    if ! option_enabled ENABLE_COLOR; then
        printf '%s' "${2:-}"
        return 0
    fi
    printf '%b%s%b' "${1:-}" "${2:-}" "${3:-${LOG_COLOR_MESSAGE}}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: debug_color_text()
## - Prints a test line in each of the defined colors (for debug purposes only)
function debug_color_text() {
    local TMP_COLOR=""
    for TMP_COLOR in black blue cyan dark_gray green light_blue light_cyan light_gray light_green light_magenta light_purple light_red light_yellow magenta purple red white yellow none; do
        printf '%s\n' "$(color_text "$(color_lookup "${TMP_COLOR}")" "1234567890 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ - ${TMP_COLOR}")"
    done
}

###------------------------------------------------------------------------------------------------
## FUNCTION: does_array_contain_element()
## - Returns true if the array contains the element
## - Arguments
##   - $1: Element string to search for
##   - $2+: Array (pass this way: "${array[@]}")
function does_array_contain_element() {
    local TMP_ELEMENT=""
    for TMP_ELEMENT in "${@:2}"; do
        [[ "${TMP_ELEMENT}" == "${1:-}" ]] && return 0
    done
    return 1
}

###------------------------------------------------------------------------------------------------
## FUNCTION: echo_key_value()
## - Prints a [key]=value line, the format read by load_key_value_from_file (the key is lowercased)
## - Arguments
##   - $1: key
##   - $2: value
function echo_key_value() {
    local TMP_KEY=""
    TMP_KEY="$(trim "${1:-}")"
    printf '[%s]=%s\n' "${TMP_KEY,,}" "$(trim "${2:-}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: exec_script()
## - Runs another script as a child (inception?), which logs in the child color palette
## - Returns the exit code of the script that was run
## - Arguments
##   - $1: Script to run
##   - $2+: Arguments to pass to the script
function exec_script() {
    local FUNCTION_DESCRIPTION="Execute Script"
    local RETURNVAL=0
    if [[ $# -eq 0 ]] || [[ -z "${1}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: no script defined"
        return $E_MISSING_DEPENDENCIES
    fi
    if [[ ! -f "${1}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: Could not locate script [${1}]"
        return $E_OBJECT_NOT_FOUND
    fi
    log "${FUNCTION_DESCRIPTION}: Running script (File::${1##*/} / Version::$(return_script_version "${1}"))"
    # the child uses the child color palette, and pads its name to this script's column, so its
    # lines line up under this script's
    RUN_SCRIPT_AS_CHILD=yes BAMF_LOG_IDENTITY_WIDTH="${LOG_IDENTITY_WIDTH}" "$@" || RETURNVAL=$?
    if [ ${RETURNVAL} -ne 0 ]; then
        log_error "${FUNCTION_DESCRIPTION}: Script failed (Script::${1##*/} / Return_Code::${RETURNVAL} [$(lookup_exit_code ${RETURNVAL})])"
    else
        log "${FUNCTION_DESCRIPTION}: Script returned success (Script::${1##*/} / Return_Code::${RETURNVAL})"
    fi
    return ${RETURNVAL}
}

###------------------------------------------------------------------------------------------------
## FUNCTION: exit_logic()
## - Ends the script: removes temporary files, logs the duration and exit code, restores the terminal
## - An exit code given but empty (exit_logic "${E_MISTYPED}") ends the script with E_UNKNOWN_EXIT_CODE,
##   not 0: an unset variable must never turn a failure into a success. Quote the code, as in
##   exit_logic "${E_BAD_ARGS}", so this check can see it; unquoted, an empty value disappears.
## - Arguments
##   - $1: Exit code to return (optional, defaults to 0)
##   - $2: Exit message (optional)
function exit_logic() {
    _bamf_exit "$(_bamf_exit_code_argument "$#" "${1:-}")" "${2:-}" normal
}

###------------------------------------------------------------------------------------------------
## FUNCTION: exit_logic_quiet()
## - Same as exit_logic, without logging
## - Arguments
##   - $1: Exit code to return (optional, defaults to 0)
function exit_logic_quiet() {
    _bamf_exit "${1:-0}" "" quiet
}

###------------------------------------------------------------------------------------------------
## FUNCTION: exit_logic_skip_cleanup()
## - Same as exit_logic, but leaves the temporary files in place (useful for debugging)
## - Arguments
##   - $1: Exit code to return (optional, defaults to 0)
##   - $2: Exit message (optional)
function exit_logic_skip_cleanup() {
    _bamf_exit "$(_bamf_exit_code_argument "$#" "${1:-}")" "${2:-}" skip_cleanup
}

###------------------------------------------------------------------------------------------------
## FUNCTION: exit_trap()
## - Exit logic for a caught signal (set up by start_logic)
## - Arguments
##   - $1: Exit code to return
##   - $2: Exit message (optional)
function exit_trap() {
    _bamf_exit "${1:-${E_UNKNOWN_EXIT_CODE}}" "${2:-}" trap
}

###------------------------------------------------------------------------------------------------
## FUNCTION: exit_trap_ensure_graceful()
## - Exit logic for the EXIT trap (set up by start_logic)
## - A safety net for a script that ends without calling exit_logic (or that stops on an error
##   under set -e): the temporary files are still removed and the exit code is still logged
function exit_trap_ensure_graceful() {
    local CAUGHT_EXIT=$?
    if ! option_enabled GRACEFUL_EXIT; then
        exit_logic "${CAUGHT_EXIT}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: explode_key_value_to_file()
## - Writes each element of a delimited string to a file as a [key]=value line
## - An element without "=" is written with the value "true"
## - Arguments
##   - $1: File to write to
##   - $2: Delimited string (example: "name=web;port=443;debug")
##   - $3: Delimiter (optional, defaults to ";")
function explode_key_value_to_file() {
    local TMP_FILE="${1:-}"
    local -a TMP_ELEMENTS=()
    local TMP_ELEMENT=""
    IFS="${3:-;}" read -r -a TMP_ELEMENTS <<< "${2:-}"
    for TMP_ELEMENT in "${TMP_ELEMENTS[@]+"${TMP_ELEMENTS[@]}"}"; do
        [[ -n "${TMP_ELEMENT}" ]] || continue
        if [[ "${TMP_ELEMENT}" == *=* ]]; then
            write_key_value_to_file "${TMP_FILE}" "${TMP_ELEMENT%%=*}" "${TMP_ELEMENT#*=}" || return $?
        else
            write_key_value_to_file "${TMP_FILE}" "${TMP_ELEMENT}" "true" || return $?
        fi
    done
}

###------------------------------------------------------------------------------------------------
## FUNCTION: file_update_owner()
## - Changes the owner of a file or directory
## - Arguments
##   - $1: File or directory
##   - $2: Owner (example: apache:apache)
function file_update_owner() {
    local TMP_FILE="${1:-}"
    local TMP_OWNER="${2:-}"
    local RETURNVAL=0
    log "- Updating ownership (File::${TMP_FILE} / Owner::${TMP_OWNER})"
    if [[ ! -e "${TMP_FILE}" ]]; then
        log_error "- Could not update ownership, file not found [File::${TMP_FILE}]"
        return $E_OBJECT_NOT_FOUND
    fi
    command chown "${TMP_OWNER}" "${TMP_FILE}" || RETURNVAL=$?
    if [ ${RETURNVAL} -ne 0 ]; then
        log_error "- Could not update ownership, chown returned ${RETURNVAL}"
        return $E_CHOWN_FAILURE
    fi
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: file_update_permissions()
## - Changes the permissions of a file or directory
## - Arguments
##   - $1: File or directory
##   - $2: Permissions (example: 0400)
function file_update_permissions() {
    local TMP_FILE="${1:-}"
    local TMP_PERMISSIONS="${2:-}"
    local RETURNVAL=0
    log "- Updating permissions (File::${TMP_FILE} / Permissions::${TMP_PERMISSIONS})"
    if [[ ! -e "${TMP_FILE}" ]]; then
        log_error "- Could not update permissions, file not found [File::${TMP_FILE}]"
        return $E_OBJECT_NOT_FOUND
    fi
    command chmod "${TMP_PERMISSIONS}" "${TMP_FILE}" || RETURNVAL=$?
    if [ ${RETURNVAL} -ne 0 ]; then
        log_error "- Could not update permissions, chmod returned ${RETURNVAL}"
        return $E_CHMOD_FAILURE
    fi
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: filesize_bytes_to_human_readable()
## - Converts a size in bytes to a human readable format (example: 1.50 MB)
## - Arguments
##   - $1: Size in bytes (integer)
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function filesize_bytes_to_human_readable() {
    local __BYTES="${1:-}"
    local __STRING=""
    if ! is_int "${__BYTES}"; then
        log_error "Filesize to Human Readable: invalid size [${__BYTES}]"
        return $E_BAD_ARGS
    fi
    __STRING="$(command awk -v fsize="${__BYTES}" 'BEGIN { hum[1024^4]="TB"; hum[1024^3]="GB"; hum[1024^2]="MB"; hum[1024]="KB"; for (x=1024^4; x>=1024; x/=1024) { if (fsize>=x) { printf "%.2f %s", fsize/x, hum[x]; exit } } printf "%d bytes", fsize }')"
    _bamf_return "${2:-}" "${__STRING}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: function_exists()
## - Returns true if a function with the specified name exists
## - Arguments
##   - $1: Function name
function function_exists() {
    declare -F "${1:-}" >/dev/null 2>&1
}

###------------------------------------------------------------------------------------------------
## FUNCTION: generate_temp_directory()
## - Creates a temporary directory, which cleanup removes when the script exits
## - Exits the script (E_OBJECT_FAILED_TO_CREATE) if the directory cannot be created
## - Arguments
##   - $1: Variable name to pass the directory name to
##   - $2: Purpose string (optional, shown in the log on failure)
##   - $3: Base directory (optional, defaults to DIRECTORY_TEMPORARY)
function generate_temp_directory() {
    local __PURPOSE=""
    local __BASE_DIRECTORY="${3:-${DIRECTORY_TEMPORARY}}"
    local __DIRECTORY=""
    [[ -n "${2:-}" ]] && __PURPOSE=" (${2})"
    if [[ ! -d "${__BASE_DIRECTORY}" ]]; then
        log_warning "- Generate Temp Directory: Specified base temp directory does not exist, defaulting to [${DIRECTORY_TEMPORARY}]"
        __BASE_DIRECTORY="${DIRECTORY_TEMPORARY}"
    fi
    if ! __DIRECTORY="$(command mktemp -q -d "${__BASE_DIRECTORY}/bamf.XXXXXXXXXX")"; then
        log_error "- Generate Temp Directory: Failed to create temporary directory${__PURPOSE} (Base_Location::${__BASE_DIRECTORY})"
        exit_logic $E_OBJECT_FAILED_TO_CREATE "temporary directory"
    fi
    TEMPORARY_DIRECTORIES+=( "${__DIRECTORY}" )
    _bamf_return "${1:-}" "${__DIRECTORY}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: generate_temp_file()
## - Creates a temporary file (mode 0600), which cleanup removes when the script exits
## - Exits the script (E_OBJECT_FAILED_TO_CREATE) if the file cannot be created
## - Arguments
##   - $1: Variable name to pass the file name to
##   - $2: Purpose string (optional, shown in the log on failure)
##   - $3: Directory (optional, defaults to DIRECTORY_TEMPORARY)
function generate_temp_file() {
    local __PURPOSE=""
    local __BASE_DIRECTORY="${3:-${DIRECTORY_TEMPORARY}}"
    local __FILE=""
    [[ -n "${2:-}" ]] && __PURPOSE=" (${2})"
    if [[ ! -d "${__BASE_DIRECTORY}" ]]; then
        log_warning "- Generate Temp File: Specified temp directory does not exist, defaulting to [${DIRECTORY_TEMPORARY}]"
        __BASE_DIRECTORY="${DIRECTORY_TEMPORARY}"
    fi
    if ! __FILE="$(command mktemp -q "${__BASE_DIRECTORY}/bamf.XXXXXXXXXX")"; then
        log_error "- Generate Temp File: Failed to create temporary file${__PURPOSE} (Base_Location::${__BASE_DIRECTORY})"
        exit_logic $E_OBJECT_FAILED_TO_CREATE "temporary file"
    fi
    TEMPORARY_FILES+=( "${__FILE}" )
    _bamf_return "${1:-}" "${__FILE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: generate_temp_file_with_content()
## - Creates a temporary file (mode 0600) holding the content exactly as given (no newline added)
## - Use it to pass sensitive values to commands as file:// or fileb:// arguments, so they do not
##   appear in the process list
## - Arguments
##   - $1: Variable name to pass the file name to
##   - $2: Content
##   - $3: Purpose string (optional)
function generate_temp_file_with_content() {
    local __CONTENT_FILE=""
    generate_temp_file __CONTENT_FILE "${3:-}"
    command chmod 600 "${__CONTENT_FILE}"
    printf '%s' "${2:-}" > "${__CONTENT_FILE}"
    _bamf_return "${1:-}" "${__CONTENT_FILE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: generate_uuid()
## - Creates a random UUID, lowercase and without hyphens
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Length (optional, truncates the 32 character string)
function generate_uuid() {
    local __UUID=""
    local __LENGTH="${2:-}"
    if command -v uuidgen >/dev/null 2>&1; then
        __UUID="$(command uuidgen)" || return $E_MISSING_DEPENDENCIES
    elif [[ -r /proc/sys/kernel/random/uuid ]]; then
        read -r __UUID < /proc/sys/kernel/random/uuid
    else
        log_error "Generate UUID: neither uuidgen nor /proc/sys/kernel/random/uuid is available"
        return $E_MISSING_DEPENDENCIES
    fi
    __UUID="${__UUID//-/}"
    __UUID="${__UUID,,}"
    if is_int "${__LENGTH}" && (( __LENGTH > 0 )); then
        __UUID="${__UUID:0:${__LENGTH}}"
    fi
    _bamf_return "${1:-}" "${__UUID}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: is_empty()
## - Returns true if the value is empty
## - With strict checking (the default), the values 0 and 0.0 also count as empty
## - Arguments
##   - $1: Value to check
##   - $2: Strict checking, yes or no (optional, defaults to yes)
function is_empty() {
    case "${2:-yes}" in
        y|yes) [[ -z "${1:-}" || "${1:-}" == "0" || "${1:-}" == "0.0" ]];;
        *)     [[ -z "${1:-}" ]];;
    esac
}

###------------------------------------------------------------------------------------------------
## FUNCTION: is_int()
## - Returns true if the value is an integer (an optional leading minus sign, then digits)
## - Arguments
##   - $1: Value to check
function is_int() {
    [[ "${1:-}" =~ ^-?[0-9]+$ ]]
}

###------------------------------------------------------------------------------------------------
## FUNCTION: is_running_as_root()
## - Returns true if the script is running as root
function is_running_as_root() {
    [[ "${EUID}" -eq 0 ]]
}

###------------------------------------------------------------------------------------------------
## FUNCTION: is_server_up()
## - Returns true if the server answers ping (3 attempts)
## - Arguments
##   - $1: Server address
function is_server_up() {
    command ping -c 3 -- "${1:-}" >/dev/null 2>&1
}

###------------------------------------------------------------------------------------------------
## FUNCTION: json_escape()
## - Prints a string escaped for use inside a JSON string value (the surrounding quotes are not added)
## - Escapes backslashes, double quotes and control characters, so any value can be placed in JSON
##   built by a script: printf '{"name": "%s"}' "$(json_escape "${NAME}")"
## - Arguments
##   - $1: String
function json_escape() {
    local TMP_STRING="${1:-}"
    local TMP_ESCAPED=""
    local TMP_CHARACTER=""
    local TMP_INDEX=0
    TMP_STRING="${TMP_STRING//\\/\\\\}"
    TMP_STRING="${TMP_STRING//\"/\\\"}"
    TMP_STRING="${TMP_STRING//$'\n'/\\n}"
    TMP_STRING="${TMP_STRING//$'\r'/\\r}"
    TMP_STRING="${TMP_STRING//$'\t'/\\t}"
    TMP_STRING="${TMP_STRING//$'\b'/\\b}"
    TMP_STRING="${TMP_STRING//$'\f'/\\f}"
    # any other control character becomes \u00XX (rare, so checked before the slower loop)
    if [[ "${TMP_STRING}" == *[$'\001'-$'\037']* ]]; then
        for (( TMP_INDEX=0; TMP_INDEX<${#TMP_STRING}; TMP_INDEX++ )); do
            TMP_CHARACTER="${TMP_STRING:${TMP_INDEX}:1}"
            if [[ "${TMP_CHARACTER}" == [$'\001'-$'\037'] ]]; then
                printf -v TMP_CHARACTER '\\u%04x' "'${TMP_CHARACTER}"
            fi
            TMP_ESCAPED+="${TMP_CHARACTER}"
        done
        TMP_STRING="${TMP_ESCAPED}"
    fi
    printf '%s' "${TMP_STRING}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: json_extract_from_string()
## - Extracts the value of a key from a simple JSON string
## - Works best on flat JSON objects that do not repeat key names; for anything more complex,
##   use a JSON tool such as jq. This exists so simple cases do not need jq installed.
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Key name
##   - $3: JSON string
function json_extract_from_string() {
    local __KEY="${2:-}"
    local __JSON="${3:-}"
    local __STRING_REGEX='"([^"\]|\\.)*"'
    local __NUMBER_REGEX='-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?'
    local __PAIR_REGEX="\"${__KEY}\"[[:space:]]*:[[:space:]]*(${__STRING_REGEX}|${__NUMBER_REGEX}|true|false|null)"
    local __VALUE=""
    if [[ "${__JSON}" =~ ${__PAIR_REGEX} ]]; then
        __VALUE="${BASH_REMATCH[1]}"
        if [[ "${__VALUE}" == \"*\" ]]; then
            __VALUE="${__VALUE:1:${#__VALUE}-2}"
        fi
    fi
    _bamf_return "${1:-}" "${__VALUE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: json_safe()
## - Prints the string with characters that would break a JSON string value replaced
## - Newlines, single and double quotes become underscores; surrounding spaces are removed;
##   non-ASCII characters are removed when iconv is available
## - Arguments
##   - $1: String
function json_safe() {
    local TMP_STRING=""
    TMP_STRING="$(trim "${1:-}")"
    TMP_STRING="${TMP_STRING//$'\n'/_}"
    TMP_STRING="${TMP_STRING//\'/_}"
    TMP_STRING="${TMP_STRING//\"/_}"
    if command -v iconv >/dev/null 2>&1; then
        printf '%s' "${TMP_STRING}" | command iconv -c -f utf-8 -t ascii
    else
        printf '%s' "${TMP_STRING}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: line_break()
## - Prints a line that spans the full width of the terminal
## - Serves no purpose other than to be a pretty, pretty princess
## - The width is looked up on first use and cached for the life of the script
function line_break() {
    if [[ -z "${LOG_LINE_BREAK}" ]]; then
        local TMP_SCREEN_WIDTH=""
        # tput fails without a terminal type (cron, CI, containers); the default width is used then
        TMP_SCREEN_WIDTH="$(command tput cols 2>/dev/null)" || TMP_SCREEN_WIDTH=""
        if [[ ! "${TMP_SCREEN_WIDTH}" =~ ^[1-9][0-9]*$ ]]; then
            TMP_SCREEN_WIDTH=${DEFAULT_SCREEN_WIDTH}
        fi
        printf -v LOG_LINE_BREAK '%*s' "${TMP_SCREEN_WIDTH}" ''
        LOG_LINE_BREAK="${LOG_LINE_BREAK// /-}"
    fi
    printf '%s\n' "${LOG_LINE_BREAK}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: load_array_key_values_from_file()
## - For each key in the array, loads its value from a [key]=value file into an uppercase variable
## - Example: keys ('region') with prepend string APP set APP_REGION
## - Arguments
##   - $1: Array (pass this way: "KEYS[@]")
##   - $2: File to load from
##   - $3: Prepend String (optional, an underscore is added automatically)
##   - $4: Parse String (optional, see load_key_value_from_file)
function load_array_key_values_from_file() {
    local -a __LA_KEYS=("${!1}")
    local __LA_PREPEND="${3:-}"
    local __LA_KEY=""
    local __LA_VALUE=""
    [[ -n "${__LA_PREPEND}" ]] && __LA_PREPEND="${__LA_PREPEND}_"
    for __LA_KEY in "${__LA_KEYS[@]+"${__LA_KEYS[@]}"}"; do
        __LA_VALUE=""
        load_key_value_from_file __LA_VALUE "${__LA_KEY}" "${2:-}" "${4:-}"
        if [[ -n "${__LA_VALUE}" ]]; then
            _bamf_set_variable "$(to_upper "${__LA_PREPEND}${__LA_KEY//./_}")" "${__LA_VALUE}" || return $?
        fi
    done
}

###------------------------------------------------------------------------------------------------
## FUNCTION: load_array_key_values_from_yaml_file()
## - For each key in the array, loads its value from a YAML file into an uppercase variable
## - See return_yaml_string for the (simple) YAML that is understood
## - Arguments
##   - $1: Array (pass this way: "KEYS[@]")
##   - $2: File to load from
##   - $3: Prepend String (optional, an underscore is added automatically)
function load_array_key_values_from_yaml_file() {
    local -a __LA_KEYS=("${!1}")
    local __LA_PREPEND="${3:-}"
    local __LA_KEY=""
    local __LA_VALUE=""
    [[ -n "${__LA_PREPEND}" ]] && __LA_PREPEND="${__LA_PREPEND}_"
    for __LA_KEY in "${__LA_KEYS[@]+"${__LA_KEYS[@]}"}"; do
        __LA_VALUE=""
        return_yaml_string __LA_VALUE "${__LA_KEY}" "${2:-}"
        _bamf_set_variable "$(to_upper "${__LA_PREPEND}${__LA_KEY//./_}")" "${__LA_VALUE}" || return $?
    done
}

###------------------------------------------------------------------------------------------------
## FUNCTION: load_array_properties_from_file()
## - For each key in the array, loads its value from a properties file into an uppercase variable
## - Example: keys ('db.host') with prepend string APP set APP_DB_HOST
## - Arguments
##   - $1: Array (pass this way: "KEYS[@]")
##   - $2: File to load from
##   - $3: Prepend String (optional, an underscore is added automatically)
function load_array_properties_from_file() {
    local -a __LA_KEYS=("${!1}")
    local __LA_PREPEND="${3:-}"
    local __LA_KEY=""
    local __LA_VALUE=""
    [[ -n "${__LA_PREPEND}" ]] && __LA_PREPEND="${__LA_PREPEND}_"
    for __LA_KEY in "${__LA_KEYS[@]+"${__LA_KEYS[@]}"}"; do
        __LA_VALUE=""
        load_property_from_file __LA_VALUE "${__LA_KEY}" "${2:-}"
        if [[ -n "${__LA_VALUE}" ]]; then
            _bamf_set_variable "$(to_upper "${__LA_PREPEND}${__LA_KEY//./_}")" "${__LA_VALUE}" || return $?
        fi
    done
}

###------------------------------------------------------------------------------------------------
## FUNCTION: load_key_value_from_file()
## - Loads a value from a file of [key]=value lines (written by write_key_value_to_file)
## - With a parse string, reads [parse][key]=value lines instead
## - The key is matched exactly; when a key appears more than once, the last line wins
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Key name
##   - $3: File
##   - $4: Parse String (optional)
function load_key_value_from_file() {
    local __KEY="${2:-}"
    local __FILE="${3:-}"
    local __PREFIX=""
    local __LINE=""
    local __VALUE=""
    if [[ -n "${__KEY}" ]] && [[ -r "${__FILE}" ]]; then
        __PREFIX="[${__KEY}]="
        [[ -n "${4:-}" ]] && __PREFIX="[${4}]${__PREFIX}"
        while IFS= read -r __LINE || [[ -n "${__LINE}" ]]; do
            __LINE="${__LINE%$'\r'}"
            if [[ "${__LINE}" == "${__PREFIX}"* ]]; then
                __VALUE="${__LINE#"${__PREFIX}"}"
            fi
        done < "${__FILE}"
    fi
    _bamf_return "${1:-}" "$(trim "${__VALUE}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: load_property_from_file()
## - Loads a value from a properties or dotenv style file (key=value lines)
## - Keys match without regard to case, and "." and "_" are interchangeable (db.host matches DB_HOST)
## - Spaces around "=" and a leading "export " are allowed; lines starting with # or ! are comments
## - Matching single or double quotes around the value are removed; when a key appears more than
##   once, the last line wins
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Key name
##   - $3: File
function load_property_from_file() {
    local __KEY="${2:-}"
    local __FILE="${3:-}"
    local __LINE=""
    local __LINE_KEY=""
    local __VALUE=""
    if [[ -n "${__KEY}" ]] && [[ -r "${__FILE}" ]]; then
        __KEY="${__KEY,,}"
        __KEY="${__KEY//./_}"
        while IFS= read -r __LINE || [[ -n "${__LINE}" ]]; do
            __LINE="$(trim "${__LINE%$'\r'}")"
            [[ -z "${__LINE}" || "${__LINE}" == \#* || "${__LINE}" == \!* || "${__LINE}" != *=* ]] && continue
            __LINE_KEY="$(trim "${__LINE%%=*}")"
            __LINE_KEY="${__LINE_KEY#export }"
            __LINE_KEY="$(trim "${__LINE_KEY}")"
            __LINE_KEY="${__LINE_KEY,,}"
            if [[ "${__LINE_KEY//./_}" == "${__KEY}" ]]; then
                __VALUE="$(trim "${__LINE#*=}")"
            fi
        done < "${__FILE}"
    fi
    _bamf_return "${1:-}" "$(_bamf_unquote "${__VALUE}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log()
## - Prints a log line: [identity] message, with the name padded with dots to the width of the name
##   column, so messages line up (see LOG_IDENTITY_WIDTH); a timestamp comes first when ENABLE_TIMESTAMP is on
## - Arguments
##   - $1: Message
##   - $2: Message type (optional): NOTC notice, SUCC success, WARN warning, ERRO error, HIGH highlight,
##         SSTR script start, SFIN script finished, SNFO script info, FILE file content, SCLI command
##         line, QUIE quiet (not printed)
##   - $3: Message color override (optional)
function log() {
    local TMP_LOG_STRING="${1:-}"
    local TMP_LOG_TYPE="${2:-}"
    local TMP_LOG_COLOR_CONTENT="${3:-}"
    local TMP_LOG_COLOR=""
    local TMP_LOG_PREPEND=""
    local TMP_LOG_DATE=""
    TMP_LOG_TYPE="${TMP_LOG_TYPE^^}"

    case "${TMP_LOG_TYPE}" in
        SINT|SHLT|QUIE) return 0;;
        NOTC) TMP_LOG_COLOR="${LOG_COLOR_NOTICE}";;
        SUCC) TMP_LOG_COLOR="${LOG_COLOR_SUCCESS}";           TMP_LOG_PREPEND="SUCCESS: ";;
        WARN) TMP_LOG_COLOR="${LOG_COLOR_WARNING}";           TMP_LOG_PREPEND="WARNING: ";;
        ERRO) TMP_LOG_COLOR="${LOG_COLOR_ERROR}";             TMP_LOG_PREPEND="ERROR: ";;
        HIGH) TMP_LOG_COLOR="${LOG_COLOR_HIGHLIGHT}";;
        SSTR) TMP_LOG_COLOR="${LOG_COLOR_MESSAGE_IMPORTANT}"; TMP_LOG_PREPEND="SCRIPT_START: ";;
        SFIN) TMP_LOG_COLOR="${LOG_COLOR_MESSAGE_IMPORTANT}"; TMP_LOG_PREPEND="SCRIPT_FINISHED: ";;
        SNFO) TMP_LOG_COLOR="${LOG_COLOR_MESSAGE_IMPORTANT}"; TMP_LOG_PREPEND="SCRIPT_INFO: ";;
        FILE) TMP_LOG_COLOR="${LOG_COLOR_FILE}";              TMP_LOG_PREPEND="FILE_CONTENT: ";;
        SCLI) TMP_LOG_COLOR="${LOG_COLOR_CLI}";               TMP_LOG_PREPEND="CLI: ";;
        *)    TMP_LOG_COLOR="${LOG_COLOR_MESSAGE}";;
    esac

    if option_enabled ENABLE_TIMESTAMP; then
        printf -v TMP_LOG_DATE "%(${LOG_DISPLAY_DATE_FORMAT})T" -1
    fi

    if option_enabled ENABLE_COLOR; then
        if [[ -z "${TMP_LOG_COLOR_CONTENT}" ]]; then
            TMP_LOG_COLOR_CONTENT="${TMP_LOG_COLOR}"
        elif [[ "${TMP_LOG_COLOR_CONTENT}" == *'\e'* ]]; then
            printf -v TMP_LOG_COLOR_CONTENT '%b' "${TMP_LOG_COLOR_CONTENT}"
        fi
        if [[ -n "${TMP_LOG_DATE}" ]]; then
            TMP_LOG_DATE="${LOG_COLOR_DATE}${TMP_LOG_DATE}${LOG_COLOR_MESSAGE} "
        fi
        printf '%s%s %s%s%s%s%s%s\n' "${TMP_LOG_DATE}" "${LOG_PREFIX_COLOR}" "${TMP_LOG_COLOR}" "${TMP_LOG_PREPEND}" "${LOG_COLOR_MESSAGE}" "${TMP_LOG_COLOR_CONTENT}" "${TMP_LOG_STRING}" "${LOG_COLOR_MESSAGE}"
    else
        printf '%s%s %s%s\n' "${TMP_LOG_DATE:+${TMP_LOG_DATE} }" "${LOG_PREFIX_PLAIN}" "${TMP_LOG_PREPEND}" "${TMP_LOG_STRING}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_add_from_file()
## - Prints the content of a file to the log, with line numbers
## - Arguments
##   - $1: File
##   - $2: Description of the contents (optional)
##   - $3: Maximum number of lines to print (optional, defaults to 1000)
function log_add_from_file() {
    local ADD_TO_LOG_FILE="${1:-}"
    local ADD_TO_LOG_DESC=""
    local ADD_TO_LOG_LINE_LIMIT="${3:-}"
    local ADD_TO_LOG_LINE_LIMIT_STRING=""
    local TMP_FORMAT='[%05d] %s\n'
    local COUNTER=0
    local LINE=""
    if [[ -z "${ADD_TO_LOG_FILE}" ]]; then
        log_warning "Log Add from File Content: Log file not specified"
        return 1
    fi
    [[ -n "${2:-}" ]] && ADD_TO_LOG_DESC=" / Description::${2}"
    if ! is_int "${ADD_TO_LOG_LINE_LIMIT}" || (( ADD_TO_LOG_LINE_LIMIT <= 0 )); then
        ADD_TO_LOG_LINE_LIMIT="${DEFAULT_ADD_TO_LOG_LINE_LIMIT}"
    else
        ADD_TO_LOG_LINE_LIMIT_STRING=" / Content_Line_Limit::${ADD_TO_LOG_LINE_LIMIT}"
    fi

    log "(File::[${ADD_TO_LOG_FILE}]${ADD_TO_LOG_DESC}${ADD_TO_LOG_LINE_LIMIT_STRING})" FILE
    # a missing file is noted in the log, but does not stop the script
    if [[ ! -f "${ADD_TO_LOG_FILE}" ]]; then
        log "ERROR: Could not load file contents [${ADD_TO_LOG_FILE}]" FILE "${LOG_COLOR_ERROR}"
        return 0
    fi
    if option_enabled ENABLE_COLOR; then
        TMP_FORMAT="${LOG_COLOR_FILE_HEADER}[%05d]${LOG_COLOR_MESSAGE} ${LOG_COLOR_FILE}%s${LOG_COLOR_MESSAGE}\n"
    fi
    log_file_content 0
    while IFS= read -r LINE || [[ -n "${LINE}" ]]; do
        COUNTER=$((COUNTER+1))
        if (( COUNTER > ADD_TO_LOG_LINE_LIMIT )); then
            # shellcheck disable=SC2059 # the format holds only color bytes and conversions
            printf "${TMP_FORMAT}" "${COUNTER}" "----- TRUNCATED AT LINE [$((COUNTER-1))] -----"
            break
        fi
        # shellcheck disable=SC2059 # the format holds only color bytes and conversions
        printf "${TMP_FORMAT}" "${COUNTER}" "${LINE}"
    done < "${ADD_TO_LOG_FILE}"
    log_file_content 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_error()
## - Prints an error log line (prefixed with "ERROR: ")
## - Arguments
##   - $1: Message
##   - $2: Message color override (optional)
function log_error() {
    log "${1:-}" ERRO "${2:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_file_content()
## - Prints a single line of file content (line number 0 prints a separator line)
## - Arguments
##   - $1: Line number
##   - $2: Line content
function log_file_content() {
    local TMP_LINE_NUMBER="${1:-0}"
    local TMP_LOG_STRING="${2:-}"
    if [[ -z "${LOG_LINE_BREAK}" ]]; then
        line_break >/dev/null
    fi
    if [[ "${TMP_LINE_NUMBER}" == "0" ]]; then
        if option_enabled ENABLE_COLOR; then
            printf '%s%s%s\n' "${LOG_COLOR_FILE_HEADER}" "${LOG_LINE_BREAK}" "${LOG_COLOR_MESSAGE}"
        else
            printf '%s\n' "${LOG_LINE_BREAK}"
        fi
    elif option_enabled ENABLE_COLOR; then
        printf '%s[%05d]%s %s%s%s\n' "${LOG_COLOR_FILE_HEADER}" "${TMP_LINE_NUMBER}" "${LOG_COLOR_MESSAGE}" "${LOG_COLOR_FILE}" "${TMP_LOG_STRING}" "${LOG_COLOR_MESSAGE}"
    else
        printf '[%05d] %s\n' "${TMP_LINE_NUMBER}" "${TMP_LOG_STRING}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_highlight()
## - Prints a highlighted log line: [[ message ]]
## - Arguments
##   - $1: Message
function log_highlight() {
    log "[[ ${1:-} ]]" HIGH
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_load_color_child()
## - Loads the color palette for scripts run by another script (see exec_script)
function log_load_color_child() {
    _log_set_color LOG_COLOR_DATE "${COLOR_CYAN}"
    _log_set_color LOG_COLOR_IDENT "${COLOR_GREEN}"
    _log_set_color LOG_COLOR_MESSAGE_IMPORTANT "${COLOR_MAGENTA}"
    _log_set_color LOG_COLOR_MESSAGE "${COLOR_LIGHT_GRAY}"
    _log_set_color LOG_COLOR_NOTICE "${COLOR_CYAN}"
    _log_set_color LOG_COLOR_WARNING "${COLOR_LIGHT_YELLOW}"
    _log_set_color LOG_COLOR_ERROR "${COLOR_LIGHT_RED}"
    _log_set_color LOG_COLOR_SUCCESS "${COLOR_GREEN}"
    _log_set_color LOG_COLOR_FILE_HEADER "${COLOR_DARK_GRAY}"
    _log_set_color LOG_COLOR_FILE "${COLOR_CYAN}"
    _log_set_color LOG_COLOR_HIGHLIGHT "${COLOR_YELLOW}"
    _log_set_color LOG_COLOR_CLI "${COLOR_LIGHT_CYAN}"
    _log_set_color LOG_COLOR_DETAILS "${COLOR_DARK_GRAY}"
    _log_build_prefix
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_load_color_parent()
## - Loads the color palette for scripts run directly
function log_load_color_parent() {
    _log_set_color LOG_COLOR_DATE "${COLOR_LIGHT_CYAN}"
    _log_set_color LOG_COLOR_IDENT "${COLOR_LIGHT_GREEN}"
    _log_set_color LOG_COLOR_MESSAGE_IMPORTANT "${COLOR_LIGHT_MAGENTA}"
    _log_set_color LOG_COLOR_MESSAGE "${COLOR_WHITE}"
    _log_set_color LOG_COLOR_NOTICE "${COLOR_LIGHT_CYAN}"
    _log_set_color LOG_COLOR_WARNING "${COLOR_YELLOW}"
    _log_set_color LOG_COLOR_ERROR "${COLOR_RED}"
    _log_set_color LOG_COLOR_SUCCESS "${COLOR_LIGHT_GREEN}"
    _log_set_color LOG_COLOR_FILE_HEADER "${COLOR_DARK_GRAY}"
    _log_set_color LOG_COLOR_FILE "${COLOR_LIGHT_CYAN}"
    _log_set_color LOG_COLOR_HIGHLIGHT "${COLOR_LIGHT_YELLOW}"
    _log_set_color LOG_COLOR_CLI "${COLOR_LIGHT_CYAN}"
    _log_set_color LOG_COLOR_DETAILS "${COLOR_DARK_GRAY}"
    _log_build_prefix
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_notice()
## - Prints a notice log line
## - Arguments
##   - $1: Message
##   - $2: Message color override (optional)
function log_notice() {
    log "${1:-}" NOTC "${2:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_quiet()
## - Prints nothing (a placeholder for messages that are only useful while debugging)
## - Arguments
##   - $1: Message
function log_quiet() {
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_status()
## - Prints a status log line: [identity] [PASS] message
## - PASS and SUCCESS are shown in the success color, FAIL and ERROR in the error color, anything
##   else in the warning color
## - Arguments
##   - $1: Status (example: PASS)
##   - $2: Message
##   - $3: Width to pad the [STATUS] to with dots, so statuses of different lengths line up
##         (optional, defaults to no padding; 9 fits [SUCCESS] and [WARNING])
##   - $4: Message color override (optional)
function log_status() {
    local TMP_STATUS="${1:-}"
    local TMP_MESSAGE="${2:-}"
    local TMP_WIDTH="${3:-0}"
    local TMP_MESSAGE_COLOR="${4:-}"
    local TMP_STATUS_COLOR=""
    local TMP_DOTS=""
    TMP_STATUS="${TMP_STATUS^^}"
    is_int "${TMP_WIDTH}" || TMP_WIDTH=0
    case "${TMP_STATUS}" in
        PASS|SUCCESS) TMP_STATUS_COLOR="${LOG_COLOR_SUCCESS}";;
        FAIL|ERROR)   TMP_STATUS_COLOR="${LOG_COLOR_ERROR}";;
        *)            TMP_STATUS_COLOR="${LOG_COLOR_WARNING}";;
    esac
    if (( TMP_WIDTH > ${#TMP_STATUS} + 2 )); then
        TMP_DOTS="${LOG_PAD:0:$(( TMP_WIDTH - ${#TMP_STATUS} - 2 ))}"
    fi
    if option_enabled ENABLE_COLOR; then
        if [[ -z "${TMP_MESSAGE_COLOR}" ]]; then
            TMP_MESSAGE_COLOR="${LOG_COLOR_MESSAGE}"
        elif [[ "${TMP_MESSAGE_COLOR}" == *'\e'* ]]; then
            printf -v TMP_MESSAGE_COLOR '%b' "${TMP_MESSAGE_COLOR}"
        fi
        log "${LOG_COLOR_DETAILS}[${TMP_STATUS_COLOR}${TMP_STATUS}${LOG_COLOR_DETAILS}]${TMP_DOTS} ${TMP_MESSAGE_COLOR}${TMP_MESSAGE}"
    else
        log "[${TMP_STATUS}]${TMP_DOTS} ${TMP_MESSAGE}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_success()
## - Prints a success log line (prefixed with "SUCCESS: ")
## - Arguments
##   - $1: Message
##   - $2: Message color override (optional)
function log_success() {
    log "${1:-}" SUCC "${2:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: log_warning()
## - Prints a warning log line (prefixed with "WARNING: ")
## - Arguments
##   - $1: Message
##   - $2: Message color override (optional)
function log_warning() {
    log "${1:-}" WARN "${2:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: lookup_exit_code()
## - Prints the name and description of an exit code (example: E_BAD_ARGS / Bad or missing arguments)
## - Prints UNKNOWN for a code that has not been defined with define_exit_code
## - Arguments
##   - $1: Exit code
function lookup_exit_code() {
    local TMP_CODE="${1:-}"
    local TMP_NAME=""
    if [[ "${TMP_CODE}" =~ ^[0-9]{1,3}$ ]]; then
        TMP_CODE=$((10#${TMP_CODE}))
        TMP_NAME="${BAMF_EXIT_CODE_NAMES[${TMP_CODE}]:-}"
    fi
    if [[ -z "${TMP_NAME}" ]]; then
        printf '%s\n' "UNKNOWN"
    elif [[ -z "${BAMF_EXIT_CODE_DESCRIPTIONS[${TMP_CODE}]:-}" ]]; then
        printf '%s\n' "${TMP_NAME}"
    else
        printf '%s\n' "${TMP_NAME} / ${BAMF_EXIT_CODE_DESCRIPTIONS[${TMP_CODE}]}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: option_enabled()
## - Returns true if the variable with the specified name is set to y, yes, 1 or true (any case)
## - Usage: "if option_enabled VERBOSE; then" - NOTE: the argument is the variable name, not the
##   variable (no $)
## - Arguments
##   - $1: Variable name
function option_enabled() {
    [[ "${1:-}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]] || return 1
    local VAR_VALUE="${!1:-}"
    # surrounding whitespace is ignored
    VAR_VALUE="${VAR_VALUE#"${VAR_VALUE%%[![:space:]]*}"}"
    VAR_VALUE="${VAR_VALUE%"${VAR_VALUE##*[![:space:]]}"}"
    case "${VAR_VALUE,,}" in
        y|yes|1|true) return 0;;
    esac
    return 1
}

###------------------------------------------------------------------------------------------------
## FUNCTION: parse_yaml()
## - Prints a simple YAML file as name="value" lines, nested keys joined with underscores
##   (example: "db:\n  host: x" prints db_host="x")
## - Handles maps of scalars indented by two spaces; lists and multi-line values are not supported
## - Arguments
##   - $1: File to parse
function parse_yaml() {
    local TMP_FILE_TO_PARSE="${1:-}"
    local s='[[:space:]]*' w='[a-zA-Z0-9_]*' FS=$'\034'
    command sed -ne "s|^\(${s}\):|\1|" \
        -e "s|^\(${s}\)\(${w}\)${s}:${s}[\"']\(.*\)[\"']${s}\$|\1${FS}\2${FS}\3|p" \
        -e "s|^\(${s}\)\(${w}\)${s}:${s}\(.*\)${s}\$|\1${FS}\2${FS}\3|p" "${TMP_FILE_TO_PARSE}" |
    command awk -F"${FS}" '{
        indent = length($1)/2;
        vname[indent] = $2;
        for (i in vname) {if (i > indent) {delete vname[i]}}
        if (length($3) > 0) {
            vn=""; for (i=0; i<indent; i++) {vn=(vn)(vname[i])("_")}
            printf("%s%s=\"%s\"\n", vn, $2, $3);
        }
    }' 2>/dev/null
}

###------------------------------------------------------------------------------------------------
## FUNCTION: parse_yaml_to_file()
## - Writes the output of parse_yaml to a file
## - Arguments
##   - $1: File to parse
##   - $2: Output file
function parse_yaml_to_file() {
    parse_yaml "${1:-}" > "${2:-}" 2>/dev/null
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_file_last_modify_timestamp()
## - Returns the last modification time of a file (UNIX timestamp)
## - Returns E_OBJECT_NOT_FOUND (and an empty value) if the file does not exist
## - Arguments
##   - $1: File
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_file_last_modify_timestamp() {
    _bamf_file_attribute "${1:-}" "${2:-}" mtime
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_file_md5sum()
## - Returns the MD5 checksum of a file
## - Returns E_OBJECT_NOT_FOUND (and an empty value) if the file does not exist
## - Arguments
##   - $1: File
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_file_md5sum() {
    _bamf_file_attribute "${1:-}" "${2:-}" md5sum
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_file_mime()
## - Returns the MIME type of a file (requires the file command)
## - Returns E_OBJECT_NOT_FOUND (and an empty value) if the file does not exist
## - Arguments
##   - $1: File
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_file_mime() {
    _bamf_file_attribute "${1:-}" "${2:-}" mime
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_file_sha1sum()
## - Returns the SHA-1 checksum of a file
## - Returns E_OBJECT_NOT_FOUND (and an empty value) if the file does not exist
## - Arguments
##   - $1: File
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_file_sha1sum() {
    _bamf_file_attribute "${1:-}" "${2:-}" sha1sum
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_file_sha256sum()
## - Returns the SHA-256 checksum of a file
## - Returns E_OBJECT_NOT_FOUND (and an empty value) if the file does not exist
## - Arguments
##   - $1: File
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_file_sha256sum() {
    _bamf_file_attribute "${1:-}" "${2:-}" sha256sum
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_filesize_of_file()
## - Returns the size of a file in a human readable format (example: 1.50 MB)
## - Returns E_OBJECT_NOT_FOUND (and an empty value) if the file does not exist
## - Arguments
##   - $1: File
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_filesize_of_file() {
    local __FS_BYTES=""
    local __FS_HUMAN=""
    _bamf_file_attribute "${1:-}" __FS_BYTES size || { _bamf_return "${2:-}" ""; return $E_OBJECT_NOT_FOUND; }
    filesize_bytes_to_human_readable "${__FS_BYTES}" __FS_HUMAN
    _bamf_return "${2:-}" "${__FS_HUMAN}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_filesize_of_file_in_bytes()
## - Returns the size of a file in bytes
## - Returns E_OBJECT_NOT_FOUND (and an empty value) if the file does not exist
## - Arguments
##   - $1: File
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_filesize_of_file_in_bytes() {
    _bamf_file_attribute "${1:-}" "${2:-}" size
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_header_string()
## - Returns the value of a "# <identifier>: <value>" comment line from a file (the first one found)
## - Example: return_header_string version "${0}" prints the "# version:" line of the script
## - Arguments
##   - $1: Header identifier
##   - $2: File
##   - $3: Variable name to pass info to (optional, if not specified the value is printed)
function return_header_string() {
    _bamf_comment_value "# ${1:-}:" "${2:-}" "${3:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_hostname()
## - Returns the host name
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function return_hostname() {
    local __HOSTNAME="${HOSTNAME:-}"
    [[ -n "${__HOSTNAME}" ]] || __HOSTNAME="$(command uname -n)"
    _bamf_return "${1:-}" "${__HOSTNAME}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_parameter_string()
## - Returns the value of a "## <identifier>: <value>" comment line from a file (the first one found)
## - Arguments
##   - $1: Parameter identifier
##   - $2: File
##   - $3: Variable name to pass info to (optional, if not specified the value is printed)
function return_parameter_string() {
    _bamf_comment_value "## ${1:-}:" "${2:-}" "${3:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_script_purpose()
## - Prints the "# purpose:" header of a script ("unknown" if it has none)
## - A <file>.info file next to the script is read instead, when it exists
## - Arguments
##   - $1: File (normally "${0}")
function return_script_purpose() {
    _bamf_script_header purpose "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_script_version()
## - Prints the "# version:" header of a script ("unknown" if it has none)
## - A <file>.info file next to the script is read instead, when it exists
## - Arguments
##   - $1: File (normally "${0}")
function return_script_version() {
    _bamf_script_header version "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_variable_value()
## - Returns the value of the variable with the specified name, with runs of whitespace collapsed to
##   a single space, without evaluating code or expanding globs
## - Returns E_BAD_ARGS (and an empty value) if the name is not a valid variable name
## - Arguments
##   - $1: Variable name
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function return_variable_value() {
    local __RVV_NAME="${1:-}"
    local -a __RVV_WORDS=()
    if [[ ! "${__RVV_NAME}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        _bamf_return "${2:-}" ""
        return $E_BAD_ARGS
    fi
    # read -d '' returns 1 at the end of input, which is expected here (safe under set -e)
    read -r -d '' -a __RVV_WORDS <<< "${!__RVV_NAME:-}" || true
    local IFS=' '
    _bamf_return "${2:-}" "${__RVV_WORDS[*]+"${__RVV_WORDS[*]}"}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: return_yaml_string()
## - Returns the value of a "key: value" line from a YAML file (the first one found, at any indent)
## - Matching quotes around the value are removed. Nested keys are not distinguished; use
##   parse_yaml for those
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Key
##   - $3: File
function return_yaml_string() {
    local __KEY="${2:-}"
    local __FILE="${3:-}"
    local __LINE=""
    local __VALUE=""
    if [[ -n "${__KEY}" ]] && [[ -r "${__FILE}" ]]; then
        while IFS= read -r __LINE || [[ -n "${__LINE}" ]]; do
            __LINE="$(trim "${__LINE%$'\r'}")"
            __LINE="${__LINE#"${__LINE%%[!$'\t']*}"}"
            if [[ "${__LINE}" == "${__KEY}:"* ]]; then
                __VALUE="$(trim "${__LINE#"${__KEY}:"}")"
                break
            fi
        done < "${__FILE}"
    fi
    _bamf_return "${1:-}" "$(_bamf_unquote "${__VALUE}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: run_silently()
## - Runs a command or function with its output hidden; the output is printed only if it fails
## - Variables set by a function run this way are kept (it does not run in a subshell)
## - If the command ends the script (for example through exit_logic), its output is not shown
## - Returns the exit code of the command
## - Arguments
##   - $1+: Command and its arguments
function run_silently() {
    local __RS_OUTPUT=""
    local __RS_RETURNVAL=0
    generate_temp_file __RS_OUTPUT "run_silently output"
    "$@" >"${__RS_OUTPUT}" 2>&1 || __RS_RETURNVAL=$?
    if (( __RS_RETURNVAL != 0 )); then
        command cat "${__RS_OUTPUT}"
    fi
    command rm -f -- "${__RS_OUTPUT}"
    return ${__RS_RETURNVAL}
}

###------------------------------------------------------------------------------------------------
## FUNCTION: source_include()
## - Loads a file with source (once; a file that is already loaded is skipped)
## - A name without a slash loads a BAMF helper file: "aws" (or "bamf.aws.inc") loads bamf.aws.inc from
##   the helpers directory next to bamf.inc, or from beside bamf.inc when the files are kept together
## - Exits the script (E_OBJECT_NOT_FOUND) if the file does not exist
## - Helper files are sourced inside this function: they must declare their variables with
##   "declare -g" (a plain "declare" or "local" would be local to this function)
## - Arguments
##   - $1: File or helper name
function source_include() {
    local __INCLUDE_FILE="${1:-}"
    local __INCLUDE_NAME=""
    if [[ "${__INCLUDE_FILE}" != */* ]]; then
        __INCLUDE_NAME="${__INCLUDE_FILE}"
        [[ "${__INCLUDE_NAME}" == *.inc ]] || __INCLUDE_NAME="bamf.${__INCLUDE_NAME}.inc"
        if [[ -f "${BAMF_HELPER_DIRECTORY}/${__INCLUDE_NAME}" ]]; then
            __INCLUDE_FILE="${BAMF_HELPER_DIRECTORY}/${__INCLUDE_NAME}"
        elif [[ -f "${BAMF_DIRECTORY}/${__INCLUDE_NAME}" || "${__INCLUDE_FILE}" != *.inc ]]; then
            __INCLUDE_FILE="${BAMF_DIRECTORY}/${__INCLUDE_NAME}"
        fi
    fi
    if [[ ! -f "${__INCLUDE_FILE}" ]]; then
        log_error "Source Include: file not found [${__INCLUDE_FILE}]"
        exit_logic $E_OBJECT_NOT_FOUND "file not found"
    fi
    if does_array_contain_element "${__INCLUDE_FILE}" "${BAMF_LOADED_SOURCE_FILES[@]+"${BAMF_LOADED_SOURCE_FILES[@]}"}"; then
        return 0
    fi
    BAMF_LOADED_SOURCE_FILES+=( "${__INCLUDE_FILE}" )
    # shellcheck disable=SC1090 # the file is chosen at runtime
    source "${__INCLUDE_FILE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: start_logic()
## - Starts the script; call it once, after processing the arguments
## - Logs the start line (the command line, with SENSITIVE_ARGUMENTS masked)
## - Sets traps so the script always ends through exit_logic (signals, errors under set -e, or
##   reaching the end of the script)
## - Loads the files listed in REQUIRED_SOURCE_FILES, in order
## - Checks that the commands listed in REQUIRED_EXECUTABLES exist (exits with E_MISSING_DEPENDENCIES)
function start_logic() {
    _bamf_start yes
}

###------------------------------------------------------------------------------------------------
## FUNCTION: start_logic_quiet()
## - Same as start_logic, without the start log line
function start_logic_quiet() {
    _bamf_start no
}

###------------------------------------------------------------------------------------------------
## FUNCTION: sync_disks()
## - Flushes file system buffers (sync) and logs it
function sync_disks() {
    log "- Flushing file system buffers [sync]"
    command sync
}

###------------------------------------------------------------------------------------------------
## FUNCTION: time_elapsed()
## - Prints the time elapsed between two timestamps in microseconds (example: 1m 05.250s)
## - Arguments
##   - $1: Start time (microseconds since the epoch, from: date +%s%6N)
##   - $2: End time (optional, defaults to now)
function time_elapsed() {
    local TIME_START="${1:-0}"
    local TIME_END="${2:-}"
    local ELAPSED=0
    local TIME_MICROSECONDS=0
    local TIME_MILLISECONDS=0
    local TIME_SECONDS=0
    [[ -n "${TIME_END}" ]] || TIME_END="$(command date +%s%6N)"
    is_int "${TIME_START}" || TIME_START=0
    is_int "${TIME_END}" || TIME_END=0
    ELAPSED=$(( 10#${TIME_END#-} - 10#${TIME_START#-} ))
    (( ELAPSED < 0 )) && ELAPSED=0
    TIME_MICROSECONDS=$(( ELAPSED % 1000 ))
    ELAPSED=$(( ELAPSED / 1000 ))
    TIME_MILLISECONDS=$(( ELAPSED % 1000 ))
    (( TIME_MICROSECONDS > 500 )) && TIME_MILLISECONDS=$(( TIME_MILLISECONDS + 1 ))
    ELAPSED=$(( ELAPSED / 1000 ))
    TIME_SECONDS=$(( ELAPSED % 60 ))
    ELAPSED=$(( ELAPSED / 60 ))
    printf '%01dm %02d.%03ds\n' "${ELAPSED}" "${TIME_SECONDS}" "${TIME_MILLISECONDS}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: timestamp_to_human_readable()
## - Prints a UNIX timestamp as YYYY-MM-DD HH:MM:SS TZ
## - Only the first 10 digits are used, so timestamps with subseconds (date +%s%6N) work too
## - Arguments
##   - $1: UNIX timestamp
##   - $2: Force UTC, yes or no (optional, defaults to no)
function timestamp_to_human_readable() {
    local TMP_TIMESTAMP="${1:-}"
    local -a TMP_UTC=()
    TMP_TIMESTAMP="${TMP_TIMESTAMP:0:10}"
    if ! is_int "${TMP_TIMESTAMP}"; then
        log_error "Timestamp to Human Readable: invalid timestamp [${1:-}]"
        return $E_BAD_ARGS
    fi
    case "${2:-no}" in
        y|yes|Y|YES|1|true|TRUE) TMP_UTC=( --utc );;
    esac
    command date "${TMP_UTC[@]+"${TMP_UTC[@]}"}" --date "@${TMP_TIMESTAMP}" +'%Y-%m-%d %H:%M:%S %Z'
}

###------------------------------------------------------------------------------------------------
## FUNCTION: to_lower()
## - Prints a string in lowercase
## - Arguments
##   - $1: String
function to_lower() {
    printf '%s' "${1,,}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: to_upper()
## - Prints a string in uppercase
## - Arguments
##   - $1: String
function to_upper() {
    printf '%s' "${1^^}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: trim()
## - Prints a string with spaces removed from both ends (of each line, for a multi-line string)
## - Arguments
##   - $1: String
function trim() {
    local TMP_STRING="${1:-}"
    if [[ "${TMP_STRING}" == *$'\n'* ]]; then
        printf '%s' "${TMP_STRING}" | command sed -e 's/^ *//' -e 's/ *$//'
        return
    fi
    TMP_STRING="${TMP_STRING#"${TMP_STRING%%[! ]*}"}"
    TMP_STRING="${TMP_STRING%"${TMP_STRING##*[! ]}"}"
    printf '%s' "${TMP_STRING}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: uptime_greater_than()
## - Returns true if the system has been up for longer than the specified number of seconds (Linux)
## - Arguments
##   - $1: Seconds (optional, defaults to 300)
function uptime_greater_than() {
    local TMP_TARGET_SECONDS="${1:-300}"
    local TMP_UPTIME=""
    is_int "${TMP_TARGET_SECONDS}" || TMP_TARGET_SECONDS=300
    read -r TMP_UPTIME _ < /proc/uptime || return 1
    TMP_UPTIME="${TMP_UPTIME%%.*}"
    (( TMP_UPTIME > TMP_TARGET_SECONDS ))
}

###------------------------------------------------------------------------------------------------
## FUNCTION: usage()
## - Prints the help: usage banner, ARGUMENTS_DESCRIPTION entries and the common arguments
function usage() {
    local TMP_ARG=""
    usage_banner "${0}"
    printf '\nARGUMENTS:\n'
    if [ ${#ARGUMENTS_DESCRIPTION[@]} -ne 0 ]; then
        for TMP_ARG in "${ARGUMENTS_DESCRIPTION[@]}"; do
            printf '  %s\n' "${TMP_ARG}"
        done
        printf '\n'
    fi
    usage_normalized_arguments
    printf '\n'
}

###------------------------------------------------------------------------------------------------
## FUNCTION: usage_banner()
## - Prints the usage banner of a script (name, "# purpose:" and "# version:" headers)
## - Arguments
##   - $1: File (normally "${0}")
function usage_banner() {
    local FILE_TO_INSPECT="${1:-${0}}"
    printf '\nUSAGE:       %s <optional arguments>\n' "${FILE_TO_INSPECT##*/}"
    printf 'DESCRIPTION: %s\n' "$(return_script_purpose "${FILE_TO_INSPECT}")"
    printf 'VERSION:     %s\n' "$(return_script_version "${FILE_TO_INSPECT}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: usage_normalized_arguments()
## - Prints the arguments every BAMF script is expected to support
function usage_normalized_arguments() {
    printf '%s\n' "  -h : Show help message" "  -V : Show version number and exit"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: verbose_display_date()
## - Prints a log date timestamp truncated to YYYY-MM-DD HH:MM:SS
## - Arguments
##   - $1: Timestamp
function verbose_display_date() {
    printf '%s' "${1:0:19}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: verify_array_key_values()
## - Checks that the variable for each key in the array is set and not empty (logs a warning for each one
##   that is not)
## - Example: keys ('region') with prepend string APP check APP_REGION
## - Returns E_OBJECT_NOT_FOUND if any is missing
## - Arguments
##   - $1: Array (pass this way: "KEYS[@]")
##   - $2: Prepend String (optional, an underscore is added automatically)
function verify_array_key_values() {
    local -a __VA_KEYS=("${!1}")
    local __VA_PREPEND="${2:-}"
    local __VA_KEY=""
    local __VA_VALUE=""
    local __VA_RETURNVAL=0
    [[ -n "${__VA_PREPEND}" ]] && __VA_PREPEND="${__VA_PREPEND}_"
    for __VA_KEY in "${__VA_KEYS[@]+"${__VA_KEYS[@]}"}"; do
        __VA_VALUE=""
        return_variable_value "$(to_upper "${__VA_PREPEND}${__VA_KEY//./_}")" __VA_VALUE
        if [[ -z "${__VA_VALUE}" ]]; then
            if [[ -z "${__VA_PREPEND}" ]]; then
                log_warning "Verify Array Key Values: key is not set (Key::${__VA_KEY})"
            else
                log_warning "Verify Array Key Values: key is not set (Key::${__VA_KEY} / Prepend_Key_String::${__VA_PREPEND})"
            fi
            __VA_RETURNVAL=$E_OBJECT_NOT_FOUND
        fi
    done
    return ${__VA_RETURNVAL}
}

###------------------------------------------------------------------------------------------------
## FUNCTION: verify_dependencies()
## - Checks that every command in REQUIRED_EXECUTABLES can be found in the PATH
## - Exits the script (E_MISSING_DEPENDENCIES) if any is missing
function verify_dependencies() {
    local IS_ERROR=0
    local ELEMENT=""
    local -a TMP_CHECKED=()
    for ELEMENT in "${REQUIRED_EXECUTABLES[@]+"${REQUIRED_EXECUTABLES[@]}"}"; do
        if [[ -z "${ELEMENT}" ]] || does_array_contain_element "${ELEMENT}" "${TMP_CHECKED[@]+"${TMP_CHECKED[@]}"}"; then
            continue
        fi
        TMP_CHECKED+=( "${ELEMENT}" )
        if ! command -v "${ELEMENT}" >/dev/null 2>&1; then
            log_error "Verify dependencies: Could not locate required executable: ${ELEMENT}"
            IS_ERROR=1
        fi
    done
    if [[ ${IS_ERROR} -eq 1 ]]; then
        exit_logic $E_MISSING_DEPENDENCIES "one or more executables could not be found"
    fi
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: version_to_integer()
## - Converts a version string of up to four numbers (example: 1.2.3 or 2.4.10-1) to an integer
##   that sorts the same way, each part up to 999
## - Arguments
##   - $1: Version string
##   - $2: Variable name to pass info to (optional, if not specified the value is printed)
function version_to_integer() {
    local __VERSION="${1:-}"
    local -a __PARTS=()
    local __INDEX=0
    local __PART=""
    local __INTEGER=0
    IFS='.-' read -r -a __PARTS <<< "${__VERSION}"
    for (( __INDEX=0; __INDEX<4; __INDEX++ )); do
        __PART="${__PARTS[${__INDEX}]:-0}"
        if [[ ! "${__PART}" =~ ^[0-9]{1,3}$ ]]; then
            log_error "Version to Integer: Invalid version [string_version::${__VERSION} / part::${__PART}]"
            return 1
        fi
        __INTEGER=$(( __INTEGER * 1000 + 10#${__PART} ))
    done
    if (( ${#__PARTS[@]} > 4 )); then
        log_error "Version to Integer: Invalid version, more than four parts [string_version::${__VERSION}]"
        return 1
    fi
    _bamf_return "${2:-}" "${__INTEGER}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: write_dotenv_value()
## - Appends a NAME=VALUE line to a dotenv file
## - dotenv files are read by most CI systems (GitLab artifacts reports dotenv, GitHub Actions
##   GITHUB_ENV / GITHUB_OUTPUT, Jenkins readProperties)
## - Arguments
##   - $1: dotenv file
##   - $2: Variable name
##   - $3: Variable value
function write_dotenv_value() {
    local FUNCTION_DESCRIPTION="Write Dotenv Value"
    local TMP_FILE_DOTENV="${1:-}"
    local TMP_NAME="${2:-}"
    local TMP_VALUE="${3:-}"
    if [[ -z "${TMP_FILE_DOTENV}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: dotenv file not specified"
        return $E_BAD_ARGS
    fi
    if [[ ! "${TMP_NAME}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        log_error "${FUNCTION_DESCRIPTION}: invalid variable name [${TMP_NAME}]"
        return $E_BAD_ARGS
    fi
    if [[ "${TMP_VALUE}" == *$'\n'* ]]; then
        log_error "${FUNCTION_DESCRIPTION}: value for [${TMP_NAME}] contains a newline, which a dotenv file cannot hold"
        return $E_BAD_ARGS
    fi
    if ! printf '%s=%s\n' "${TMP_NAME}" "${TMP_VALUE}" >> "${TMP_FILE_DOTENV}" 2>/dev/null; then
        log_error "${FUNCTION_DESCRIPTION}: could not write to dotenv file [${TMP_FILE_DOTENV}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: write_key_value_to_file()
## - Appends a [key]=value line to a file (read it back with load_key_value_from_file)
## - Arguments
##   - $1: File
##   - $2: Key
##   - $3: Value
function write_key_value_to_file() {
    if [[ -z "${1:-}" ]]; then
        log_error "Write Key Value to File: file not specified"
        return $E_BAD_ARGS
    fi
    if ! printf '[%s]=%s\n' "${2:-}" "${3:-}" >> "${1}" 2>/dev/null; then
        log_error "Write Key Value to File: could not write to file [${1}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
}

###------------------------------------------------------------------------------------------------
# Internal helpers (not part of the public function list; names start with an underscore)

# _bamf_return <variable name> <value>: stores the value in the variable, or prints it if no name is given
function _bamf_return() {
    if [[ -z "${1}" ]]; then
        printf '%s' "${2}"
    elif [[ "${1}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        printf -v "${1}" '%s' "${2}"
    else
        log_error "invalid result variable name [${1}]"
        return $E_BAD_ARGS
    fi
}

# _bamf_set_variable <name> <value>: sets a global variable, checking the name first
function _bamf_set_variable() {
    if [[ ! "${1}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        log_error "invalid variable name [${1}]"
        return $E_BAD_ARGS
    fi
    declare -g "${1}=${2}"
}

# _bamf_unquote <value>: prints the value without one pair of matching surrounding quotes
function _bamf_unquote() {
    local __VALUE="${1}"
    if [[ ${#__VALUE} -ge 2 ]] && { [[ "${__VALUE}" == \"*\" ]] || [[ "${__VALUE}" == \'*\' ]]; }; then
        __VALUE="${__VALUE:1:${#__VALUE}-2}"
    fi
    printf '%s' "${__VALUE}"
}

# _bamf_comment_value <prefix> <file> <variable name>: value of the first line starting with the prefix,
# whitespace runs collapsed to one space
function _bamf_comment_value() {
    local __PREFIX="${1}"
    local __FILE="${2}"
    local __LINE=""
    local __VALUE=""
    local -a __WORDS=()
    if [[ -r "${__FILE}" ]]; then
        while IFS= read -r __LINE || [[ -n "${__LINE}" ]]; do
            if [[ "${__LINE}" == "${__PREFIX}"* ]]; then
                read -r -a __WORDS <<< "${__LINE#"${__PREFIX}"}" || true
                local IFS=' '
                __VALUE="${__WORDS[*]+"${__WORDS[*]}"}"
                break
            fi
        done < "${__FILE}"
    fi
    _bamf_return "${3}" "${__VALUE}"
}

# _bamf_script_header <identifier> <file>: prints a script header value, or "unknown"
function _bamf_script_header() {
    local __SH_FILE="${2}"
    local __SH_VALUE=""
    [[ -f "${__SH_FILE}.info" ]] && __SH_FILE="${__SH_FILE}.info"
    _bamf_comment_value "# ${1}:" "${__SH_FILE}" __SH_VALUE
    printf '%s' "${__SH_VALUE:-unknown}"
}

# _bamf_file_attribute <file> <variable name> <attribute>: size, mtime, md5sum, sha1sum, sha256sum or mime
function _bamf_file_attribute() {
    local __FILE="${1}"
    local __VALUE=""
    if [[ ! -f "${__FILE}" ]]; then
        _bamf_return "${2}" ""
        return $E_OBJECT_NOT_FOUND
    fi
    case "${3}" in
        size)                    __VALUE="$(command stat -c %s -- "${__FILE}" 2>/dev/null)" || __VALUE="";;
        mtime)                   __VALUE="$(command stat -c %Y -- "${__FILE}" 2>/dev/null)" || __VALUE="";;
        md5sum|sha1sum|sha256sum) __VALUE="$(command "${3}" -- "${__FILE}" 2>/dev/null)" || __VALUE=""; __VALUE="${__VALUE%% *}";;
        mime)
            if ! command -v file >/dev/null 2>&1; then
                log_error "Return File MIME: the file command is not installed"
                _bamf_return "${2}" ""
                return $E_MISSING_DEPENDENCIES
            fi
            __VALUE="$(command file --mime-type -b -- "${__FILE}" 2>/dev/null)" || __VALUE="";;
    esac
    _bamf_return "${2}" "${__VALUE}"
}

# _bamf_is_temporary_path <path>: true if the path is beneath the temporary directory or /tmp
function _bamf_is_temporary_path() {
    [[ "${1}" != *"/../"* ]] && { [[ "${1}" == /tmp/?* ]] || [[ "${1}" == "${DIRECTORY_TEMPORARY}"/?* ]]; }
}

# _log_set_color <variable name> <color definition>: stores the escape bytes for a color definition
function _log_set_color() {
    printf -v "${1}" '%b' "${2}"
}

# _log_build_prefix: builds the "[identity]...." prefixes from SELF_IDENTITY and the loaded palette
function _log_build_prefix() {
    local TMP_PAD_LENGTH=0
    LOG_IDENTITY_WIDTH=${#SELF_IDENTITY}
    if is_int "${BAMF_LOG_IDENTITY_WIDTH:-}" && (( BAMF_LOG_IDENTITY_WIDTH > LOG_IDENTITY_WIDTH )); then
        LOG_IDENTITY_WIDTH=${BAMF_LOG_IDENTITY_WIDTH}
    fi
    TMP_PAD_LENGTH=$(( LOG_IDENTITY_WIDTH - ${#SELF_IDENTITY} ))
    LOG_PREFIX_PLAIN="[${SELF_IDENTITY}]${LOG_PAD:0:${TMP_PAD_LENGTH}}"
    LOG_PREFIX_COLOR="${LOG_COLOR_DETAILS}[${LOG_COLOR_IDENT}${SELF_IDENTITY}${LOG_COLOR_MESSAGE}${LOG_COLOR_DETAILS}]${LOG_PAD:0:${TMP_PAD_LENGTH}}"
}

# _log_load_palette: loads the parent or child palette (or none when color is off)
function _log_load_palette() {
    if option_enabled ENABLE_COLOR; then
        if option_enabled RUN_SCRIPT_AS_CHILD; then
            log_load_color_child
        else
            log_load_color_parent
        fi
    else
        _log_build_prefix
    fi
}

# _bamf_start <log the start line, yes or no>: see start_logic
function _bamf_start() {
    local ELEMENT=""
    # SELF_IDENTITY or ENABLE_COLOR may have changed since bamf.inc was sourced
    _log_load_palette
    if [[ "${1}" == "yes" ]]; then
        log "Exec::${SELF_EXEC} / PID::$$" SSTR
    fi

    # the exit codes are expanded now, on purpose
    # shellcheck disable=SC2064
    trap "exit_trap ${E_TRAP_SIGHUP}" SIGHUP
    # shellcheck disable=SC2064
    trap "exit_trap ${E_TRAP_SIGINT}" SIGINT
    # shellcheck disable=SC2064
    trap "exit_trap ${E_TRAP_SIGQUIT}" SIGQUIT
    # shellcheck disable=SC2064
    trap "exit_trap ${E_TRAP_SIGTERM}" SIGTERM
    # shellcheck disable=SC2064
    trap "exit_trap ${E_TRAP_SIGTSTP}" SIGTSTP
    trap "exit_trap_ensure_graceful" EXIT

    # Disable CTRL+Z on a terminal: a script stopped with CTRL+Z cannot clean up after itself.
    # It is enabled again when the script exits.
    if [[ -t 0 ]] && command stty susp ^- >/dev/null 2>&1; then
        BAMF_TERMINAL_SUSPEND_DISABLED=yes
    fi

    for ELEMENT in "${REQUIRED_SOURCE_FILES[@]+"${REQUIRED_SOURCE_FILES[@]}"}"; do
        [[ -n "${ELEMENT}" ]] && source_include "${ELEMENT}"
    done
    verify_dependencies
}

# _bamf_exit_code_argument <argument count> <exit code>: prints the exit code to use; no argument is 0,
# an empty argument (an unset or misspelled variable) is E_UNKNOWN_EXIT_CODE, with a warning on stderr
function _bamf_exit_code_argument() {
    if [[ "${1}" -eq 0 ]]; then
        printf '0'
    elif [[ -z "${2}" ]]; then
        log_warning "exit_logic was given an empty exit code (an unset or misspelled variable?); exiting with E_UNKNOWN_EXIT_CODE" >&2
        printf '%s' "${E_UNKNOWN_EXIT_CODE}"
    else
        printf '%s' "${2}"
    fi
}

# _bamf_run_exit_functions <exit code> <exit mode>: runs the functions registered with add_exit_function,
# the last registered first; each is taken off the list before it runs, so it never runs twice
function _bamf_run_exit_functions() {
    local TMP_FUNCTION=""
    local TMP_RETURNVAL=0
    # shellcheck disable=SC2034 # read by the functions registered with add_exit_function
    BAMF_EXIT_CODE="${1}"
    while [[ ${#BAMF_EXIT_FUNCTIONS[@]} -gt 0 ]]; do
        TMP_FUNCTION="${BAMF_EXIT_FUNCTIONS[${#BAMF_EXIT_FUNCTIONS[@]}-1]}"
        unset "BAMF_EXIT_FUNCTIONS[${#BAMF_EXIT_FUNCTIONS[@]}-1]"
        TMP_RETURNVAL=0
        if [[ "${2}" == "quiet" ]]; then
            "${TMP_FUNCTION}" >/dev/null 2>&1 || TMP_RETURNVAL=$?
        else
            "${TMP_FUNCTION}" || TMP_RETURNVAL=$?
        fi
        if (( TMP_RETURNVAL != 0 )) && [[ "${2}" != "quiet" ]]; then
            log_warning "Exit function [${TMP_FUNCTION}] failed (returned::${TMP_RETURNVAL})"
        fi
    done
    return 0
}

# _bamf_exit <exit code> <message> <mode: normal, quiet, skip_cleanup or trap>: see exit_logic
function _bamf_exit() {
    local EXIT_CODE="${1}"
    local EXIT_MSG=""
    local EXIT_MODE="${3}"
    local EXIT_COLOR=""
    local EXIT_DESCRIPTION="success"
    if [[ ! "${EXIT_CODE}" =~ ^[0-9]{1,3}$ ]] || (( 10#${EXIT_CODE} > 255 )); then
        EXIT_CODE=${E_UNKNOWN_EXIT_CODE}
    fi
    EXIT_CODE=$((10#${EXIT_CODE}))
    # set first, so an error during cleanup cannot run the exit logic a second time
    GRACEFUL_EXIT=yes
    [[ -n "${2}" ]] && EXIT_MSG=" / Exit_Message::${2}"

    _bamf_run_exit_functions "${EXIT_CODE}" "${EXIT_MODE}"
    if [[ "${EXIT_MODE}" == "quiet" ]]; then
        cleanup_quiet
    else
        if [[ "${EXIT_MODE}" == "trap" ]]; then
            log_error "Terminating script prematurely [caught trap]"
        elif (( EXIT_CODE != 0 && EXIT_CODE != E_SUCCESS_ALT )); then
            log_error "Terminating script due to error"
        fi
        if [[ "${EXIT_MODE}" == "skip_cleanup" ]]; then
            log_warning "Cleanup: Skipping (exit conditions explicitly requested we skip cleanup)"
        else
            cleanup
        fi
        if (( EXIT_CODE == 0 || EXIT_CODE == E_SUCCESS_ALT )); then
            EXIT_COLOR="${LOG_COLOR_SUCCESS}"
        else
            EXIT_COLOR="${LOG_COLOR_ERROR}"
        fi
        (( EXIT_CODE != 0 )) && EXIT_DESCRIPTION="$(lookup_exit_code "${EXIT_CODE}")"
        log "(Duration::$(time_elapsed "${RUNCODE}") / Exit_Code::${EXIT_CODE} [${EXIT_DESCRIPTION}]${EXIT_MSG})" SFIN "${EXIT_COLOR}"
    fi

    if option_enabled ENABLE_COLOR && [[ "${EXIT_MODE}" != "quiet" ]]; then
        printf '%b' "${COLOR_NONE}"
    fi
    if option_enabled BAMF_TERMINAL_SUSPEND_DISABLED; then
        command stty susp ^z >/dev/null 2>&1 || true
    fi
    # wait for background jobs on a normal exit; after a signal, stop now
    if [[ "${EXIT_MODE}" != "trap" ]]; then
        wait
    fi
    exit "${EXIT_CODE}"
}

###------------------------------------------------------------------------------------------------
## Self Initialization
# Command line as written to the log; values of the options listed in SENSITIVE_ARGUMENTS (option
# letters, example: SENSITIVE_ARGUMENTS="s" for "-s <secret>") are replaced with LOG_MASK
SELF_EXEC="${0##*/}"
__BAMF_MASK_NEXT=no
__BAMF_END_OF_OPTIONS=no
for __BAMF_ARG in "$@"; do
    if [[ "${__BAMF_MASK_NEXT}" == "yes" ]]; then
        SELF_EXEC+=" ${LOG_MASK}"
        __BAMF_MASK_NEXT=no
        continue
    fi
    if [[ "${__BAMF_ARG}" == "--" ]]; then
        __BAMF_END_OF_OPTIONS=yes
    elif [[ -n "${SENSITIVE_ARGUMENTS}" ]] && [[ "${__BAMF_END_OF_OPTIONS}" == "no" ]] && [[ "${__BAMF_ARG}" =~ ^-[A-Za-z] ]]; then
        for (( __BAMF_INDEX=1; __BAMF_INDEX<${#__BAMF_ARG}; __BAMF_INDEX++ )); do
            if [[ "${SENSITIVE_ARGUMENTS}" == *"${__BAMF_ARG:${__BAMF_INDEX}:1}"* ]]; then
                if [[ $((__BAMF_INDEX+1)) -lt ${#__BAMF_ARG} ]]; then
                    __BAMF_ARG="${__BAMF_ARG:0:$((__BAMF_INDEX+1))}${LOG_MASK}"
                else
                    __BAMF_MASK_NEXT=yes
                fi
                break
            fi
        done
    fi
    SELF_EXEC+=" ${__BAMF_ARG}"
done
unset __BAMF_ARG __BAMF_MASK_NEXT __BAMF_END_OF_OPTIONS __BAMF_INDEX
RUNCODE="$(command date +%s%6N)"
# both are read by name, through option_enabled, which ShellCheck cannot follow
# shellcheck disable=SC2034
GRACEFUL_EXIT=no
# shellcheck disable=SC2034
BAMF_TERMINAL_SUSPEND_DISABLED=no
_log_load_palette
