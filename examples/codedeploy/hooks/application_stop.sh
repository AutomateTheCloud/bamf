#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  application_stop.sh
# purpose: CodeDeploy ApplicationStop hook: stops the running containers
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# CodeDeploy runs ApplicationStop from the revision that is already installed, before the new one
# arrives. On an instance's first deployment there is nothing to stop.

# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="application_stop"
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh" || exit 3
REQUIRED_SOURCE_FILES+=( docker )

start_logic
if [[ ! -f "${FILE_COMPOSE}" ]]; then
    log_notice "No application installed yet, nothing to stop"
    exit_logic 0
fi
docker_compose_down "${FILE_COMPOSE}" || exit_logic $?
exit_logic 0
