#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Bugs found in earlier versions of BAMF; each test fails on the old code

load test_helper

setup() {
    setup_bamf
    load_bamf
}

@test "lookup_exit_code 1 is not matched by prefix to 124" {
    [ "$(lookup_exit_code 1)" = "E_BASH_GENERIC_FAILURE / Generic failure (catch all)" ]
}

@test "a value read from a file cannot run commands" {
    printf "[k]=a'; touch %s/PWNED; '\n" "${BATS_TEST_TMPDIR}" > "${BATS_TEST_TMPDIR}/kv"
    load_key_value_from_file VALUE k "${BATS_TEST_TMPDIR}/kv"
    [ ! -e "${BATS_TEST_TMPDIR}/PWNED" ]
}

@test "base64_encode does not encode a trailing newline" {
    [ "$(base64_encode "" abc | base64 -d | od -An -c | tr -d ' ')" = "abc" ]
}

@test "function_exists prints nothing" {
    [ -z "$(function_exists log)" ]
}

@test "color_lookup knows purple and light_purple" {
    [ "$(color_lookup purple)" = '\e[0;35m' ]
    [ "$(color_lookup light_purple)" = '\e[1;35m' ]
}

@test "cleanup only removes what it created, and only beneath the temporary directory" {
    TEMPORARY_FILES+=( "/etc/passwd" )
    run cleanup
    [ -f /etc/passwd ]
    [[ "${output}" == *"not located in a known temporary directory [/etc/passwd]"* ]]
}

@test "line_break works under set -e without a terminal type (cron, CI, containers)" {
    run env -u TERM bash -c "set -euo pipefail; source '${BAMF_ROOT}/bamf.inc'; line_break; echo survived"
    [ "${status}" -eq 0 ]
    [ "${lines[1]}" = "survived" ]
    [ "${#lines[0]}" -eq "${DEFAULT_SCREEN_WIDTH}" ]
}

@test "a whole script runs under set -e without a terminal type" {
    run env -u TERM MODE=temp OUTPUT_FILE="${BATS_TEST_TMPDIR}/out" bash -c "set -e; source '${BAMF_ROOT}/tests/fixtures/script.sh'"
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"Exit_Code::0 [success]"* ]]
}

@test "exit_logic with an empty exit code (an unset variable) is not a success" {
    run bash -c "source '${BAMF_ROOT}/bamf.inc'; start_logic_quiet; exit_logic \"\${E_NOT_DEFINED_ANYWHERE:-}\""
    [ "${status}" -eq 255 ]
    [[ "${output}" == *"given an empty exit code"* ]]
    run bash -c "source '${BAMF_ROOT}/bamf.inc'; start_logic_quiet; exit_logic"
    [ "${status}" -eq 0 ]
}
