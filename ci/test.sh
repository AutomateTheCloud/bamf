#!/bin/bash
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Runs the bats test suite, locally or inside container images
# usage: ci/test.sh                 run with the local bash
#        ci/test.sh <image> [...]   run inside each Docker image (example: centos:7 debian:12-slim)
#   BATS: path to bats (defaults to .cache/bats-core/bin/bats, then bats on PATH)
set -euo pipefail
cd "$(dirname "$0")/.."

if [ $# -eq 0 ]; then
    BATS="${BATS:-}"
    if [ -z "${BATS}" ]; then
        if [ -x .cache/bats-core/bin/bats ]; then BATS=.cache/bats-core/bin/bats; else BATS=bats; fi
    fi
    if ! command -v "${BATS}" >/dev/null 2>&1; then
        echo "ERROR: bats not found (run ci/install_test_dependencies.sh, or set BATS)" >&2
        exit 1
    fi
    echo "bash ${BASH_VERSION}"
    exec "${BATS}" tests/
fi

if [ ! -x .cache/bats-core/bin/bats ]; then
    echo "ERROR: run ci/install_test_dependencies.sh first (the containers use .cache/bats-core)" >&2
    exit 1
fi
FAILED=0
for IMAGE in "$@"; do
    echo "::group::${IMAGE}"
    # run as an ordinary user: some tests check behavior that root would bypass
    if ! docker run --rm --user 1000:1000 -e HOME=/tmp -v "${PWD}:/code:ro" -w /code "${IMAGE}" \
        bash -c 'echo "bash ${BASH_VERSION}"; .cache/bats-core/bin/bats tests/'; then
        FAILED=1
    fi
    echo "::endgroup::"
done
exit ${FAILED}
