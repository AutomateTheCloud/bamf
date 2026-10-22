#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  pipeline.sh
# purpose: Runs a series of step scripts in order, stopping at the first one that fails
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# What this example shows:
#   - exec_script: run another BAMF script as a child; on a terminal its log lines use a softer
#     color palette, so the parent's lines stand out
#   - exit codes travel back: a child's E_* code is named in the parent's log
#   - log_status: a one-line summary per step
#
# Try it:
#   examples/pipeline.sh            both steps pass
#   examples/pipeline.sh -f         the second step fails, and the pipeline stops with its exit code

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="pipeline"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-f : Make the second step fail, to see how a failure travels back'
)

###------------------------------------------------------------------------------------------------
## Load BAMF
DIRECTORY_EXAMPLES="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../bamf.inc
source "${DIRECTORY_EXAMPLES}/../bamf.inc" || exit 3

###------------------------------------------------------------------------------------------------
## Variables
STEPS=( "${DIRECTORY_EXAMPLES}/pipeline/build.sh" "${DIRECTORY_EXAMPLES}/pipeline/package.sh" )
STEP_ARGUMENTS=()

###------------------------------------------------------------------------------------------------
## Main
while getopts "hVf" OPTION; do
    case "${OPTION}" in
        f) STEP_ARGUMENTS=( -f );;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic

line_break
log_highlight "Execution"
for STEP in "${STEPS[@]}"; do
    RETURNVAL=0
    exec_script "${STEP}" "${STEP_ARGUMENTS[@]+"${STEP_ARGUMENTS[@]}"}" || RETURNVAL=$?
    if (( RETURNVAL != 0 )); then
        log_status FAIL "$(return_script_purpose "${STEP}")"
        # Ending with the child's code tells whoever runs the pipeline exactly what went wrong
        exit_logic "${RETURNVAL}" "step [${STEP##*/}] failed"
    fi
    log_status PASS "$(return_script_purpose "${STEP}")"
done

log_success "Every step passed"
exit_logic 0
