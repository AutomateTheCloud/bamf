#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# The example scripts run as documented (AWS, Docker and the network are stubbed)

load test_helper

EXAMPLES="${BAMF_ROOT}/examples"

setup() {
    setup_bamf
    cd "${BATS_TEST_TMPDIR}" || return 1
}

# Stubs for everything outside the machine: the AWS CLI, curl, docker, systemctl and the rest
use_all_stubs() {
    use_aws_stub
    export PATH="${BAMF_ROOT}/tests/helpers/bin-tools:${PATH}"
    mkdir -p "${STUB_DIR}/http"
}

@test "hello.sh: says hello; an empty name is E_BAD_ARGS" {
    run "${EXAMPLES}/hello.sh" -n "Automate the Cloud"
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"Hello, Automate the Cloud"* ]]
    [[ "${output}" == *"Exit_Code::0 [success]"* ]]
    run "${EXAMPLES}/hello.sh" -n ""
    [ "${status}" -eq 5 ]
    run "${EXAMPLES}/hello.sh" -h
    [[ "${output}" == *"DESCRIPTION: A first BAMF script"* ]]
}

@test "config_check.sh: reports the missing value with its own exit code, then passes and writes dotenv" {
    run "${EXAMPLES}/config_check.sh"
    [ "${status}" -eq 201 ]
    [[ "${output}" == *"key is not set (Key::db.user"* ]]
    [[ "${output}" == *"[E_CONFIG_INCOMPLETE / The configuration file is missing required values]"* ]]
    run "${EXAMPLES}/config_check.sh" -u admin -o build.env
    [ "${status}" -eq 0 ]
    [ "$(cat build.env)" = $'APP_NAME=inventory-api\nAPP_ENVIRONMENT=staging\nDATABASE_URL=postgresql://admin@db.staging.internal:5432/inventory' ]
}

@test "pipeline.sh: runs each step; a failing step's exit code ends the pipeline" {
    run "${EXAMPLES}/pipeline.sh"
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"[build]"*"Build finished"* ]]
    [[ "${output}" == *"[PASS]"*"Package step"* ]]
    run "${EXAMPLES}/pipeline.sh" -f
    [ "${status}" -eq 8 ]
    [[ "${output}" == *"[FAIL]"*"Package step"* ]]
    [[ "${output}" == *"Exit_Code::8 [E_OBJECT_NOT_FOUND / Object not found] / Exit_Message::step [package.sh] failed"* ]]
}

@test "cleanup_and_signals.sh: the lock is released on success, on an error under set -e, and on a signal" {
    run "${EXAMPLES}/cleanup_and_signals.sh" -s 0 -l demo.lock
    [ "${status}" -eq 0 ]
    [ ! -e demo.lock ]
    run "${EXAMPLES}/cleanup_and_signals.sh" -s 5 -e -l demo.lock
    [ "${status}" -eq 1 ]
    [[ "${output}" == *"Released the lock"*"exiting with 1"* ]]
    [ ! -e demo.lock ]
    touch demo.lock
    run "${EXAMPLES}/cleanup_and_signals.sh" -s 0 -l demo.lock
    [ "${status}" -eq 252 ]
    [ -e demo.lock ]
    rm -f demo.lock
    "${EXAMPLES}/cleanup_and_signals.sh" -s 30 -l demo.lock > out.log 2>&1 &
    local PID=$! WAITED=0
    while [ ! -e demo.lock ] && (( WAITED < 50 )); do sleep 0.1; WAITED=$((WAITED+1)); done
    [ -e demo.lock ]
    kill -TERM "${PID}"
    local STATUS=0
    wait "${PID}" || STATUS=$?
    [ "${STATUS}" -eq 143 ]
    [ ! -e demo.lock ]
    grep -q "Released the lock" out.log
}

@test "download_verify.sh: a matching download is extracted; a mismatch stops before extracting" {
    command -v tar >/dev/null && command -v gzip >/dev/null || skip "tar or gzip is not installed"
    use_all_stubs
    mkdir -p release
    printf 'v1\n' > release/VERSION
    mkdir -p "${STUB_DIR}/http/http.stub"
    tar -czf "${STUB_DIR}/http/http.stub/app-1.0.tar.gz" -C release .
    local SHA256
    SHA256="$(sha256sum < "${STUB_DIR}/http/http.stub/app-1.0.tar.gz" | cut -d' ' -f1)"
    run "${EXAMPLES}/download_verify.sh" -u http://http.stub/app-1.0.tar.gz -s "${SHA256}" -d installed
    [ "${status}" -eq 0 ]
    [ "$(cat installed/VERSION)" = "v1" ]
    run "${EXAMPLES}/download_verify.sh" -u http://http.stub/app-1.0.tar.gz -s "$(printf '0%.0s' {1..64})" -d other
    [ "${status}" -eq 111 ]
    [ ! -e other ]
}

@test "backup_to_s3.sh: archives, encrypts and uploads; nothing unencrypted is left behind" {
    command -v tar >/dev/null && command -v gzip >/dev/null || skip "tar or gzip is not installed"
    command -v openssl >/dev/null && openssl enc -help 2>&1 | grep -q -- -pbkdf2 || skip "OpenSSL 1.1.1 or newer is not installed"
    use_all_stubs
    stub_response kms.generate-data-key "$(printf '%s\t%s' "$(head -c 32 /dev/urandom | base64 -w 0)" "RU5DUllQVEVE")"
    mkdir -p www
    printf 'site\n' > www/index.html
    run "${EXAMPLES}/backup_to_s3.sh" -s www -b backups-bucket -k alias/backups -p nightly -r us-east-1
    [ "${status}" -eq 0 ]
    grep -q '\[s3\] \[cp\] \[--only-show-errors\] \[.*/encrypted/www-[0-9T]*\.tar\.gz\.enc\] \[s3://backups-bucket/nightly/www-[0-9T]*\.tar\.gz\.enc\]' "${STUB_LOG}"
    [ -z "$(ls -A "${BAMF_DIRECTORY_TEMPORARY}")" ]
}

@test "codedeploy hooks: stop with nothing installed, start with settings from SSM, validate the service" {
    use_all_stubs
    export DIRECTORY_APPLICATION="${BATS_TEST_TMPDIR}/app" APPLICATION_NAME=shop DEPLOYMENT_GROUP_NAME=staging DEPLOYMENT_ID=d-TEST
    mkdir -p "${DIRECTORY_APPLICATION}"
    run "${EXAMPLES}/codedeploy/hooks/application_stop.sh"
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"nothing to stop"* ]]

    cp "${EXAMPLES}/codedeploy/app/docker-compose.yaml" "${DIRECTORY_APPLICATION}/"
    mkdir -p "${STUB_DIR}/imds/meta-data" "${STUB_DIR}/imds/dynamic/instance-identity"
    printf '{"region" : "us-east-2"}' > "${STUB_DIR}/imds/dynamic/instance-identity/document"
    printf 'i-0123456789abcdef0' > "${STUB_DIR}/imds/meta-data/instance-id"
    stub_response ssm.get-parameters-by-path "$(printf '/shop/staging/DATABASE_PASSWORD\ts3cr3t\n/shop/staging/LOG_LEVEL\tinfo\n')"
    run "${EXAMPLES}/codedeploy/hooks/application_start.sh"
    [ "${status}" -eq 0 ]
    [[ "${output}" != *"s3cr3t"* ]]
    grep -q '^ARGV: \[--region\] \[us-east-2\] \[ssm\] \[get-parameters-by-path\] \[--path\] \[/shop/staging/\]' "${STUB_LOG}"
    [ "$(stat -c %a "${DIRECTORY_APPLICATION}/app.env")" = "600" ]
    grep -qx 'DATABASE_PASSWORD=s3cr3t' "${DIRECTORY_APPLICATION}/app.env"
    grep -qx 'INSTANCE_ID=i-0123456789abcdef0' "${DIRECTORY_APPLICATION}/app.env"
    grep -qF "[--env-file] [${DIRECTORY_APPLICATION}/app.env] [up] [-d]" "${STUB_LOG}"

    http_response http.stub/healthz "ok"
    HEALTH_URL=http://http.stub/healthz HEALTH_DELAY=0 run "${EXAMPLES}/codedeploy/hooks/validate_service.sh"
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"The application is healthy"* ]]
    HEALTH_URL=http://http.stub/down HEALTH_ATTEMPTS=2 HEALTH_DELAY=0 run "${EXAMPLES}/codedeploy/hooks/validate_service.sh"
    [ "${status}" -eq 124 ]
}
