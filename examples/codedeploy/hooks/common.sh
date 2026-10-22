# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# shellcheck disable=SC2034 # these settings are read by the hooks that source this file
# Settings shared by every hook. Each hook sets SELF_IDENTITY, then sources this file.
#
# CodeDeploy runs each hook with environment variables that describe the deployment, including
# APPLICATION_NAME, DEPLOYMENT_GROUP_NAME, DEPLOYMENT_ID and LIFECYCLE_EVENT. Using them keeps one
# set of hooks working for every environment: the deployment group decides which settings to load.

DIRECTORY_HOOKS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# In a revision bundle, bamf.inc and helpers/ are copied to bamf/ next to hooks/. Run from this
# repository, the hooks use the repository's own copy.
if [[ -f "${DIRECTORY_HOOKS}/../bamf/bamf.inc" ]]; then
    FILE_BAMF="${DIRECTORY_HOOKS}/../bamf/bamf.inc"
else
    FILE_BAMF="${DIRECTORY_HOOKS}/../../../bamf.inc"
fi
# shellcheck source=../../../bamf.inc
source "${FILE_BAMF}" || exit 3

# The application's directory on the instance (appspec.yml copies app/ here)
DIRECTORY_APPLICATION="${DIRECTORY_APPLICATION:-/opt/example-app}"
FILE_COMPOSE="${DIRECTORY_APPLICATION}/docker-compose.yaml"
FILE_ENVIRONMENT="${DIRECTORY_APPLICATION}/app.env"
# Settings for this deployment group live in SSM Parameter Store under this path
SSM_PATH="/${APPLICATION_NAME:-example-app}/${DEPLOYMENT_GROUP_NAME:-local}"
HEALTH_URL="${HEALTH_URL:-http://localhost:8080/}"
# A pinned Compose version: a deployment should not change because someone published a release
DOCKER_COMPOSE_VERSION="v2.29.7"
