#!/bin/bash
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Downloads pinned, checksum-verified copies of shellcheck and bats-core into .cache/
# usage: ci/install_test_dependencies.sh
set -euo pipefail
cd "$(dirname "$0")/.."

SHELLCHECK_VERSION="0.10.0"
SHELLCHECK_SHA256="6c881ab0698e4e6ea235245f22832860544f17ba386442fe7e9d629f8cbedf87"
BATS_VERSION="1.11.1"
BATS_SHA256="5c57ed9616b78f7fd8c553b9bae3c7c9870119edd727ec17dbd1185c599f79d9"

CACHE=".cache"
mkdir -p "${CACHE}"

# download <url> <file> <sha256>
download() {
    if [ ! -f "${CACHE}/${2}" ]; then
        curl -fsSL -o "${CACHE}/${2}.part" "${1}"
        mv "${CACHE}/${2}.part" "${CACHE}/${2}"
    fi
    echo "${3}  ${CACHE}/${2}" | sha256sum -c -
}

download "https://github.com/koalaman/shellcheck/releases/download/v${SHELLCHECK_VERSION}/shellcheck-v${SHELLCHECK_VERSION}.linux.x86_64.tar.xz" \
    "shellcheck-v${SHELLCHECK_VERSION}.tar.xz" "${SHELLCHECK_SHA256}"
download "https://github.com/bats-core/bats-core/archive/refs/tags/v${BATS_VERSION}.tar.gz" \
    "bats-core-${BATS_VERSION}.tar.gz" "${BATS_SHA256}"

rm -rf "${CACHE}/shellcheck" "${CACHE}/bats-core"
mkdir -p "${CACHE}/shellcheck" "${CACHE}/bats-core"
tar -xJf "${CACHE}/shellcheck-v${SHELLCHECK_VERSION}.tar.xz" -C "${CACHE}/shellcheck" --strip-components=1
tar -xzf "${CACHE}/bats-core-${BATS_VERSION}.tar.gz" -C "${CACHE}/bats-core" --strip-components=1
echo "shellcheck: ${CACHE}/shellcheck/shellcheck"
echo "bats:       ${CACHE}/bats-core/bin/bats"
