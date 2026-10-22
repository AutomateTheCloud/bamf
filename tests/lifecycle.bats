#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Sourcing, start_logic, exit_logic, traps, cleanup, exit codes and helper files

load test_helper

setup() {
    setup_bamf
    export OUTPUT_FILE="${BATS_TEST_TMPDIR}/output"
}

run_fixture() {
    run bash "${FIXTURES}/script.sh" "$@"
}

@test "sourcing: SELF_IDENTITY defaults to the script name" {
    run bash -c "source '${BAMF_ROOT}/bamf.inc'; printf '%s' \"\${SELF_IDENTITY}\""
    [ "${output}" = "bash" ]
    printf '#!/bin/bash\nsource "%s/bamf.inc"\nprintf "%%s" "${SELF_IDENTITY}"\n' "${BAMF_ROOT}" > "${BATS_TEST_TMPDIR}/my_tool.sh"
    run bash "${BATS_TEST_TMPDIR}/my_tool.sh"
    [ "${output}" = "my_tool" ]
}

@test "sourcing: a read-only SELF_IDENTITY set by the script is kept" {
    run bash -c "declare -r SELF_IDENTITY=fixed; source '${BAMF_ROOT}/bamf.inc' && log hi"
    [ "${status}" -eq 0 ]
    [[ "${output}" == "[fixed]"* ]]
}

@test "sourcing: twice is harmless" {
    run bash -c "source '${BAMF_ROOT}/bamf.inc'; source '${BAMF_ROOT}/bamf.inc' && echo ok"
    [ "${status}" -eq 0 ]
    [ "${output}" = "ok" ]
}

@test "sourcing: works under set -euo pipefail" {
    run bash -c "set -euo pipefail; source '${BAMF_ROOT}/bamf.inc'; log ok"
    [ "${status}" -eq 0 ]
    [[ "${output}" == *" ok" ]]
}

@test "sourcing: from inside a function" {
    run bash -c "f() { source '${BAMF_ROOT}/bamf.inc'; }; f; log ok; lookup_exit_code 5"
    [ "${status}" -eq 0 ]
    [ "${lines[1]}" = "E_BAD_ARGS / Bad or missing arguments" ]
}

@test "temporary directory: TMPDIR by default, BAMF_DIRECTORY_TEMPORARY wins and is created" {
    unset BAMF_DIRECTORY_TEMPORARY
    TMPDIR="${BATS_TEST_TMPDIR}" run bash -c "source '${BAMF_ROOT}/bamf.inc'; printf '%s' \"\${DIRECTORY_TEMPORARY}\""
    [ "${output}" = "${BATS_TEST_TMPDIR}" ]
    BAMF_DIRECTORY_TEMPORARY="${BATS_TEST_TMPDIR}/custom/" run bash -c "source '${BAMF_ROOT}/bamf.inc'; printf '%s' \"\${DIRECTORY_TEMPORARY}\""
    [ "${output}" = "${BATS_TEST_TMPDIR}/custom" ]
    [ -d "${BATS_TEST_TMPDIR}/custom" ]
}

@test "temporary directory: / and relative paths are rejected" {
    local VALUE
    for VALUE in "/" "//" "relative/path"; do
        BAMF_DIRECTORY_TEMPORARY="${VALUE}" run bash -c "source '${BAMF_ROOT}/bamf.inc'"
        [ "${status}" -eq 3 ]
        [[ "${output}" == *"must be an absolute path other than /"* ]]
    done
}

@test "exit_logic 0: start and finish lines, exit code 0" {
    run_fixture -a value
    [ "${status}" -eq 0 ]
    [[ "${lines[0]}" == "[fixture]"*"SCRIPT_START: Exec::script.sh -a value / PID::"* ]]
    [[ "${lines[1]}" == *"running mode success" ]]
    [[ "${lines[2]}" =~ SCRIPT_FINISHED:\ \(Duration::0m\ [0-9]{2}\.[0-9]{3}s\ /\ Exit_Code::0\ \[success\]\)$ ]]
}

@test "exit_logic with an error: named exit code and message" {
    MODE=fail run_fixture
    [ "${status}" -eq 8 ]
    [[ "${output}" == *"ERROR: Terminating script due to error"* ]]
    [[ "${output}" == *"Exit_Code::8 [E_OBJECT_NOT_FOUND / Object not found] / Exit_Message::thing not found)"* ]]
}

@test "exit_logic E_SUCCESS_ALT: non-zero, but not an error" {
    MODE=success_alt run_fixture
    [ "${status}" -eq 251 ]
    [[ "${output}" != *"Terminating script due to error"* ]]
}

@test "define_exit_code: a script's own exit code is named in the log" {
    MODE=custom_code run_fixture
    [ "${status}" -eq 201 ]
    [[ "${output}" == *"Exit_Code::201 [E_FIXTURE_FAILURE / Fixture failure]"* ]]
}

@test "define_exit_code: invalid definitions and reused numbers are refused" {
    load_bamf
    run define_exit_code BAD_NAME 201
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    run define_exit_code E_TOO_BIG 256
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    run define_exit_code E_TAKEN 5
    [ "${status}" -eq "${E_OBJECT_ALREADY_EXISTS}" ]
    define_exit_code E_BAD_ARGS 5 "redefined, same number"
}

@test "lookup_exit_code: exact match on the code" {
    load_bamf
    [ "$(lookup_exit_code 0)" = "E_SUCCESS / Success" ]
    [ "$(lookup_exit_code 1)" = "E_BASH_GENERIC_FAILURE / Generic failure (catch all)" ]
    [ "$(lookup_exit_code 10)" = "E_OBJECT_FAILED_TO_CREATE / Could not create object" ]
    [ "$(lookup_exit_code 124)" = "E_OPERATION_TIMEOUT / Requested operation timed out (normally returned on error when [timeout] command is used)" ]
    [ "$(lookup_exit_code 121)" = "UNKNOWN" ]
    [ "$(lookup_exit_code 999)" = "UNKNOWN" ]
    [ "$(lookup_exit_code abc)" = "UNKNOWN" ]
}

@test "no exit_logic at the end: the EXIT trap still finishes the script" {
    MODE=no_exit run_fixture
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"SCRIPT_FINISHED: (Duration::"*"Exit_Code::0 [success])"* ]]
    MODE=no_exit_failure run_fixture
    [ "${status}" -eq 1 ]
    [[ "${output}" == *"Exit_Code::1 [E_BASH_GENERIC_FAILURE"* ]]
}

@test "set -euo pipefail: helpers work, a failing command ends the script through exit_logic" {
    MODE=set_e run_fixture
    [ "${status}" -eq 1 ]
    [[ "${output}" == *"unset is fine: default"* ]]
    [[ "${output}" == *"is_empty works"* ]]
    [[ "${output}" == *"option_enabled works"* ]]
    [[ "${output}" != *"not reached"* ]]
    [[ "${output}" == *"Exit_Code::1 [E_BASH_GENERIC_FAILURE"* ]]
    [ -z "$(ls -A "${BAMF_DIRECTORY_TEMPORARY}")" ]
}

@test "temporary files are removed on exit, kept by exit_logic_skip_cleanup" {
    MODE=temp run_fixture
    [ "${status}" -eq 0 ]
    [ ! -e "$(sed -n 1p "${OUTPUT_FILE}")" ]
    [ ! -e "$(sed -n 2p "${OUTPUT_FILE}")" ]
    MODE=temp SKIP_CLEANUP=yes run_fixture
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"Cleanup: Skipping"* ]]
    [ -f "$(sed -n 1p "${OUTPUT_FILE}")" ]
    [ -d "$(sed -n 2p "${OUTPUT_FILE}")" ]
}

@test "signal: SIGTERM is caught, temporary files removed, exit code 143" {
    MODE=signal run_fixture
    [ "${status}" -eq 143 ]
    [[ "${output}" == *"ERROR: Terminating script prematurely [caught trap]"* ]]
    [[ "${output}" == *"Exit_Code::143 [E_TRAP_SIGTERM"* ]]
    [[ "${output}" != *"not reached"* ]]
    [ ! -e "$(cat "${OUTPUT_FILE}")" ]
}

@test "SENSITIVE_ARGUMENTS: values are masked in the start line" {
    local ARGS
    for ARGS in "-p /x -s TOPSECRET -r us-east-1" "-p /x -sTOPSECRET" "-ds TOPSECRET -p /x"; do
        # shellcheck disable=SC2086
        run_fixture ${ARGS}
        [ "${status}" -eq 0 ]
        [[ "${lines[0]}" == *"Exec::script.sh"*"********"* ]]
        [[ "${output}" != *"TOPSECRET"* ]]
    done
    run_fixture -- -s NOT_AN_OPTION
    [[ "${lines[0]}" == *"-- -s NOT_AN_OPTION"* ]]
}

@test "missing dependency: E_MISSING_DEPENDENCIES" {
    MODE=missing_dependency run_fixture
    [ "${status}" -eq 7 ]
    [[ "${output}" == *"Could not locate required executable: no_such_command_bamf"* ]]
}

@test "helper files: loaded by name from the helpers directory" {
    local DIR="${BATS_TEST_TMPDIR}/lib"
    mkdir -p "${DIR}/helpers"
    cp "${BAMF_ROOT}/bamf.inc" "${DIR}/"
    printf 'declare -g HELPER_VALUE="from helpers"\n' > "${DIR}/helpers/bamf.first.inc"
    printf 'declare -g HELPER_VALUE="from beside"\n' > "${DIR}/bamf.first.inc"
    BAMF_INC="${DIR}/bamf.inc" MODE=helper HELPER=first run_fixture
    [[ "${output}" == *"helper value: from helpers"* ]]
    BAMF_INC="${DIR}/bamf.inc" MODE=helper HELPER=bamf.first.inc run_fixture
    [[ "${output}" == *"helper value: from helpers"* ]]
}

@test "helper files: loaded by name from beside bamf.inc, in order, once" {
    local DIR="${BATS_TEST_TMPDIR}/lib"
    mkdir -p "${DIR}"
    cp "${BAMF_ROOT}/bamf.inc" "${DIR}/"
    printf 'declare -g HELPER_VALUE="${HELPER_VALUE:-}first"\nREQUIRED_EXECUTABLES+=( "bash" )\n' > "${DIR}/bamf.first.inc"
    BAMF_INC="${DIR}/bamf.inc" MODE=helper HELPER=first run_fixture
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"helper value: first"* ]]
    BAMF_INC="${DIR}/bamf.inc" MODE=helper HELPER=bamf.first.inc run_fixture
    [[ "${output}" == *"helper value: first"* ]]
    BAMF_INC="${DIR}/bamf.inc" MODE=helper HELPER="${DIR}/bamf.first.inc" run_fixture
    [[ "${output}" == *"helper value: first"* ]]
}

@test "helper files: a missing helper ends the script with E_OBJECT_NOT_FOUND" {
    MODE=helper HELPER=no_such_helper run_fixture
    [ "${status}" -eq 8 ]
    [[ "${output}" == *"file not found ["*"/bamf.no_such_helper.inc]"* ]]
}

@test "exec_script: child runs with RUN_SCRIPT_AS_CHILD, return codes passed back" {
    MODE=child run_fixture
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"Exec::script.sh -x child_argument"* ]]
    [[ "${output}" == *"child returned 0"* ]]
    [[ "${output}" == *"Script failed (Script::script.sh / Return_Code::8 [E_OBJECT_NOT_FOUND"* ]]
    [[ "${output}" == *"failing child returned 8"* ]]
}

@test "exit_logic_quiet: no log lines" {
    MODE=quiet run_fixture
    [ "${status}" -eq 0 ]
    [ "${#lines[@]}" -eq 2 ]
}

@test "usage: banner from the script headers and ARGUMENTS_DESCRIPTION" {
    MODE=usage run_fixture
    [[ "${output}" == *"USAGE:       script.sh <optional arguments>"* ]]
    [[ "${output}" == *"DESCRIPTION: Test fixture for the BAMF lifecycle"* ]]
    [[ "${output}" == *"VERSION:     1.2.3"* ]]
    [[ "${output}" == *"-h : Show help message"* ]]
}

@test "add_exit_function: runs last-registered first, once each, on every way out" {
    local HOW
    for HOW in logic set_e signal; do
        MODE=exit_functions EXIT_HOW="${HOW}" run_fixture
        [[ "${output}" == *"refused an unknown function"* ]]
        [[ "${output}" == *"second exit function, temp file still there: yes"*"Exit function [second_exit_function] failed (returned::3)"*"first exit function (exit code ${status})"* ]] || { echo "${HOW}: ${output}"; return 1; }
        [ "$(grep -c 'second exit function' <<< "${output}")" -eq 1 ]
    done
    MODE=exit_functions EXIT_HOW=logic run_fixture
    [ "${status}" -eq 8 ]
}

@test "exec_script: a child pads its name to the parent's column, so their lines line up" {
    printf '#!/bin/bash\nSELF_IDENTITY=kid\nsource "%s/bamf.inc"\nstart_logic_quiet\nlog "child line"\nexit_logic_quiet 0\n' "${BAMF_ROOT}" > "${BATS_TEST_TMPDIR}/kid.sh"
    chmod +x "${BATS_TEST_TMPDIR}/kid.sh"
    run bash -c "SELF_IDENTITY=parent_script; source '${BAMF_ROOT}/bamf.inc'; start_logic_quiet; log 'parent line'; exec_script '${BATS_TEST_TMPDIR}/kid.sh' >/dev/null; exec_script '${BATS_TEST_TMPDIR}/kid.sh' | grep 'child line'; exit_logic_quiet 0"
    [ "${status}" -eq 0 ]
    [ "${lines[0]}" = "[parent_script] parent line" ]
    [ "${lines[1]}" = "[kid].......... child line" ]
}
