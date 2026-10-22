###------------------------------------------------------------------------------------------------
# script:  bamf.git.inc
# purpose: BAMF helper file for Git and GitHub: tag and push a commit, check a clone is up to date, list an organization's repositories
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( git )
#          start_logic
#          git_tag_and_push . "v${VERSION}" || exit_logic $?
#
# function list:
#          - git_github_repository_list
#          - git_is_repo_up_to_date
#          - git_tag_and_push
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Dependencies
REQUIRED_EXECUTABLES+=('git')

###------------------------------------------------------------------------------------------------
## FUNCTION: git_github_repository_list()
## - Writes the full names (owner/name) of every repository in a GitHub organization to a file, one
##   per line, sorted
## - The token is read from an environment variable and sent to curl in a 0600 header file, never
##   on the command line
## - Arguments
##   - $1: Organization name
##   - $2: Output file
##   - $3: Name of the environment variable holding the GitHub token (optional, defaults to GITHUB_TOKEN)
function git_github_repository_list() {
    local FUNCTION_DESCRIPTION="GitHub (Repository List)"
    local TMP_ORGANIZATION="${1:-}"
    local TMP_FILE_OUTPUT="${2:-}"
    local TMP_TOKEN_VARIABLE="${3:-GITHUB_TOKEN}"
    local TMP_TOKEN=""
    local TMP_FILE_HEADERS=""
    local TMP_FILE_RESPONSE=""
    local TMP_FILE_NAMES=""
    local TMP_FILE_ERROR=""
    local TMP_PAGE=1
    local TMP_PAGE_COUNT=0
    local TMP_LINE=""
    if [[ -z "${TMP_ORGANIZATION}" || -z "${TMP_FILE_OUTPUT}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: organization and output file are required"
        return $E_BAD_ARGS
    fi
    return_variable_value "${TMP_TOKEN_VARIABLE}" TMP_TOKEN || return $?
    if [[ -z "${TMP_TOKEN}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: no token in [${TMP_TOKEN_VARIABLE}]"
        return $E_BAD_ARGS
    fi
    if ! command -v curl >/dev/null 2>&1; then
        log_error "${FUNCTION_DESCRIPTION}: curl is not installed"
        return $E_MISSING_DEPENDENCIES
    fi
    generate_temp_file_with_content TMP_FILE_HEADERS "$(printf 'Authorization: Bearer %s\nAccept: application/vnd.github+json\nX-GitHub-Api-Version: 2022-11-28' "${TMP_TOKEN}")" "github headers"
    generate_temp_file TMP_FILE_RESPONSE "github response"
    generate_temp_file TMP_FILE_NAMES "repository names"
    generate_temp_file TMP_FILE_ERROR "curl error output"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_ORGANIZATION}]"
    while :; do
        if ! command curl --silent --show-error --fail --retry 3 --header "@${TMP_FILE_HEADERS}" --output "${TMP_FILE_RESPONSE}" \
            "https://api.github.com/orgs/${TMP_ORGANIZATION}/repos?type=all&per_page=100&page=${TMP_PAGE}" 2>"${TMP_FILE_ERROR}"; then
            log_add_from_file "${TMP_FILE_ERROR}" "${FUNCTION_DESCRIPTION}: curl error output"
            log_error "${FUNCTION_DESCRIPTION}: the GitHub API request failed (page ${TMP_PAGE})"
            return $E_GIT_FAILURE
        fi
        TMP_PAGE_COUNT=0
        while IFS= read -r TMP_LINE; do
            TMP_LINE="${TMP_LINE#*\"full_name\"}"
            TMP_LINE="${TMP_LINE#*\"}"
            printf '%s\n' "${TMP_LINE%%\"*}" >> "${TMP_FILE_NAMES}"
            TMP_PAGE_COUNT=$(( TMP_PAGE_COUNT + 1 ))
        done < <(command grep -o '"full_name": *"[^"]*"' "${TMP_FILE_RESPONSE}")
        (( TMP_PAGE_COUNT < 100 )) && break
        TMP_PAGE=$(( TMP_PAGE + 1 ))
    done
    if ! LC_ALL=C command sort -u "${TMP_FILE_NAMES}" > "${TMP_FILE_OUTPUT}"; then
        log_error "${FUNCTION_DESCRIPTION}: could not write file [${TMP_FILE_OUTPUT}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    log_add_from_file "${TMP_FILE_OUTPUT}" "${FUNCTION_DESCRIPTION}: repositories"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: git_is_repo_up_to_date()
## - Fetches from the remote and returns true if the current branch matches its upstream branch
## - Returns 1 if it does not, E_GIT_FAILURE if it cannot tell (no upstream, fetch failed)
## - Arguments
##   - $1: Repository directory (optional, defaults to the current directory)
function git_is_repo_up_to_date() {
    local TMP_DIRECTORY="${1:-.}"
    local TMP_LOCAL=""
    local TMP_UPSTREAM=""
    if ! command git -C "${TMP_DIRECTORY}" fetch --quiet >/dev/null 2>&1; then
        log_warning "Git (Up to Date): fetch failed [${TMP_DIRECTORY}]"
        return $E_GIT_FAILURE
    fi
    if ! TMP_LOCAL="$(command git -C "${TMP_DIRECTORY}" rev-parse HEAD 2>/dev/null)" \
        || ! TMP_UPSTREAM="$(command git -C "${TMP_DIRECTORY}" rev-parse '@{upstream}' 2>/dev/null)"; then
        log_warning "Git (Up to Date): the current branch has no upstream branch [${TMP_DIRECTORY}]"
        return $E_GIT_FAILURE
    fi
    [[ "${TMP_LOCAL}" == "${TMP_UPSTREAM}" ]]
}

###------------------------------------------------------------------------------------------------
## FUNCTION: git_tag_and_push()
## - Creates an annotated tag on a commit and pushes it to a remote
## - An existing tag is not moved unless asked: moving a published tag changes what the version
##   means for everyone who has already fetched it
## - Arguments
##   - $1: Repository directory
##   - $2: Tag (example: v1.2.3)
##   - $3: Commit (optional, defaults to HEAD)
##   - $4: Message (optional, defaults to the tag)
##   - $5: Remote (optional, defaults to origin)
##   - $6: Move the tag if it exists, yes or no (optional, defaults to no)
##   - $7: Tagger name (optional, defaults to Git's configuration)
##   - $8: Tagger email (optional, defaults to Git's configuration)
function git_tag_and_push() {
    local FUNCTION_DESCRIPTION="Git (Tag and Push)"
    local TMP_DIRECTORY="${1:-}"
    local TMP_TAG="${2:-}"
    local TMP_COMMIT="${3:-HEAD}"
    local TMP_MESSAGE="${4:-${2:-}}"
    local TMP_REMOTE="${5:-origin}"
    local TMP_FILE_LOG=""
    local -a TMP_IDENTITY=()
    local -a TMP_FORCE=()
    if [[ -z "${TMP_DIRECTORY}" || -z "${TMP_TAG}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: repository directory and tag are required"
        return $E_BAD_ARGS
    fi
    if ! command git check-ref-format "refs/tags/${TMP_TAG}"; then
        log_error "${FUNCTION_DESCRIPTION}: not a valid tag name [${TMP_TAG}]"
        return $E_BAD_ARGS
    fi
    if ! command git -C "${TMP_DIRECTORY}" rev-parse --git-dir >/dev/null 2>&1; then
        log_error "${FUNCTION_DESCRIPTION}: not a Git repository [${TMP_DIRECTORY}]"
        return $E_OBJECT_NOT_FOUND
    fi
    case "${6:-no}" in y|yes|Y|YES|1|true|TRUE) TMP_FORCE=( --force );; esac
    [[ -n "${7:-}" ]] && TMP_IDENTITY+=( -c "user.name=${7}" )
    [[ -n "${8:-}" ]] && TMP_IDENTITY+=( -c "user.email=${8}" )
    generate_temp_file TMP_FILE_LOG "git output"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_TAG}] on [${TMP_COMMIT}] -> [${TMP_REMOTE}]"
    if ! command git -C "${TMP_DIRECTORY}" "${TMP_IDENTITY[@]+"${TMP_IDENTITY[@]}"}" tag --annotate "${TMP_FORCE[@]+"${TMP_FORCE[@]}"}" \
        --message "${TMP_MESSAGE}" "${TMP_TAG}" "${TMP_COMMIT}" >"${TMP_FILE_LOG}" 2>&1; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: git output"
        log_error "${FUNCTION_DESCRIPTION}: could not create the tag"
        return $E_GIT_FAILURE
    fi
    if ! command git -C "${TMP_DIRECTORY}" push "${TMP_FORCE[@]+"${TMP_FORCE[@]}"}" "${TMP_REMOTE}" "refs/tags/${TMP_TAG}" >"${TMP_FILE_LOG}" 2>&1; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: git output"
        log_error "${FUNCTION_DESCRIPTION}: could not push the tag"
        return $E_GIT_FAILURE
    fi
    log "${FUNCTION_DESCRIPTION}: pushed [${TMP_TAG}]"
    return 0
}
