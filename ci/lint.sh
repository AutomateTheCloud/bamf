#!/bin/bash
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Lints the library, examples, tests and CI scripts with shellcheck
# usage: ci/lint.sh [severity]   (warning by default; style for the full report)
#   SHELLCHECK: path to shellcheck (defaults to .cache/shellcheck/shellcheck, then shellcheck on PATH)
set -euo pipefail
cd "$(dirname "$0")/.."

SEVERITY="${1:-warning}"
SHELLCHECK="${SHELLCHECK:-}"
if [ -z "${SHELLCHECK}" ]; then
    if [ -x .cache/shellcheck/shellcheck ]; then SHELLCHECK=.cache/shellcheck/shellcheck; else SHELLCHECK=shellcheck; fi
fi
if ! command -v "${SHELLCHECK}" >/dev/null 2>&1; then
    echo "ERROR: shellcheck not found (run ci/install_test_dependencies.sh, or set SHELLCHECK)" >&2
    exit 1
fi

mapfile -t FILES < <(find . -path ./temp -prune -o -path ./.cache -prune -o -path ./.git -prune -o -type f \
    \( -name '*.inc' -o -name '*.sh' -o -name '*.bash' -o -name '*.bats' \) -print | sort)
"${SHELLCHECK}" --version | sed -n 2p
"${SHELLCHECK}" --external-sources --severity="${SEVERITY}" "${FILES[@]}"
echo "shellcheck: ${#FILES[@]} files, no findings at severity [${SEVERITY}] or above"
