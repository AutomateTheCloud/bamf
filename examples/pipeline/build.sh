#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  build.sh
# purpose: Build step: writes a small build manifest
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# A step of examples/pipeline.sh. It is an ordinary BAMF script: it can also be run on its own.

# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="build"
# shellcheck source=../../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../../bamf.inc" || exit 3

while getopts "hVf" OPTION; do
    case "${OPTION}" in
        f) ;;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic
generate_temp_file FILE_MANIFEST "build manifest"
printf 'built_by=%s\nbuilt_at=%(%Y-%m-%dT%H:%M:%S)T\n' "$(return_hostname)" -1 > "${FILE_MANIFEST}"
log_add_from_file "${FILE_MANIFEST}" "build manifest"
log_success "Build finished"
exit_logic 0
