#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Checks on the library files themselves

load test_helper

@test "bamf.inc: the function list in the header matches the functions defined" {
    local LISTED DEFINED
    LISTED="$(sed -n '/^# function list:/,/^###/p' "${BAMF_ROOT}/bamf.inc" | grep -oE '^#\s+- [a-z_0-9]+' | awk '{print $3}' | sort)"
    DEFINED="$(grep -oE '^function [a-z][a-z_0-9]*\(\)' "${BAMF_ROOT}/bamf.inc" | sed -E 's/^function ([a-z_0-9]+)\(\)/\1/' | sort)"
    [ "${LISTED}" = "${DEFINED}" ] || { echo "listed:"; echo "${LISTED}"; echo "defined:"; echo "${DEFINED}"; return 1; }
}

@test "bamf.inc: every public function has a FUNCTION doc comment" {
    local NAME MISSING=""
    for NAME in $(grep -oE '^function [a-z][a-z_0-9]*\(\)' "${BAMF_ROOT}/bamf.inc" | sed -E 's/^function ([a-z_0-9]+)\(\)/\1/'); do
        grep -q "^## FUNCTION: ${NAME}()$" "${BAMF_ROOT}/bamf.inc" || MISSING+=" ${NAME}"
    done
    [ -z "${MISSING}" ] || { echo "missing:${MISSING}"; return 1; }
}

@test "source files start with the copyright and SPDX header" {
    local FILE MISSING="" COUNT=0
    shopt -s globstar nullglob
    for FILE in "${BAMF_ROOT}"/*.inc "${BAMF_ROOT}"/helpers/*.inc "${BAMF_ROOT}"/ci/*.sh "${BAMF_ROOT}"/tests/**/*.bats "${BAMF_ROOT}"/tests/**/*.bash \
        "${BAMF_ROOT}"/tests/**/*.sh "${BAMF_ROOT}"/examples/**/*.sh "${BAMF_ROOT}"/examples/**/*.inc "${BAMF_ROOT}"/.github/**/*.yml; do
        COUNT=$((COUNT+1))
        # at the top of the file, or inside the script/purpose/version header block
        head -12 "${FILE}" | grep -qE '^# Copyright [0-9]{4} Automate the Cloud Inc\.$' || MISSING+=" ${FILE#"${BAMF_ROOT}"/}"
        head -12 "${FILE}" | grep -q '^# SPDX-License-Identifier: Apache-2.0$' || MISSING+=" ${FILE#"${BAMF_ROOT}"/}"
    done
    [ "${COUNT}" -gt 5 ]
    [ -z "${MISSING}" ] || { echo "missing:${MISSING}"; return 1; }
}
