###------------------------------------------------------------------------------------------------
# script:  bamf.tar.inc
# purpose: BAMF helper file for tar: create and extract archives, compressed or not
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( tar )
#          start_logic
#          tar_create /backups/site.tar.gz /var/www/site || exit_logic $?
#          tar_extract /backups/site.tar.gz /srv/restore || exit_logic $?
#
# notes:
#          - the compression comes from the archive's extension: .tar, .tar.gz or .tgz (gzip),
#            .tar.bz2 or .tbz2 (bzip2, or lbzip2 when installed, which uses every CPU), .tar.xz or .txz (xz)
#          - extraction detects the compression by itself
#
# function list:
#          - tar_create
#          - tar_extract
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Dependencies
REQUIRED_EXECUTABLES+=('tar')

###------------------------------------------------------------------------------------------------
## FUNCTION: tar_create()
## - Creates an archive of a directory or a file
## - Relative (the default): a directory's contents are stored at the top of the archive, and a file
##   is stored under its own name, so the archive can be extracted anywhere
## - Absolute: entries keep their full path (without the leading /), to be restored to the same place
##   with tar_extract <archive> /
## - A failed archive is removed, so a partial file is never left behind
## - Arguments
##   - $1: Archive to create (the extension picks the compression)
##   - $2: Directory or file to archive
##   - $3: Paths, relative or absolute (optional, defaults to relative)
function tar_create() {
    local FUNCTION_DESCRIPTION="Tar (Create)"
    local TMP_ARCHIVE="${1:-}"
    local TMP_TARGET="${2:-}"
    local TMP_PATHS="${3:-relative}"
    local TMP_FILE_LOG=""
    local TMP_RETURNVAL=0
    local -a TMP_COMPRESSION=()
    local -a TMP_CONTENT=()
    if [[ -z "${TMP_ARCHIVE}" || -z "${TMP_TARGET}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: archive and target are required"
        return $E_BAD_ARGS
    fi
    if [[ ! -e "${TMP_TARGET}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: target not found [${TMP_TARGET}]"
        return $E_OBJECT_NOT_FOUND
    fi
    _tar_compression TMP_COMPRESSION "${TMP_ARCHIVE}" || return $?
    TMP_TARGET="${TMP_TARGET%/}"
    case "${TMP_PATHS}" in
        relative)
            if [[ -d "${TMP_TARGET}" ]]; then
                TMP_CONTENT=( -C "${TMP_TARGET:-/}" . )
            elif [[ "${TMP_TARGET}" == */* ]]; then
                TMP_CONTENT=( -C "${TMP_TARGET%/*}" "${TMP_TARGET##*/}" )
            else
                TMP_CONTENT=( "${TMP_TARGET}" )
            fi;;
        absolute)
            # GNU tar stores the path without its leading /
            TMP_CONTENT=( "$(cd "$(dirname "${TMP_TARGET}")" && pwd)/${TMP_TARGET##*/}" );;
        *)
            log_error "${FUNCTION_DESCRIPTION}: paths must be relative or absolute [${TMP_PATHS}]"
            return $E_BAD_ARGS;;
    esac
    generate_temp_file TMP_FILE_LOG "tar output"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_TARGET}] -> [${TMP_ARCHIVE}] (${TMP_PATHS} paths)"
    command tar "${TMP_COMPRESSION[@]+"${TMP_COMPRESSION[@]}"}" -cf "${TMP_ARCHIVE}" "${TMP_CONTENT[@]}" >"${TMP_FILE_LOG}" 2>&1 || TMP_RETURNVAL=$?
    if (( TMP_RETURNVAL != 0 )); then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: tar output"
        command rm -f -- "${TMP_ARCHIVE:?}"
        log_error "${FUNCTION_DESCRIPTION}: tar failed (returned::${TMP_RETURNVAL})"
        return $E_FILE_CONVERSION_FAILURE
    fi
    log "${FUNCTION_DESCRIPTION}: created [${TMP_ARCHIVE}] ($(return_filesize_of_file "${TMP_ARCHIVE}"))"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: tar_extract()
## - Extracts an archive into a directory (created if it does not exist)
## - GNU tar refuses entries that would land outside the directory (absolute paths and "..")
## - Arguments
##   - $1: Archive
##   - $2: Directory to extract into (use / to restore an archive made with absolute paths)
function tar_extract() {
    local FUNCTION_DESCRIPTION="Tar (Extract)"
    local TMP_ARCHIVE="${1:-}"
    local TMP_DIRECTORY="${2:-}"
    local TMP_FILE_LOG=""
    local TMP_RETURNVAL=0
    if [[ -z "${TMP_ARCHIVE}" || -z "${TMP_DIRECTORY}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: archive and directory are required"
        return $E_BAD_ARGS
    fi
    if [[ ! -f "${TMP_ARCHIVE}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: archive not found [${TMP_ARCHIVE}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if ! command mkdir -p "${TMP_DIRECTORY}" 2>/dev/null; then
        log_error "${FUNCTION_DESCRIPTION}: could not create directory [${TMP_DIRECTORY}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    generate_temp_file TMP_FILE_LOG "tar output"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_ARCHIVE}] -> [${TMP_DIRECTORY}]"
    command tar -xf "${TMP_ARCHIVE}" -C "${TMP_DIRECTORY}" >"${TMP_FILE_LOG}" 2>&1 || TMP_RETURNVAL=$?
    if (( TMP_RETURNVAL != 0 )); then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: tar output"
        log_error "${FUNCTION_DESCRIPTION}: tar failed (returned::${TMP_RETURNVAL})"
        return $E_FILE_CONVERSION_FAILURE
    fi
    log "${FUNCTION_DESCRIPTION}: extracted"
    return 0
}

###------------------------------------------------------------------------------------------------
# Internal helpers (not part of the public function list; names start with an underscore)

# _tar_compression <array name> <archive>: the tar options for the archive's compression
function _tar_compression() {
    case "${2,,}" in
        *.tar)              eval "${1}=()";;
        *.tar.gz|*.tgz)     eval "${1}=( -z )";;
        *.tar.xz|*.txz)     eval "${1}=( -J )";;
        *.tar.bz2|*.tbz2)
            if command -v lbzip2 >/dev/null 2>&1; then
                eval "${1}=( -I lbzip2 )"
            else
                eval "${1}=( -j )"
            fi;;
        *)
            log_error "Tar: cannot tell the compression from the archive name [${2}] (expected .tar, .tar.gz, .tgz, .tar.bz2, .tbz2, .tar.xz or .txz)"
            return $E_BAD_ARGS;;
    esac
}
