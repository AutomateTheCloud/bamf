#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  package.sh
# purpose: Package step: fails on request, to show a failure traveling back to the pipeline
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# A step of examples/pipeline.sh. With -f it fails with E_OBJECT_NOT_FOUND, and the pipeline's log
# names that code.

# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="package"
# shellcheck source=../../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../../bamf.inc" || exit 3

FAIL=no
while getopts "hVf" OPTION; do
    case "${OPTION}" in
        f) FAIL=yes;;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic
if option_enabled FAIL; then
    log_error "The build output to package was not found"
    exit_logic "${E_OBJECT_NOT_FOUND}"
fi
log_success "Package finished"
exit_logic 0
