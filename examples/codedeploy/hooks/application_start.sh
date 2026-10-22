#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  application_start.sh
# purpose: CodeDeploy ApplicationStart hook: writes the application's settings from SSM Parameter Store, then starts it
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# Settings and secrets are not in the revision bundle: they are read from SSM Parameter Store at
# deployment time, from the deployment group's own path, into an env file only root can read.
# Docker Compose reads that file as root and passes the values to the container as environment
# variables, so the container never needs to read the file itself.

# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="application_start"
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh" || exit 3
REQUIRED_SOURCE_FILES+=( aws docker )

start_logic

line_break
log_highlight "Settings"
log "- Deployment:     [${DEPLOYMENT_ID:-none}] (${DEPLOYMENT_GROUP_NAME:-local})"
log "- Parameter path: [${SSM_PATH}]"
# Uses the instance profile's credentials; the AWS CLI finds them by itself
aws_metadata_region REGION || exit_logic $?
aws_ssm_parameters_to_properties_file "${SSM_PATH}" "${FILE_ENVIRONMENT}" yes "${REGION}" || exit_logic $?
aws_metadata_instance_id INSTANCE_ID || exit_logic $?
write_dotenv_value "${FILE_ENVIRONMENT}" INSTANCE_ID "${INSTANCE_ID}" || exit_logic $?
log "- Settings file:  [${FILE_ENVIRONMENT}] (mode $(stat -c %a "${FILE_ENVIRONMENT}"), values not logged)"

line_break
log_highlight "Start"
docker_compose_up "${FILE_COMPOSE}" "${FILE_ENVIRONMENT}" || exit_logic $?
exit_logic 0
