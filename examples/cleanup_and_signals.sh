#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  cleanup_and_signals.sh
# purpose: Shows that a BAMF script cleans up after itself however it ends: success, error, or Ctrl+C
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# What this example shows:
#   - temporary files that are always removed
#   - add_exit_function: the script's own cleanup (here, a lock file outside the temporary
#     directory) runs on every way out
#   - set -euo pipefail: a failing command ends the script, and the cleanup still runs
#
# Try it:
#   examples/cleanup_and_signals.sh          press Ctrl+C while it waits
#   examples/cleanup_and_signals.sh -e       a command fails partway, under set -e
#   examples/cleanup_and_signals.sh -s 0     finishes at once
# Each time, the lock file is gone afterwards.

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="cleanup_demo"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-s <seconds> : How long to work (optional, defaults to 30)'
    '-e : Fail partway, with a command that returns an error'
    '-l <file> : Lock file (optional, defaults to cleanup_demo.lock in the current directory)'
)

###------------------------------------------------------------------------------------------------
## Load BAMF
# BAMF works under the strictest settings: stop on errors (-e), on unset variables (-u), and on a
# failure anywhere in a pipeline (pipefail)
set -euo pipefail
# shellcheck source=../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../bamf.inc" || exit 3

###------------------------------------------------------------------------------------------------
## Variables
SECONDS_TO_WORK=30
FAIL=no
FILE_LOCK="${PWD}/cleanup_demo.lock"

###------------------------------------------------------------------------------------------------
## Functions

## FUNCTION: release_lock()
## - Exit function: removes the lock file, so the next run can start
function release_lock() {
    rm -f -- "${FILE_LOCK:?}"
    log "Released the lock [${FILE_LOCK}] (the script is exiting with ${BAMF_EXIT_CODE})"
}

###------------------------------------------------------------------------------------------------
## Main
while getopts "hVs:el:" OPTION; do
    case "${OPTION}" in
        s) SECONDS_TO_WORK="${OPTARG}";;
        e) FAIL=yes;;
        l) FILE_LOCK="${OPTARG}";;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic

line_break
log_highlight "Initialize"
if ! is_int "${SECONDS_TO_WORK}"; then
    exit_logic "${E_BAD_ARGS}" "seconds must be a number"
fi
# set -o noclobber makes > fail if the file exists: creating the lock and checking it is one step,
# so two copies of the script cannot both get it
if ! ( set -o noclobber; printf '%s\n' "$$" > "${FILE_LOCK}" ) 2>/dev/null; then
    exit_logic "${E_SCRIPT_ALREADY_RUNNING}" "lock file exists [${FILE_LOCK}]"
fi
add_exit_function release_lock
log "- Took the lock [${FILE_LOCK}]"
generate_temp_file FILE_WORK "work in progress"
log "- Temporary file [${FILE_WORK}]"

line_break
log_highlight "Execution"
for (( SECOND_NUMBER=1; SECOND_NUMBER<=SECONDS_TO_WORK; SECOND_NUMBER++ )); do
    printf 'step %s\n' "${SECOND_NUMBER}" >> "${FILE_WORK}"
    log "Working ${SECOND_NUMBER} of ${SECONDS_TO_WORK} (press Ctrl+C to stop)"
    if option_enabled FAIL && (( SECOND_NUMBER == 2 )); then
        log_warning "Running a command that fails (grep finds nothing)"
        grep "something that is not there" "${FILE_WORK}" > /dev/null
    fi
    sleep 1
done

log_success "Worked for ${SECONDS_TO_WORK} seconds"
exit_logic 0
