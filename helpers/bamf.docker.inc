###------------------------------------------------------------------------------------------------
# script:  bamf.docker.inc
# purpose: BAMF helper file for Docker: install Docker and Docker Compose, run Compose projects, log in to Amazon ECR, and clean up
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( docker )
#          start_logic
#          docker_compose_up /srv/app/docker-compose.yaml /srv/app/docker.env || exit_logic $?
#
# notes:
#          - Docker Compose v2 (the "docker compose" plugin) is used when it is installed, otherwise
#            the older standalone docker-compose
#          - loads bamf.pkg.inc, which docker_install uses
#
# function list:
#          - docker_compose_down
#          - docker_compose_install
#          - docker_compose_up
#          - docker_ecr_login
#          - docker_install
#          - docker_prune
#          - docker_reset
#          - docker_reset_images
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Dependencies
source_include pkg

###------------------------------------------------------------------------------------------------
## Exit Codes
define_exit_code E_DOCKER_FAILURE 101 "Failure related to Docker"

###------------------------------------------------------------------------------------------------
## Variables
declare -g DOCKER_COMPOSE_PLUGIN_DIRECTORY="${DOCKER_COMPOSE_PLUGIN_DIRECTORY:-/usr/local/lib/docker/cli-plugins}"

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_compose_down()
## - Stops and removes the containers of a Docker Compose project
## - Arguments
##   - $1: Compose file
##   - $2: Environment file (optional)
function docker_compose_down() {
    _docker_compose_project "Docker Compose (Down)" "${1:-}" "${2:-}" down
}

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_compose_install()
## - Installs a pinned version of the Docker Compose v2 plugin from its GitHub release, for the
##   machine's architecture, after checking the download against its SHA-256 checksum
## - Pass the expected checksum to check against a value you trust; without it, the checksum file
##   published with the release is used (which catches a damaged download, not a tampered release)
## - Does nothing if "docker compose" already works
## - Arguments
##   - $1: Version (example: v2.29.7)
##   - $2: Expected SHA-256 of the binary (optional)
function docker_compose_install() {
    local FUNCTION_DESCRIPTION="Docker Compose (Install)"
    local TMP_VERSION="${1:-}"
    local TMP_SHA256="${2:-}"
    local TMP_ARCHITECTURE=""
    local TMP_URL=""
    local TMP_FILE_BINARY=""
    local TMP_FILE_CHECKSUM=""
    local TMP_ACTUAL=""
    if [[ ! "${TMP_VERSION}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
        log_error "${FUNCTION_DESCRIPTION}: version must look like v2.29.7 [${TMP_VERSION}]"
        return $E_BAD_ARGS
    fi
    if command docker compose version >/dev/null 2>&1; then
        log_notice "${FUNCTION_DESCRIPTION}: already installed, skipping ($(command docker compose version --short 2>/dev/null))"
        return 0
    fi
    for TMP_ACTUAL in curl sha256sum; do
        if ! command -v "${TMP_ACTUAL}" >/dev/null 2>&1; then
            log_error "${FUNCTION_DESCRIPTION}: ${TMP_ACTUAL} is not installed"
            return $E_MISSING_DEPENDENCIES
        fi
    done
    TMP_ARCHITECTURE="$(command uname -m)"
    TMP_URL="https://github.com/docker/compose/releases/download/${TMP_VERSION}/docker-compose-linux-${TMP_ARCHITECTURE}"
    generate_temp_file TMP_FILE_BINARY "docker compose binary"
    log_notice "${FUNCTION_DESCRIPTION}: downloading [${TMP_URL}]"
    if ! command curl --silent --show-error --fail --location --retry 3 -o "${TMP_FILE_BINARY}" "${TMP_URL}"; then
        log_error "${FUNCTION_DESCRIPTION}: download failed"
        return $E_CURL_FAILURE
    fi
    if [[ -z "${TMP_SHA256}" ]]; then
        generate_temp_file TMP_FILE_CHECKSUM "docker compose checksum"
        if ! command curl --silent --show-error --fail --location --retry 3 -o "${TMP_FILE_CHECKSUM}" "${TMP_URL}.sha256"; then
            log_error "${FUNCTION_DESCRIPTION}: could not download the checksum file"
            return $E_CURL_FAILURE
        fi
        read -r TMP_SHA256 _ < "${TMP_FILE_CHECKSUM}" || true
    fi
    TMP_ACTUAL="$(command sha256sum < "${TMP_FILE_BINARY}")" || TMP_ACTUAL=""
    TMP_ACTUAL="${TMP_ACTUAL%% *}"
    if [[ -z "${TMP_SHA256}" || "${TMP_ACTUAL}" != "${TMP_SHA256,,}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: checksum mismatch (expected::${TMP_SHA256:-none} / actual::${TMP_ACTUAL})"
        return $E_CHECKSUM_MISMATCH
    fi
    if ! command mkdir -p "${DOCKER_COMPOSE_PLUGIN_DIRECTORY}" || ! command install -m 0755 "${TMP_FILE_BINARY}" "${DOCKER_COMPOSE_PLUGIN_DIRECTORY}/docker-compose"; then
        log_error "${FUNCTION_DESCRIPTION}: could not install into [${DOCKER_COMPOSE_PLUGIN_DIRECTORY}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    log "${FUNCTION_DESCRIPTION}: installed ${TMP_VERSION} [${DOCKER_COMPOSE_PLUGIN_DIRECTORY}/docker-compose]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_compose_up()
## - Starts the containers of a Docker Compose project in the background (up -d)
## - Logs the Compose file and the output; the environment file is not logged, as it often holds secrets
## - Arguments
##   - $1: Compose file
##   - $2: Environment file (optional)
function docker_compose_up() {
    _docker_compose_project "Docker Compose (Up)" "${1:-}" "${2:-}" up -d
}

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_ecr_login()
## - Logs Docker in to an Amazon ECR registry with the current AWS credentials
## - The password goes from the AWS CLI to docker through a pipe (--password-stdin), never the
##   command line
## - Arguments
##   - $1: Registry (example: 123456789012.dkr.ecr.us-east-1.amazonaws.com)
##   - $2: Region (optional, defaults to the Region in the registry name)
function docker_ecr_login() {
    local FUNCTION_DESCRIPTION="Docker (ECR Login)"
    local TMP_REGISTRY="${1:-}"
    local TMP_REGION="${2:-}"
    local TMP_OUTPUT=""
    local TMP_RETURNVAL=0
    if [[ ! "${TMP_REGISTRY}" =~ ^[0-9]{12}\.dkr\.ecr\.([a-z0-9-]+)\.amazonaws\.com(\.cn)?$ ]]; then
        log_error "${FUNCTION_DESCRIPTION}: not an ECR registry name [${TMP_REGISTRY}]"
        return $E_BAD_ARGS
    fi
    [[ -n "${TMP_REGION}" ]] || TMP_REGION="${BASH_REMATCH[1]}"
    if ! command -v aws >/dev/null 2>&1; then
        log_error "${FUNCTION_DESCRIPTION}: the AWS CLI is not installed"
        return $E_MISSING_DEPENDENCIES
    fi
    generate_temp_file TMP_OUTPUT "docker login output"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_REGISTRY}]"
    # pipefail, so a failure of the AWS CLI is not hidden by docker; set in a subshell, so the
    # option does not change the rest of the calling script
    ( set -o pipefail
      command aws ecr get-login-password --region "${TMP_REGION}" 2>"${TMP_OUTPUT}" \
          | command docker login --username AWS --password-stdin "${TMP_REGISTRY}" >>"${TMP_OUTPUT}" 2>&1 ) || TMP_RETURNVAL=$?
    if (( TMP_RETURNVAL != 0 )); then
        log_add_from_file "${TMP_OUTPUT}" "${FUNCTION_DESCRIPTION}: output"
        log_error "${FUNCTION_DESCRIPTION}: login failed (returned::${TMP_RETURNVAL})"
        return $E_DOCKER_FAILURE
    fi
    log "${FUNCTION_DESCRIPTION}: logged in"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_install()
## - Installs Docker with the system package manager (if it is not installed), and starts it and
##   enables it at boot with systemctl
## - The default package is docker.io on apt-get systems (Debian, Ubuntu) and docker on dnf and yum
##   systems (Amazon Linux). On other distributions, pass the package names, after adding Docker's
##   own package repository.
## - Arguments
##   - $1+: Package names (optional)
function docker_install() {
    local FUNCTION_DESCRIPTION="Docker (Install)"
    local TMP_MANAGER=""
    local -a TMP_PACKAGES=( "$@" )
    if command -v docker >/dev/null 2>&1; then
        log_notice "${FUNCTION_DESCRIPTION}: already installed, skipping"
    else
        if [[ ${#TMP_PACKAGES[@]} -eq 0 ]]; then
            package_management_detect TMP_MANAGER || return $?
            if [[ "${TMP_MANAGER}" == "apt-get" ]]; then TMP_PACKAGES=( docker.io ); else TMP_PACKAGES=( docker ); fi
        fi
        log_notice "${FUNCTION_DESCRIPTION}: installing Docker"
        package_management_install_packages "${TMP_PACKAGES[@]}" || return $?
    fi
    if ! command -v systemctl >/dev/null 2>&1; then
        log_warning "${FUNCTION_DESCRIPTION}: systemctl not found, start Docker yourself"
        return 0
    fi
    log_notice "${FUNCTION_DESCRIPTION}: starting Docker"
    if ! command systemctl enable --now docker.service >/dev/null 2>&1; then
        log_error "${FUNCTION_DESCRIPTION}: could not start the docker service"
        return $E_SYSTEMCTL_FAILURE
    fi
    log "${FUNCTION_DESCRIPTION}: Docker is installed and running"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_prune()
## - Removes every stopped container, unused network, unused image and unused volume
## - Volumes hold data: this deletes the data of every volume no container is using
function docker_prune() {
    _docker_run "Docker (Prune)" "removing unused containers, networks, images and volumes" system prune --force --all --volumes
}

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_reset()
## - Stops and removes every container on the machine (not only one project's)
## - Arguments
##   - $1: Seconds each container gets to stop before it is killed (optional, Docker's default is 10)
function docker_reset() {
    local FUNCTION_DESCRIPTION="Docker (Reset)"
    local -a TMP_CONTAINERS=()
    if [[ -n "${1:-}" ]] && ! is_int "${1}"; then
        log_error "${FUNCTION_DESCRIPTION}: invalid timeout [${1}]"
        return $E_BAD_ARGS
    fi
    _docker_list TMP_CONTAINERS ps --all --quiet || return $?
    if [[ ${#TMP_CONTAINERS[@]} -eq 0 ]]; then
        log_notice "${FUNCTION_DESCRIPTION}: no containers"
        return 0
    fi
    _docker_run "${FUNCTION_DESCRIPTION}" "stopping ${#TMP_CONTAINERS[@]} containers" stop ${1:+--time "${1}"} "${TMP_CONTAINERS[@]}" || return $?
    _docker_run "${FUNCTION_DESCRIPTION}" "removing ${#TMP_CONTAINERS[@]} containers" rm "${TMP_CONTAINERS[@]}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: docker_reset_images()
## - Removes every image that no container uses
function docker_reset_images() {
    local FUNCTION_DESCRIPTION="Docker (Reset Images)"
    local -a TMP_IMAGES=()
    _docker_list TMP_IMAGES images --quiet || return $?
    if [[ ${#TMP_IMAGES[@]} -eq 0 ]]; then
        log_notice "${FUNCTION_DESCRIPTION}: no images"
        return 0
    fi
    _docker_run "${FUNCTION_DESCRIPTION}" "removing ${#TMP_IMAGES[@]} images" rmi "${TMP_IMAGES[@]}"
}

###------------------------------------------------------------------------------------------------
# Internal helpers (not part of the public function list; names start with an underscore)

# _docker_compose_command <array name>: the command that runs Docker Compose (v2 plugin, or v1 binary)
function _docker_compose_command() {
    if command docker compose version >/dev/null 2>&1; then
        eval "${1}=( docker compose )"
    elif command -v docker-compose >/dev/null 2>&1; then
        eval "${1}=( docker-compose )"
    else
        log_error "Docker Compose: not installed (neither the docker compose plugin nor docker-compose)"
        return $E_MISSING_DEPENDENCIES
    fi
}

# _docker_compose_project <description> <compose file> <environment file> <compose arguments...>
function _docker_compose_project() {
    local TMP_DESCRIPTION="${1}"
    local TMP_FILE_COMPOSE="${2}"
    local TMP_FILE_ENVIRONMENT="${3}"
    local TMP_OUTPUT=""
    local TMP_RETURNVAL=0
    local -a TMP_COMPOSE=()
    shift 3
    if [[ -z "${TMP_FILE_COMPOSE}" ]]; then
        log_error "${TMP_DESCRIPTION}: Compose file not specified"
        return $E_BAD_ARGS
    fi
    if [[ ! -f "${TMP_FILE_COMPOSE}" ]]; then
        log_error "${TMP_DESCRIPTION}: Compose file not found [${TMP_FILE_COMPOSE}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if [[ -n "${TMP_FILE_ENVIRONMENT}" && ! -f "${TMP_FILE_ENVIRONMENT}" ]]; then
        log_error "${TMP_DESCRIPTION}: environment file not found [${TMP_FILE_ENVIRONMENT}]"
        return $E_OBJECT_NOT_FOUND
    fi
    _docker_compose_command TMP_COMPOSE || return $?
    generate_temp_file TMP_OUTPUT "docker compose output"
    log_notice "${TMP_DESCRIPTION}: started"
    log "- Compose File:     [${TMP_FILE_COMPOSE}]"
    log "- Environment File: [${TMP_FILE_ENVIRONMENT:-none}]"
    log_add_from_file "${TMP_FILE_COMPOSE}" "${TMP_DESCRIPTION}: Compose file"
    command "${TMP_COMPOSE[@]}" --ansi never --file "${TMP_FILE_COMPOSE}" ${TMP_FILE_ENVIRONMENT:+--env-file "${TMP_FILE_ENVIRONMENT}"} "$@" >"${TMP_OUTPUT}" 2>&1 || TMP_RETURNVAL=$?
    log_add_from_file "${TMP_OUTPUT}" "${TMP_DESCRIPTION}: output"
    if (( TMP_RETURNVAL != 0 )); then
        log_error "${TMP_DESCRIPTION}: failed (returned::${TMP_RETURNVAL})"
        return $E_DOCKER_FAILURE
    fi
    log "${TMP_DESCRIPTION}: finished"
    return 0
}

# _docker_list <array name> <docker arguments...>: loads the IDs that docker prints into an array
function _docker_list() {
    local __DL_ARRAY="${1}"
    local __DL_OUTPUT=""
    local __DL_ID=""
    shift
    if ! command -v docker >/dev/null 2>&1; then
        log_error "Docker: not installed"
        return $E_MISSING_DEPENDENCIES
    fi
    if ! __DL_OUTPUT="$(command docker "$@" 2>&1)"; then
        log_error "Docker: [docker $*] failed: ${__DL_OUTPUT}"
        return $E_DOCKER_FAILURE
    fi
    eval "${__DL_ARRAY}=()"
    while IFS= read -r __DL_ID; do
        [[ -n "${__DL_ID}" ]] && add_element_to_array "${__DL_ARRAY}" "${__DL_ID}"
    done <<< "${__DL_OUTPUT}"
    return 0
}

# _docker_run <description> <what is happening> <docker arguments...>: runs docker, logs its output on failure
function _docker_run() {
    local TMP_DESCRIPTION="${1}"
    local TMP_OUTPUT=""
    local TMP_RETURNVAL=0
    shift
    log_notice "${TMP_DESCRIPTION}: ${1}"
    shift
    if ! command -v docker >/dev/null 2>&1; then
        log_error "${TMP_DESCRIPTION}: Docker is not installed"
        return $E_MISSING_DEPENDENCIES
    fi
    generate_temp_file TMP_OUTPUT "docker output"
    command docker "$@" >"${TMP_OUTPUT}" 2>&1 || TMP_RETURNVAL=$?
    if (( TMP_RETURNVAL != 0 )); then
        log_add_from_file "${TMP_OUTPUT}" "${TMP_DESCRIPTION}: output"
        log_error "${TMP_DESCRIPTION}: failed (returned::${TMP_RETURNVAL})"
        return $E_DOCKER_FAILURE
    fi
    return 0
}
