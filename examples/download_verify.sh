#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  download_verify.sh
# purpose: Downloads a release archive, checks its SHA-256 checksum, and extracts it
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# What this example shows:
#   - helper files: curl and tar, loaded by name
#   - curl_request: retries network errors and 408/429/5xx responses, never a 404
#   - checking a download against a checksum you got from somewhere you trust, before using it
#   - the download lands in a temporary file, so a failed run leaves nothing half-written behind
#
# Try it:
#   examples/download_verify.sh -u https://example.com/app-1.2.3.tar.gz -s <sha256> -d /tmp/app

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="download_verify"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-u <url> : Archive to download (.tar.gz, .tgz, .tar.bz2, .tar.xz or .tar)'
    '-s <sha256> : Expected SHA-256 checksum of the archive'
    '-d <directory> : Directory to extract into'
)

###------------------------------------------------------------------------------------------------
## Load BAMF
# shellcheck source=../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../bamf.inc" || exit 3
REQUIRED_SOURCE_FILES+=( curl tar )
REQUIRED_EXECUTABLES+=( 'sha256sum' )

###------------------------------------------------------------------------------------------------
## Variables
URL=""
EXPECTED_SHA256=""
DIRECTORY_TARGET=""
REQUIRED_ARGUMENTS=( 'url' 'expected_sha256' 'directory_target' )

###------------------------------------------------------------------------------------------------
## Main
while getopts "hVu:s:d:" OPTION; do
    case "${OPTION}" in
        u) URL="${OPTARG}";;
        s) EXPECTED_SHA256="${OPTARG,,}";;
        d) DIRECTORY_TARGET="${OPTARG}";;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic

line_break
log_highlight "Initialize"
verify_array_key_values "REQUIRED_ARGUMENTS[@]" || exit_logic "${E_BAD_ARGS}" "-u, -s and -d are required"
if [[ ! "${EXPECTED_SHA256}" =~ ^[0-9a-f]{64}$ ]]; then
    exit_logic "${E_BAD_ARGS}" "the checksum must be 64 hexadecimal characters"
fi
# Keep the archive's own name, so tar can tell the compression from the extension
ARCHIVE_NAME="${URL##*/}"
ARCHIVE_NAME="${ARCHIVE_NAME%%[?#]*}"
generate_temp_directory DIRECTORY_DOWNLOAD "download"
FILE_ARCHIVE="${DIRECTORY_DOWNLOAD}/${ARCHIVE_NAME}"

line_break
log_highlight "Download"
curl_request "${URL}" "${FILE_ARCHIVE}" || exit_logic $?

line_break
log_highlight "Verify"
return_file_sha256sum "${FILE_ARCHIVE}" ACTUAL_SHA256
log "- Expected: [${EXPECTED_SHA256}]"
log "- Actual:   [${ACTUAL_SHA256}]"
if [[ "${ACTUAL_SHA256}" != "${EXPECTED_SHA256}" ]]; then
    # Never extract or run a file that does not match: it is damaged, or not the file you think
    exit_logic "${E_CHECKSUM_MISMATCH}" "the download does not match the expected checksum"
fi
log_status PASS "checksum matches"

line_break
log_highlight "Extract"
tar_extract "${FILE_ARCHIVE}" "${DIRECTORY_TARGET}" || exit_logic $?

log_success "Downloaded, verified and extracted [${ARCHIVE_NAME}] into [${DIRECTORY_TARGET}]"
exit_logic 0
