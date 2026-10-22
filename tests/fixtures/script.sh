#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  script.sh
# purpose: Test fixture for the BAMF lifecycle
# version: 1.2.3
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# The MODE environment variable picks what the script does; BAMF_INC picks the bamf.inc to load.

# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="fixture"
# shellcheck disable=SC2034 # read by bamf.inc
SENSITIVE_ARGUMENTS="s"
# shellcheck source=../../bamf.inc
source "${BAMF_INC:-$(dirname "${BASH_SOURCE[0]}")/../../bamf.inc}" || exit 3

case "${MODE:-success}" in
    set_e) set -euo pipefail;;
    missing_dependency) REQUIRED_EXECUTABLES+=( 'no_such_command_bamf' );;
    helper) REQUIRED_SOURCE_FILES+=( "${HELPER:-}" );;
esac

start_logic
log "running mode ${MODE:-success}"

case "${MODE:-success}" in
    success)
        exit_logic 0
        ;;
    success_alt)
        exit_logic "${E_SUCCESS_ALT}"
        ;;
    fail)
        exit_logic "${E_OBJECT_NOT_FOUND}" "thing not found"
        ;;
    custom_code)
        define_exit_code E_FIXTURE_FAILURE 201 "Fixture failure"
        exit_logic "${E_FIXTURE_FAILURE}"
        ;;
    no_exit)
        log "ending without exit_logic"
        ;;
    no_exit_failure)
        false
        ;;
    set_e)
        log "unset is fine: ${UNSET_VARIABLE:-default}"
        generate_temp_file TMP_FILE "set -e"
        is_empty "" && log "is_empty works"
        option_enabled NOT_SET || log "option_enabled works"
        false
        log "not reached"
        ;;
    temp)
        generate_temp_file TMP_FILE "test"
        generate_temp_directory TMP_DIRECTORY "test"
        printf '%s\n%s\n' "${TMP_FILE}" "${TMP_DIRECTORY}" > "${OUTPUT_FILE}"
        if [[ "${SKIP_CLEANUP:-no}" == "yes" ]]; then exit_logic_skip_cleanup 0; fi
        exit_logic 0
        ;;
    signal)
        generate_temp_file TMP_FILE "test"
        printf '%s\n' "${TMP_FILE}" > "${OUTPUT_FILE}"
        kill -TERM $$
        sleep 5
        log "not reached"
        ;;
    child)
        MODE=success exec_script "${BASH_SOURCE[0]}" -x child_argument
        RETURNVAL=$?
        log "child returned ${RETURNVAL}"
        MODE=fail exec_script "${BASH_SOURCE[0]}" || log "failing child returned $?"
        exit_logic 0
        ;;
    helper|missing_dependency)
        log "helper value: ${HELPER_VALUE:-none}"
        exit_logic 0
        ;;
    quiet)
        exit_logic_quiet 0
        ;;
    usage)
        usage
        exit_logic 0
        ;;
    exit_functions)
        generate_temp_file TMP_FILE "test"
        first_exit_function() { log "first exit function (exit code ${BAMF_EXIT_CODE})"; }
        second_exit_function() { log "second exit function, temp file still there: $([[ -f "${TMP_FILE}" ]] && echo yes)"; return 3; }
        add_exit_function first_exit_function
        add_exit_function second_exit_function
        add_exit_function second_exit_function
        add_exit_function no_such_function || log "refused an unknown function"
        case "${EXIT_HOW:-logic}" in
            logic)  exit_logic "${E_OBJECT_NOT_FOUND}";;
            set_e)  set -e; false;;
            signal) kill -TERM $$; sleep 5;;
        esac
        ;;
esac
