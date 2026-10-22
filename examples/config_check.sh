#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  config_check.sh
# purpose: Reads a configuration file, checks every required value is set, and hands values to a CI pipeline
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# What this example shows:
#   - load_array_properties_from_file: many keys from one file, into variables named after them
#   - verify_array_key_values: one place that lists what the script cannot run without
#   - define_exit_code: a script's own exit code, named in the log like BAMF's
#   - write_dotenv_value: NAME=VALUE lines that GitLab, GitHub Actions and Jenkins can all read
#
# Try it:
#   examples/config_check.sh                         fails: db.user is empty in the sample file
#   examples/config_check.sh -o build.env -u admin   passes, and writes build.env

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="config_check"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-f <file> : Configuration file (optional, defaults to examples/config/app.properties)'
    '-u <user> : Database user, overriding the file (optional)'
    '-o <file> : dotenv file to append the values to, for a CI pipeline (optional)'
)

###------------------------------------------------------------------------------------------------
## Load BAMF
# shellcheck source=../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../bamf.inc" || exit 3

# Exit codes 201-250 are free for your own scripts. The log will show the name and description.
define_exit_code E_CONFIG_INCOMPLETE 201 "The configuration file is missing required values"

###------------------------------------------------------------------------------------------------
## Variables
FILE_CONFIG="$(dirname "${BASH_SOURCE[0]}")/config/app.properties"
FILE_DOTENV=""
DB_USER_OVERRIDE=""
# The keys this script needs. Each is loaded into CONFIG_<KEY>: db.host becomes CONFIG_DB_HOST.
CONFIG_REQUIRED=( 'app.name' 'app.environment' 'db.host' 'db.port' 'db.name' 'db.user' )

###------------------------------------------------------------------------------------------------
## Main
while getopts "hVf:u:o:" OPTION; do
    case "${OPTION}" in
        f) FILE_CONFIG="${OPTARG}";;
        u) DB_USER_OVERRIDE="${OPTARG}";;
        o) FILE_DOTENV="${OPTARG}";;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic

line_break
log_highlight "Initialize"
if [[ ! -f "${FILE_CONFIG}" ]]; then
    log_error "Configuration file not found [${FILE_CONFIG}]"
    exit_logic "${E_OBJECT_NOT_FOUND}"
fi
log "- Configuration File: [${FILE_CONFIG}]"
load_array_properties_from_file "CONFIG_REQUIRED[@]" "${FILE_CONFIG}" CONFIG
# A command line argument wins over the file: a common pattern for values that differ per run
if [[ -n "${DB_USER_OVERRIDE}" ]]; then
    CONFIG_DB_USER="${DB_USER_OVERRIDE}"
fi
# Check everything at once and report every missing value, instead of failing on the first
if ! verify_array_key_values "CONFIG_REQUIRED[@]" CONFIG; then
    exit_logic "${E_CONFIG_INCOMPLETE}" "fix the values listed above"
fi

line_break
log_highlight "Execution"
log "- Application: [${CONFIG_APP_NAME}] (${CONFIG_APP_ENVIRONMENT})"
log "- Database:    [${CONFIG_DB_USER}@${CONFIG_DB_HOST}:${CONFIG_DB_PORT}/${CONFIG_DB_NAME}]"
if ! is_int "${CONFIG_DB_PORT}"; then
    log_error "db.port must be a number [${CONFIG_DB_PORT}]"
    exit_logic "${E_CONFIG_INCOMPLETE}"
fi
if [[ -n "${FILE_DOTENV}" ]]; then
    # Later CI jobs read these as environment variables (GitLab: artifacts:reports:dotenv;
    # GitHub Actions: pass -o "$GITHUB_ENV")
    write_dotenv_value "${FILE_DOTENV}" APP_NAME "${CONFIG_APP_NAME}" || exit_logic $?
    write_dotenv_value "${FILE_DOTENV}" APP_ENVIRONMENT "${CONFIG_APP_ENVIRONMENT}" || exit_logic $?
    write_dotenv_value "${FILE_DOTENV}" DATABASE_URL "postgresql://${CONFIG_DB_USER}@${CONFIG_DB_HOST}:${CONFIG_DB_PORT}/${CONFIG_DB_NAME}" || exit_logic $?
    log "- Wrote the values to [${FILE_DOTENV}]"
fi

log_success "The configuration is complete"
exit_logic 0
