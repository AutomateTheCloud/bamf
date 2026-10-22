#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# String, array, encoding and conversion helpers

# shellcheck disable=SC2034 # variables are read by the library functions under test
load test_helper

setup() {
    setup_bamf
    load_bamf
}

@test "is_empty: strict mode treats empty, 0 and 0.0 as empty" {
    is_empty ""
    is_empty
    is_empty "0"
    is_empty "0.0"
    is_empty "0" yes
    is_empty "0" y
    ! is_empty "00" || return 1
    ! is_empty " 0" || return 1
    ! is_empty "abc" || return 1
    ! is_empty "-n" || return 1
}

@test "is_empty: non-strict mode treats only the empty string as empty" {
    is_empty "" no
    ! is_empty "0" no || return 1
    ! is_empty "0.0" n || return 1
    ! is_empty "abc" no || return 1
}

@test "is_int" {
    is_int 0
    is_int 42
    is_int -7
    ! is_int "" || return 1
    ! is_int "4.2" || return 1
    ! is_int "5a" || return 1
    ! is_int "-" || return 1
}

@test "option_enabled: accepted values, any case, surrounding whitespace ignored" {
    local VALUE
    for VALUE in y yes Y YES Yes 1 true TRUE True " yes " $'yes\n'; do
        TEST_OPTION="${VALUE}"
        option_enabled TEST_OPTION || { echo "rejected [${VALUE}]"; return 1; }
    done
    for VALUE in "" no n 0 "yes no" "y*"; do
        TEST_OPTION="${VALUE}"
        ! option_enabled TEST_OPTION || { echo "accepted [${VALUE}]"; return 1; }
    done
    unset TEST_OPTION
    ! option_enabled TEST_OPTION || return 1
    ! option_enabled "" || return 1
    ! option_enabled 'bad name' || return 1
}

@test "to_upper / to_lower: including values that look like echo options" {
    [ "$(to_upper "abc-Def")" = "ABC-DEF" ]
    [ "$(to_lower "ABC-Def")" = "abc-def" ]
    [ "$(to_upper "-n")" = "-N" ]
    [ "$(to_lower "-E")" = "-e" ]
}

@test "trim: removes spaces only, per line for multi-line strings" {
    [ "$(trim "  a b  ")" = "a b" ]
    [ "$(trim "-e")" = "-e" ]
    [ "$(trim $'\ttab\t')" = $'\ttab\t' ]
    [ "$(trim $' one \n  two  ')" = $'one\ntwo' ]
}

@test "add_element_to_array: appends elements, keeping spaces; rejects bad names" {
    LIST=(x)
    add_element_to_array LIST "y z" w
    [ "${#LIST[@]}" -eq 3 ]
    [ "${LIST[1]}" = "y z" ]
    add_element_to_array LIST '$(touch nope)'
    [ "${LIST[3]}" = '$(touch nope)' ]
    run add_element_to_array 'LIST;touch x' a
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "does_array_contain_element" {
    LIST=("a b" c)
    does_array_contain_element "a b" "${LIST[@]}"
    ! does_array_contain_element "a" "${LIST[@]}" || return 1
    ! does_array_contain_element "a" || return 1
}

@test "base64: round trip is exact, no newline added" {
    local ENCODED DECODED
    base64_encode ENCODED "abc"
    [ "${ENCODED}" = "YWJj" ]
    [ "$(base64_encode "" "it's")" = "aXQncw==" ]
    base64_decode DECODED "${ENCODED}"
    [ "${DECODED}" = "abc" ]
    run base64_decode DECODED "!!not base64!!"
    [ "${status}" -eq "${E_BASE64_CONVERSION_FAILURE}" ]
}

@test "base64_encode_and_compress: decodes back to the input" {
    command -v gzip >/dev/null || skip "gzip is not installed"
    local ENCODED
    base64_encode_and_compress ENCODED "hello hello hello"
    [ "$(printf '%s' "${ENCODED}" | base64 -d | gunzip)" = "hello hello hello" ]
}

@test "generate_uuid: 32 lowercase hex characters, or the requested length" {
    local UUID
    generate_uuid UUID
    [[ "${UUID}" =~ ^[0-9a-f]{32}$ ]]
    [[ "$(generate_uuid "" 8)" =~ ^[0-9a-f]{8}$ ]]
}

@test "json_extract_from_string: strings, numbers, booleans, missing keys" {
    local JSON='{"name": "web \"1\"", "port": 8080, "enabled": true, "nested": {"x": 1}}'
    [ "$(json_extract_from_string "" name "${JSON}")" = 'web \"1\"' ]
    [ "$(json_extract_from_string "" port "${JSON}")" = "8080" ]
    [ "$(json_extract_from_string "" enabled "${JSON}")" = "true" ]
    [ -z "$(json_extract_from_string "" missing "${JSON}")" ]
}

@test "json_safe: quotes and newlines replaced" {
    [ "$(json_safe $'  say "hi"\nit'"'"'s  ')" = "say _hi__it_s" ]
}

@test "filesize_bytes_to_human_readable" {
    [ "$(filesize_bytes_to_human_readable 512)" = "512 bytes" ]
    [ "$(filesize_bytes_to_human_readable 1536)" = "1.50 KB" ]
    [ "$(filesize_bytes_to_human_readable 1572864)" = "1.50 MB" ]
    run filesize_bytes_to_human_readable abc
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "version_to_integer: sorts like the version" {
    local A B
    version_to_integer 1.2.3 A
    version_to_integer 1.10.0 B
    [ "${A}" -lt "${B}" ]
    [ "$(version_to_integer 2.4.10-1)" = "2004010001" ]
    run version_to_integer 1.2.x
    [ "${status}" -ne 0 ]
    run version_to_integer 1.2.3.4.5
    [ "${status}" -ne 0 ]
}

@test "time_elapsed: minutes, seconds, milliseconds" {
    [ "$(time_elapsed 0 65250000)" = "1m 05.250s" ]
    [ "$(time_elapsed 1000000 1000000)" = "0m 00.000s" ]
}

@test "timestamp_to_human_readable: UTC, subseconds ignored" {
    [ "$(timestamp_to_human_readable 0 yes)" = "1970-01-01 00:00:00 UTC" ]
    [ "$(timestamp_to_human_readable 1700000000123456 yes)" = "2023-11-14 22:13:20 UTC" ]
    run timestamp_to_human_readable abc
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "function_exists: quiet, true only for functions" {
    [ -z "$(function_exists log)" ]
    function_exists log
    ! function_exists ls || return 1
    ! function_exists no_such_function || return 1
}

@test "return_variable_value: collapses whitespace, no glob expansion, rejects invalid names" {
    cd "${BATS_TEST_TMPDIR}"
    touch pXss
    SAMPLE=$'  two  spaces\tand tab  '
    return_variable_value SAMPLE RESULT
    [ "${RESULT}" = "two spaces and tab" ]
    SAMPLE='p?ss'
    return_variable_value SAMPLE RESULT
    [ "${RESULT}" = 'p?ss' ]
    run return_variable_value "BAD-NAME" RESULT
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "base64_encode_and_compress: E_MISSING_DEPENDENCIES without gzip" {
    PATH="${BATS_TEST_TMPDIR}/empty" run base64_encode_and_compress RESULT "x"
    [ "${status}" -eq "${E_MISSING_DEPENDENCIES}" ]
    [[ "${output}" == *"gzip is not installed"* ]]
}

@test "result variables: invalid names are rejected" {
    run base64_encode 'bad name' x
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "call_sleep: rejects non-numbers, says 1 second" {
    run call_sleep abc
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    run call_sleep 0
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"Sleeping for 0 seconds"* ]]
}

@test "return_hostname / is_running_as_root" {
    [ -n "$(return_hostname)" ]
    if [ "$(id -u)" -eq 0 ]; then is_running_as_root; else ! is_running_as_root || return 1; fi
}

@test "write_dotenv_value: writes NAME=VALUE and rejects invalid input" {
    local FILE="${BATS_TEST_TMPDIR}/out.env"
    write_dotenv_value "${FILE}" APP_URL "https://x.example/?a=1&b=2"
    write_dotenv_value "${FILE}" EMPTY ""
    [ "$(cat "${FILE}")" = $'APP_URL=https://x.example/?a=1&b=2\nEMPTY=' ]
    run write_dotenv_value "${FILE}" 1BAD x
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    run write_dotenv_value "${FILE}" GOOD $'multi\nline'
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    run write_dotenv_value "" GOOD x
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "json_escape: quotes, backslashes and control characters; the result parses back exactly" {
    local INPUT=$'say "hi" \\ path\ttab\nline\r\x01end & done'
    [ "$(json_escape "${INPUT}")" = 'say \"hi\" \\ path\ttab\nline\r\u0001end & done' ]
    [ "$(json_escape "plain")" = "plain" ]
    [ -z "$(json_escape "")" ]
    if command -v python3 >/dev/null; then
        [ "$(python3 -c 'import json,sys; print(json.loads("\"" + sys.argv[1] + "\""), end="")' "$(json_escape "${INPUT}")")" = "${INPUT}" ]
    fi
}

set_value_and_fail() {
    RUN_SILENTLY_VALUE="set"
    echo "the details of what went wrong"
    return "${1}"
}

@test "run_silently: quiet on success with variables kept, the output and exit code on failure" {
    RUN_SILENTLY_VALUE=""
    run_silently set_value_and_fail 0 > "${BATS_TEST_TMPDIR}/out"
    [ ! -s "${BATS_TEST_TMPDIR}/out" ]
    [ "${RUN_SILENTLY_VALUE}" = "set" ]
    run run_silently set_value_and_fail 7
    [ "${status}" -eq 7 ]
    [ "${output}" = "the details of what went wrong" ]
}
