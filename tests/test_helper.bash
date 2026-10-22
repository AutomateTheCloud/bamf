# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# shellcheck disable=SC2034 # variables are used by the test files and the library
# Shared setup for the BAMF bats test suite

BAMF_ROOT="$(cd "$(dirname "${BATS_TEST_FILENAME}")/.." && pwd)"
FIXTURES="${BAMF_ROOT}/tests/fixtures"

# Every test gets its own temporary directory, no color and no timestamps
setup_bamf() {
    export BAMF_DIRECTORY_TEMPORARY="${BATS_TEST_TMPDIR}/bamf"
    mkdir -p "${BAMF_DIRECTORY_TEMPORARY}"
    export BAMF_ENABLE_COLOR=no
    # the tests compare log lines exactly, so timestamps are off unless a test turns them on
    export BAMF_ENABLE_TIMESTAMP=no
    unset ENABLE_COLOR ENABLE_TIMESTAMP NO_COLOR RUN_SCRIPT_AS_CHILD DIRECTORY_TEMPORARY SENSITIVE_ARGUMENTS BAMF_LOG_IDENTITY_WIDTH
}

# Load bamf.inc into the current test shell
load_bamf() {
    SELF_IDENTITY="${1:-bats_test}"
    # shellcheck source=../bamf.inc
    source "${BAMF_ROOT}/bamf.inc"
}

# Remove the "[identity]... " prefix from log lines
strip_prefix() {
    sed -E 's/^\[[^]]*\]\.* //'
}

# Put the AWS CLI and curl stubs first on PATH (they never contact AWS). The real AWS CLI is also
# pointed at an unreachable endpoint with empty configuration, so even if the stub were bypassed no
# request could reach AWS.
use_aws_stub() {
    export PATH="${BAMF_ROOT}/tests/helpers/bin:${PATH}"
    export AWS_ENDPOINT_URL="http://127.0.0.1:9"
    export AWS_CONFIG_FILE=/dev/null AWS_SHARED_CREDENTIALS_FILE=/dev/null AWS_EC2_METADATA_DISABLED=true
    export AWS_METADATA_URL="http://imds.stub"
    export STUB_DIR="${BATS_TEST_TMPDIR}/stub"
    export STUB_LOG="${STUB_DIR}/aws.log"
    export STUB_PROFILES="${STUB_PROFILES:-ci-role}"
    export AWS_RETRY_TIMER_MAX_SEC=0
    unset AWS_PROFILE AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY AWS_SESSION_TOKEN AWS_REGION AWS_DEFAULT_REGION
    mkdir -p "${STUB_DIR}/imds"
    : > "${STUB_LOG}"
}

# stub_response <service.operation> <output> [exit code] [error output]: what the AWS CLI stub answers
stub_response() {
    printf '%s' "${2}" > "${STUB_DIR}/${1}.out"
    [[ -n "${3:-}" ]] && printf '%s' "${3}" > "${STUB_DIR}/${1}.rc"
    [[ -n "${4:-}" ]] && printf '%s\n' "${4}" > "${STUB_DIR}/${1}.err"
    return 0
}

# stub_calls <service> <operation>: how many times the AWS CLI stub was called for an operation
stub_calls() {
    grep -c "^ARGV: \(\[--[a-z-]*\] \[[^]]*\] \)*\[${1}\] \[${2}\]" "${STUB_LOG}" || true
}

# Load bamf.inc and bamf.aws.inc into the current test shell, against the stubs
load_bamf_aws() {
    use_aws_stub
    load_bamf "${1:-aws_test}"
    # shellcheck source=../helpers/bamf.aws.inc
    source "${BAMF_ROOT}/helpers/bamf.aws.inc"
}

# Put the generic tool stubs (docker, systemctl, apt-get, dnf, yum, dpkg-query, rpm, npm, terraform)
# and the curl stub first on PATH. They never run the real commands; see tests/helpers/stub.
use_tool_stubs() {
    export STUB_DIR="${BATS_TEST_TMPDIR}/stub"
    export STUB_LOG="${STUB_DIR}/calls.log"
    export PATH="${BAMF_ROOT}/tests/helpers/bin-tools:${BAMF_ROOT}/tests/helpers/bin:${PATH}"
    mkdir -p "${STUB_DIR}/http"
    : > "${STUB_LOG}"
}

# tool_response <command.arg1.arg2> <output> [exit code]: what the generic stub answers
tool_response() {
    printf '%s' "${2}" > "${STUB_DIR}/${1}.out"
    [[ -n "${3:-}" ]] && printf '%s' "${3}" > "${STUB_DIR}/${1}.rc"
    return 0
}

# http_response <host/path> <body> [http code]: what the curl stub serves
http_response() {
    mkdir -p "$(dirname "${STUB_DIR}/http/${1}")"
    printf '%s' "${2}" > "${STUB_DIR}/http/${1}"
    [[ -n "${3:-}" ]] && printf '%s' "${3}" > "${STUB_DIR}/http/${1}.code"
    return 0
}

# Load bamf.inc and the named helper files into the current test shell, against the tool stubs
load_bamf_helpers() {
    use_tool_stubs
    load_bamf helper_test
    local HELPER
    for HELPER in "$@"; do
        source_include "${HELPER}"
    done
}
