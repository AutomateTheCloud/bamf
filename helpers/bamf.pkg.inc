###------------------------------------------------------------------------------------------------
# script:  bamf.pkg.inc
# purpose: BAMF helper file for package management: install and update packages with apt-get, dnf or yum
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( pkg )
#          start_logic
#          package_management_install_packages jq unzip || exit_logic $?
#
# notes:
#          - the package manager is found automatically (apt-get, then dnf, then yum); set
#            PACKAGE_MANAGER to choose one
#          - apt-get waits up to PACKAGE_MANAGEMENT_LOCK_TIMEOUT seconds for the dpkg lock, which
#            unattended-upgrades often holds while an Ubuntu machine is starting; without the wait,
#            apt-get fails at once
#          - every command runs without prompts (DEBIAN_FRONTEND=noninteractive, -y), and its output
#            goes to the log
#
# function list:
#          - package_management_detect
#          - package_management_install_packages
#          - package_management_is_installed
#          - package_management_update_all
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Variables
declare -g PACKAGE_MANAGER="${PACKAGE_MANAGER:-}"                                    # apt-get, dnf or yum (empty finds it)
declare -g PACKAGE_MANAGEMENT_LOCK_TIMEOUT="${PACKAGE_MANAGEMENT_LOCK_TIMEOUT:-600}" # seconds apt-get waits for the dpkg lock

###------------------------------------------------------------------------------------------------
## FUNCTION: package_management_detect()
## - Returns the package manager to use: PACKAGE_MANAGER if set, otherwise the first of apt-get, dnf
##   and yum that is installed
## - Returns E_PACKAGE_MANAGEMENT_FAILURE if there is none
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function package_management_detect() {
    local __PD_MANAGER="${PACKAGE_MANAGER}"
    local __PD_CANDIDATE=""
    if [[ -z "${__PD_MANAGER}" ]]; then
        for __PD_CANDIDATE in apt-get dnf yum; do
            if command -v "${__PD_CANDIDATE}" >/dev/null 2>&1; then
                __PD_MANAGER="${__PD_CANDIDATE}"
                break
            fi
        done
    fi
    case "${__PD_MANAGER}" in
        apt-get|dnf|yum) ;;
        "")
            log_error "Package Management: no supported package manager found (apt-get, dnf or yum)"
            return $E_PACKAGE_MANAGEMENT_FAILURE;;
        *)
            log_error "Package Management: unsupported package manager [${__PD_MANAGER}] (apt-get, dnf or yum)"
            return $E_PACKAGE_MANAGEMENT_FAILURE;;
    esac
    _bamf_return "${1:-}" "${__PD_MANAGER}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: package_management_install_packages()
## - Installs packages (refreshing the package lists first with apt-get)
## - Arguments
##   - $1+: Package names, or one array passed by name (pass this way: "PACKAGES[@]")
function package_management_install_packages() {
    local FUNCTION_DESCRIPTION="Package Management (Install Packages)"
    local TMP_MANAGER=""
    local TMP_PACKAGE=""
    local -a TMP_PACKAGES=()
    if [[ $# -eq 1 && "${1}" == *"[@]" ]]; then
        TMP_PACKAGES=( "${!1}" )
    else
        TMP_PACKAGES=( "$@" )
    fi
    if [[ ${#TMP_PACKAGES[@]} -eq 0 ]]; then
        log "${FUNCTION_DESCRIPTION}: no packages specified, nothing to install"
        return 0
    fi
    package_management_detect TMP_MANAGER || return $?
    log_notice "${FUNCTION_DESCRIPTION}: started (${TMP_MANAGER})"
    for TMP_PACKAGE in "${TMP_PACKAGES[@]}"; do
        log "- ${TMP_PACKAGE}"
    done
    case "${TMP_MANAGER}" in
        apt-get)
            _pkg_run "${FUNCTION_DESCRIPTION}" "apt-get update" apt-get update || return $?
            _pkg_run "${FUNCTION_DESCRIPTION}" "apt-get install" apt-get install "${TMP_PACKAGES[@]}" || return $?;;
        *)
            _pkg_run "${FUNCTION_DESCRIPTION}" "${TMP_MANAGER} install" "${TMP_MANAGER}" install "${TMP_PACKAGES[@]}" || return $?;;
    esac
    log "${FUNCTION_DESCRIPTION}: installed ${#TMP_PACKAGES[@]} packages"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: package_management_is_installed()
## - Returns true if a package is installed (dpkg on apt-get systems, rpm on dnf and yum systems)
## - Arguments
##   - $1: Package name
function package_management_is_installed() {
    local TMP_MANAGER=""
    package_management_detect TMP_MANAGER >/dev/null 2>&1 || return $E_PACKAGE_MANAGEMENT_FAILURE
    if [[ "${TMP_MANAGER}" == "apt-get" ]]; then
        # dpkg -s also knows removed packages whose configuration is kept; only "install ok installed" counts
        [[ "$(command dpkg-query -W -f '${Status}' "${1:-}" 2>/dev/null)" == "install ok installed" ]]
    else
        command rpm -q "${1:-}" >/dev/null 2>&1
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: package_management_update_all()
## - Upgrades every installed package (apt-get update and upgrade, or dnf/yum update)
## - Changed configuration files keep the local version (apt-get), so an upgrade never stops to ask
## - Think twice before calling this from a deployment: upgrading the machine under an application
##   also upgrades what the deployment relies on (docker, systemd, ...), and makes every deployment
##   depend on the package mirrors. Patching belongs to whoever builds the machine image.
function package_management_update_all() {
    local FUNCTION_DESCRIPTION="Package Management (Update All)"
    local TMP_MANAGER=""
    package_management_detect TMP_MANAGER || return $?
    log_notice "${FUNCTION_DESCRIPTION}: started (${TMP_MANAGER})"
    case "${TMP_MANAGER}" in
        apt-get)
            _pkg_run "${FUNCTION_DESCRIPTION}" "apt-get update" apt-get update || return $?
            _pkg_run "${FUNCTION_DESCRIPTION}" "apt-get upgrade" apt-get \
                -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold upgrade || return $?;;
        *)
            _pkg_run "${FUNCTION_DESCRIPTION}" "${TMP_MANAGER} update" "${TMP_MANAGER}" update || return $?;;
    esac
    log "${FUNCTION_DESCRIPTION}: finished"
    return 0
}

###------------------------------------------------------------------------------------------------
# Internal helpers (not part of the public function list; names start with an underscore)

# _pkg_run <description> <step> <package manager> <arguments...>: runs it without prompts, logs its output
function _pkg_run() {
    local TMP_DESCRIPTION="${1}"
    local TMP_STEP="${2}"
    local TMP_MANAGER="${3}"
    local TMP_OUTPUT=""
    local TMP_RETURNVAL=0
    shift 3
    generate_temp_file TMP_OUTPUT "package manager output"
    if [[ "${TMP_MANAGER}" == "apt-get" ]]; then
        DEBIAN_FRONTEND=noninteractive command apt-get -y -o "DPkg::Lock::Timeout=${PACKAGE_MANAGEMENT_LOCK_TIMEOUT}" "$@" >"${TMP_OUTPUT}" 2>&1 || TMP_RETURNVAL=$?
    else
        command "${TMP_MANAGER}" -y "$@" >"${TMP_OUTPUT}" 2>&1 || TMP_RETURNVAL=$?
    fi
    log_add_from_file "${TMP_OUTPUT}" "${TMP_STEP}"
    if (( TMP_RETURNVAL != 0 )); then
        log_error "${TMP_DESCRIPTION}: ${TMP_STEP} failed (returned::${TMP_RETURNVAL})"
        return $E_PACKAGE_MANAGEMENT_FAILURE
    fi
    return 0
}
