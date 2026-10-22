#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  validate_service.sh
# purpose: CodeDeploy ValidateService hook: waits for the application to answer before the deployment succeeds
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# If the application never answers, this hook fails, CodeDeploy marks the instance's deployment as
# failed, and (with a deployment configuration that allows it) stops before the rest of the fleet
# gets the broken revision.

# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="validate_service"
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh" || exit 3
REQUIRED_SOURCE_FILES+=( curl )

start_logic
# 12 attempts, 10 seconds apart: two minutes for the container to start
curl_wait_for_http_status "${HEALTH_URL}" 200 "${HEALTH_ATTEMPTS:-12}" "${HEALTH_DELAY:-10}" || exit_logic $?
log_success "The application is healthy"
exit_logic 0
