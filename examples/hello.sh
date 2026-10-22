#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  hello.sh
# purpose: A first BAMF script: logging, a temporary file, and a clean exit
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="hello"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-n <name> : Who to greet (optional, defaults to world)'
)

###------------------------------------------------------------------------------------------------
## Load BAMF
# shellcheck source=../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../bamf.inc" || exit 3

###------------------------------------------------------------------------------------------------
## Main
NAME="world"
while getopts "hVn:" OPTION; do
    case "${OPTION}" in
        n) NAME="${OPTARG}";;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic

line_break
log_highlight "Initialize"
if is_empty "${NAME}"; then
    log_error "A name is required"
    exit_logic "${E_BAD_ARGS}"
fi

line_break
log_highlight "Execution"
log "Hello, ${NAME}"

# Removed automatically when the script exits, however it exits
generate_temp_file FILE_GREETING "greeting"
printf 'Hello, %s\nfrom %s\n' "${NAME}" "$(return_hostname)" > "${FILE_GREETING}"
log_add_from_file "${FILE_GREETING}" "greeting"

log_success "Said hello"
exit_logic 0
