#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  before_install.sh
# purpose: CodeDeploy BeforeInstall hook: makes sure Docker and a pinned Docker Compose are installed
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# This hook installs only what the application needs, and only when it is missing. It does not
# upgrade every package on the machine: that would change what the deployment depends on (Docker,
# systemd) in the middle of deploying, and make every deployment depend on the package mirrors.
# Patching belongs to whoever builds the machine image, on its own schedule.

# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="before_install"
# shellcheck source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh" || exit 3
REQUIRED_SOURCE_FILES+=( docker )

start_logic
line_break
log_highlight "Docker"
docker_install || exit_logic $?
docker_compose_install "${DOCKER_COMPOSE_VERSION}" || exit_logic $?
exit_logic 0
