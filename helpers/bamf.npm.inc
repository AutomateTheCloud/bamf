###------------------------------------------------------------------------------------------------
# script:  bamf.npm.inc
# purpose: BAMF helper file for npm: read package.json, check a registry for a version, write .npmrc
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( npm )
#          start_logic
#          npm_package_value PACKAGE_VERSION version . || exit_logic $?
#          if npm_does_version_exist my-package "${PACKAGE_VERSION}"; then exit_logic "${E_NPM_VERSION_VALIDATION_FAILURE}"; fi
#
# requirements:
#          - npm 7.20 or newer (npm pkg)
#
# function list:
#          - npm_does_version_exist
#          - npm_generate_npmrc
#          - npm_package_value
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Dependencies
REQUIRED_EXECUTABLES+=('npm')

###------------------------------------------------------------------------------------------------
## Exit Codes
define_exit_code E_NPM_FAILURE                    113 "NPM execution failed"
define_exit_code E_NPM_VERSION_VALIDATION_FAILURE 114 "NPM Version Validation failure. Package Version already exists"
define_exit_code E_NPM_PACKAGE_DOES_NOT_EXIST     115 "NPM Package does not exist"

###------------------------------------------------------------------------------------------------
## FUNCTION: npm_does_version_exist()
## - Returns true if a version of a package is published in the registry (npm view), and
##   E_NPM_PACKAGE_DOES_NOT_EXIST if it is not, for example to stop a release that would publish
##   a version twice
## - Arguments
##   - $1: Package name
##   - $2: Version
##   - $3: Directory to run npm in, for its .npmrc (optional, defaults to the current directory)
function npm_does_version_exist() {
    local FUNCTION_DESCRIPTION="npm (Does Version Exist)"
    local TMP_NAME="${1:-}"
    local TMP_VERSION="${2:-}"
    local TMP_DIRECTORY="${3:-.}"
    local TMP_FOUND=""
    if [[ -z "${TMP_NAME}" || -z "${TMP_VERSION}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: package name and version are required"
        return $E_BAD_ARGS
    fi
    if [[ ! -d "${TMP_DIRECTORY}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: directory not found [${TMP_DIRECTORY}]"
        return $E_OBJECT_NOT_FOUND
    fi
    # a subshell, so the cd does not change the calling script's directory
    TMP_FOUND="$(cd "${TMP_DIRECTORY}" && command npm view "${TMP_NAME}@${TMP_VERSION}" version 2>/dev/null)" || TMP_FOUND=""
    if [[ -n "${TMP_FOUND}" ]]; then
        log "${FUNCTION_DESCRIPTION}: [${TMP_NAME}@${TMP_VERSION}] is published"
        return 0
    fi
    log "${FUNCTION_DESCRIPTION}: [${TMP_NAME}@${TMP_VERSION}] is not published"
    return $E_NPM_PACKAGE_DOES_NOT_EXIST
}

###------------------------------------------------------------------------------------------------
## FUNCTION: npm_generate_npmrc()
## - Writes an .npmrc file that points npm at a registry
## - With a token variable name, adds the registry's auth token as a reference, ${NAME}, which npm
##   reads from the environment when it runs, so the token itself is never written to the file
## - Arguments
##   - $1: Registry URL (example: https://npm.pkg.github.com/)
##   - $2: Directory to write the file in
##   - $3: File name (optional, defaults to .npmrc)
##   - $4: Name of the environment variable holding the auth token (optional, example: NPM_TOKEN)
function npm_generate_npmrc() {
    local FUNCTION_DESCRIPTION="npm (Generate .npmrc)"
    local TMP_REGISTRY="${1:-}"
    local TMP_DIRECTORY="${2:-}"
    local TMP_FILE="${2:-}/${3:-.npmrc}"
    local TMP_TOKEN_VARIABLE="${4:-}"
    local TMP_REGISTRY_PATH=""
    if [[ ! "${TMP_REGISTRY}" =~ ^https?:// ]]; then
        log_error "${FUNCTION_DESCRIPTION}: registry must be an http(s) URL [${TMP_REGISTRY}]"
        return $E_BAD_ARGS
    fi
    if [[ -z "${TMP_DIRECTORY}" || ! -d "${TMP_DIRECTORY}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: directory not found [${TMP_DIRECTORY}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if [[ -n "${TMP_TOKEN_VARIABLE}" && ! "${TMP_TOKEN_VARIABLE}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        log_error "${FUNCTION_DESCRIPTION}: invalid token variable name [${TMP_TOKEN_VARIABLE}]"
        return $E_BAD_ARGS
    fi
    [[ "${TMP_REGISTRY}" == */ ]] || TMP_REGISTRY+="/"
    {
        printf 'registry=%s\n' "${TMP_REGISTRY}"
        if [[ -n "${TMP_TOKEN_VARIABLE}" ]]; then
            # npm matches the token to the registry by host and path, without the scheme
            TMP_REGISTRY_PATH="${TMP_REGISTRY#*://}"
            printf '//%s:_authToken=${%s}\n' "${TMP_REGISTRY_PATH}" "${TMP_TOKEN_VARIABLE}"
        fi
    } > "${TMP_FILE}" || { log_error "${FUNCTION_DESCRIPTION}: could not write [${TMP_FILE}]"; return $E_OBJECT_FAILED_TO_CREATE; }
    log "${FUNCTION_DESCRIPTION}: wrote [${TMP_FILE}] (registry::${TMP_REGISTRY}${TMP_TOKEN_VARIABLE:+ / token::\$\{${TMP_TOKEN_VARIABLE}\}})"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: npm_package_value()
## - Returns a field from a package.json (npm pkg get), such as name or version; nested fields use
##   dots (example: repository.url). A list or object is returned as JSON.
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Field
##   - $3: Directory holding package.json (optional, defaults to the current directory)
function npm_package_value() {
    local FUNCTION_DESCRIPTION="npm (Package Value)"
    local __NP_FIELD="${2:-}"
    local __NP_DIRECTORY="${3:-.}"
    local __NP_VALUE=""
    if [[ -z "${__NP_FIELD}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: field not specified"
        return $E_BAD_ARGS
    fi
    if [[ ! -f "${__NP_DIRECTORY}/package.json" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: package.json not found in [${__NP_DIRECTORY}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if ! __NP_VALUE="$(cd "${__NP_DIRECTORY}" && command npm pkg get "${__NP_FIELD}" 2>&1)"; then
        log_error "${FUNCTION_DESCRIPTION}: npm pkg get failed: ${__NP_VALUE}"
        return $E_NPM_FAILURE
    fi
    # npm prints JSON: an empty object for a missing field, a quoted string for a string
    case "${__NP_VALUE}" in
        "{}") __NP_VALUE="";;
        \"*\") json_extract_from_string __NP_VALUE value "{\"value\": ${__NP_VALUE}}";;
    esac
    _bamf_return "${1:-}" "${__NP_VALUE}"
}
