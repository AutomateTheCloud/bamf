#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# Helper files other than AWS: pkg, docker, curl, git, tar, terraform, npm (against stubs; git and tar
# use the real tools)

# shellcheck disable=SC2034 # variables are read by the library functions under test
load test_helper

setup() {
    setup_bamf
    cd "${BATS_TEST_TMPDIR}" || return 1
}

###------------------------------------------------------------------------------------------------
## bamf.pkg.inc

@test "pkg: apt-get waits for the dpkg lock, never prompts, refreshes the lists before installing" {
    load_bamf_helpers pkg
    PACKAGE_MANAGER=apt-get
    run package_management_install_packages jq "unzip"
    [ "${status}" -eq 0 ]
    grep -qF 'CMD: apt-get [-y] [-o] [DPkg::Lock::Timeout=600] [update]' "${STUB_LOG}"
    grep -qF 'CMD: apt-get [-y] [-o] [DPkg::Lock::Timeout=600] [install] [jq] [unzip]' "${STUB_LOG}"
    [ "$(grep -c 'ENV: DEBIAN_FRONTEND=noninteractive' "${STUB_LOG}")" -eq 2 ]
}

@test "pkg: an array of packages, dnf, and a failure with the package manager's output" {
    load_bamf_helpers pkg
    PACKAGE_MANAGER=dnf
    PACKAGES=( docker "amazon-ecr-credential-helper" )
    package_management_install_packages "PACKAGES[@]" > /dev/null
    grep -qF 'CMD: dnf [-y] [install] [docker] [amazon-ecr-credential-helper]' "${STUB_LOG}"
    tool_response dnf.install "Error: Unable to find a match: nope" 1
    run package_management_install_packages nope
    [ "${status}" -eq "${E_PACKAGE_MANAGEMENT_FAILURE}" ]
    [[ "${output}" == *"Unable to find a match: nope"* ]]
}

@test "pkg: update_all keeps local configuration files; unsupported managers are refused" {
    load_bamf_helpers pkg
    PACKAGE_MANAGER=apt-get
    package_management_update_all > /dev/null
    grep -qF '[-o] [Dpkg::Options::=--force-confdef] [-o] [Dpkg::Options::=--force-confold] [upgrade]' "${STUB_LOG}"
    PACKAGE_MANAGER=pacman
    run package_management_update_all
    [ "${status}" -eq "${E_PACKAGE_MANAGEMENT_FAILURE}" ]
}

@test "pkg: is_installed reads dpkg status or rpm" {
    load_bamf_helpers pkg
    PACKAGE_MANAGER=apt-get
    tool_response dpkg-query "install ok installed"
    package_management_is_installed curl
    tool_response dpkg-query "deinstall ok config-files"
    ! package_management_is_installed curl || return 1
    PACKAGE_MANAGER=yum
    package_management_is_installed curl
    tool_response rpm "package nope is not installed" 1
    ! package_management_is_installed nope || return 1
}

###------------------------------------------------------------------------------------------------
## bamf.docker.inc

@test "docker: loading it loads bamf.pkg.inc and defines E_DOCKER_FAILURE" {
    load_bamf_helpers docker
    function_exists package_management_install_packages
    [ "$(lookup_exit_code 101)" = "E_DOCKER_FAILURE / Failure related to Docker" ]
}

@test "docker compose up: v2 plugin, the environment file passed through (not a global)" {
    load_bamf_helpers docker
    printf 'services: {}\n' > compose.yaml
    printf 'SECRET=hunter2\n' > app.env
    run docker_compose_up compose.yaml app.env
    [ "${status}" -eq 0 ]
    grep -qF 'CMD: docker [compose] [--ansi] [never] [--file] [compose.yaml] [--env-file] [app.env] [up] [-d]' "${STUB_LOG}"
    [[ "${output}" != *"hunter2"* ]]
    run docker_compose_up compose.yaml missing.env
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
}

@test "docker compose down: falls back to docker-compose v1 when the plugin is missing" {
    load_bamf_helpers docker
    export PATH="${BAMF_ROOT}/tests/helpers/bin-compose-v1:${PATH}"
    tool_response docker.compose.version "" 1
    printf 'services: {}\n' > compose.yaml
    docker_compose_down compose.yaml > /dev/null
    grep -qF 'CMD: docker-compose [--ansi] [never] [--file] [compose.yaml] [down]' "${STUB_LOG}"
}

@test "docker compose: a failure returns E_DOCKER_FAILURE (it no longer ends the script)" {
    load_bamf_helpers docker
    printf 'services: {}\n' > compose.yaml
    # the stub answers by the first two words that are not options: "compose" and "never" (from --ansi never)
    tool_response docker.compose.version "Docker Compose version v2.29.7"
    tool_response docker.compose.never "no such image" 1
    run docker_compose_up compose.yaml
    [ "${status}" -eq "${E_DOCKER_FAILURE}" ]
    [[ "${output}" == *"no such image"* ]]
    [[ "${output}" != *"SCRIPT_FINISHED"* ]]
}

@test "docker reset: stops and removes every container; nothing to do is fine" {
    load_bamf_helpers docker
    tool_response docker.ps $'c1\nc2\n'
    docker_reset 240 > /dev/null
    grep -qF 'CMD: docker [stop] [--time] [240] [c1] [c2]' "${STUB_LOG}"
    grep -qF 'CMD: docker [rm] [c1] [c2]' "${STUB_LOG}"
    tool_response docker.ps ""
    run docker_reset
    [[ "${output}" == *"no containers"* ]]
    run docker_reset abc
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "docker install: skips an installed Docker and starts the service" {
    load_bamf_helpers docker
    docker_install > /dev/null
    ! grep -q 'CMD: apt-get' "${STUB_LOG}" || return 1
    grep -qF 'CMD: systemctl [enable] [--now] [docker.service]' "${STUB_LOG}"
}

@test "docker install: installs docker.io with apt-get when Docker is missing" {
    load_bamf_helpers docker
    # a PATH with the package manager and systemctl stubs, but no docker
    mkdir -p no-docker
    ln -s "${BAMF_ROOT}/tests/helpers/stub" no-docker/apt-get
    ln -s "${BAMF_ROOT}/tests/helpers/stub" no-docker/systemctl
    export PATH="${BATS_TEST_TMPDIR}/no-docker:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
    command -v docker >/dev/null && skip "Docker is installed on this machine"
    PACKAGE_MANAGER=apt-get
    run docker_install
    [ "${status}" -eq 0 ]
    grep -qF '[install] [docker.io]' "${STUB_LOG}"
}

@test "docker ecr_login: the password goes through standard input, never the command line" {
    load_bamf_helpers docker
    use_aws_stub
    export PATH="${BAMF_ROOT}/tests/helpers/bin-tools:${BAMF_ROOT}/tests/helpers/bin:${PATH}"
    export STUB_READ_STDIN="docker"
    stub_response ecr.get-login-password "ECR-PASSWORD-123"
    run docker_ecr_login 123456789012.dkr.ecr.eu-west-1.amazonaws.com
    [ "${status}" -eq 0 ]
    grep -q '^ARGV: \[ecr\] \[get-login-password\] \[--region\] \[eu-west-1\]' "${STUB_LOG}"
    grep -qF 'CMD: docker [login] [--username] [AWS] [--password-stdin] [123456789012.dkr.ecr.eu-west-1.amazonaws.com]' "${STUB_LOG}"
    grep -qF 'STDIN: [ECR-PASSWORD-123]' "${STUB_LOG}"
    ! grep '^CMD\|^ARGV' "${STUB_LOG}" | grep -q 'ECR-PASSWORD-123' || return 1
    run docker_ecr_login docker.io
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "docker ecr_login: does not change the caller's shell options" {
    load_bamf_helpers docker
    use_aws_stub
    export PATH="${BAMF_ROOT}/tests/helpers/bin-tools:${BAMF_ROOT}/tests/helpers/bin:${PATH}"
    set +o pipefail
    docker_ecr_login 123456789012.dkr.ecr.us-east-1.amazonaws.com > /dev/null
    [[ "$(set -o)" == *"pipefail"*"off"* ]]
}

@test "docker compose_install: refuses a download whose checksum does not match" {
    load_bamf_helpers docker
    tool_response docker.compose.version "" 1
    DOCKER_COMPOSE_PLUGIN_DIRECTORY="${BATS_TEST_TMPDIR}/plugins"
    local URL_PATH=""
    URL_PATH="github.com/docker/compose/releases/download/v2.29.7/docker-compose-linux-$(uname -m)"
    http_response "${URL_PATH}" "binary-content"
    run docker_compose_install v2.29.7 "0000000000000000000000000000000000000000000000000000000000000000"
    [ "${status}" -eq "${E_CHECKSUM_MISMATCH}" ]
    [ ! -e "${BATS_TEST_TMPDIR}/plugins/docker-compose" ]
    http_response "${URL_PATH}.sha256" "$(printf 'binary-content' | sha256sum | cut -d' ' -f1)  docker-compose-linux-$(uname -m)"
    run docker_compose_install v2.29.7
    [ "${status}" -eq 0 ]
    [ -x "${BATS_TEST_TMPDIR}/plugins/docker-compose" ]
    run docker_compose_install latest
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

###------------------------------------------------------------------------------------------------
## bamf.curl.inc

@test "curl request: body saved, HTTP code kept, 5xx retried, 404 not retried" {
    load_bamf_helpers curl
    CURL_DEFAULT_RETRY_TIMER_MAX_SEC=0
    http_response http.stub/file.txt "hello"
    curl_request http://http.stub/file.txt out.txt > /dev/null
    [ "$(cat out.txt)" = "hello" ]
    [ "${CURL_LAST_HTTP_CODE}" = "200" ]
    http_response http.stub/flaky "ok"
    printf '503' > "${STUB_DIR}/http/http.stub/flaky.code.1"
    run curl_request http://http.stub/flaky flaky.txt
    [ "${status}" -eq 0 ]
    [ "$(cat "${STUB_DIR}/http/http.stub/flaky.count")" -eq 2 ]
    run curl_request http://http.stub/missing missing.txt
    [ "${status}" -eq "${E_CURL_FAILURE}" ]
    [ "$(cat "${STUB_DIR}/http/http.stub/missing.count")" -eq 1 ]
    [[ "${output}" == *"HTTP status 404"* ]]
}

@test "curl request: network errors are retried, then reported" {
    load_bamf_helpers curl
    CURL_DEFAULT_RETRY_TIMER_MAX_SEC=0
    mkdir -p "${STUB_DIR}/http/http.stub"
    printf '7' > "${STUB_DIR}/http/http.stub/down.exit"
    run curl_request http://http.stub/down
    [ "${status}" -eq "${E_CURL_FAILURE}" ]
    [ "$(cat "${STUB_DIR}/http/http.stub/down.count")" -eq 3 ]
    [[ "${output}" == *"curl returned: 7 [COULDNT_CONNECT]"* ]]
    printf '3' > "${STUB_DIR}/http/http.stub/bad.exit"
    run curl_request http://http.stub/bad
    [ "$(cat "${STUB_DIR}/http/http.stub/bad.count")" -eq 1 ]
}

@test "curl request: password and POST body go through 0600 files, never the command line" {
    load_bamf_helpers curl
    http_response http.stub/api "{}"
    curl_request http://http.stub/api "" no '{"secret": "post-body"}' admin 'p@ss word' > /dev/null
    grep -qF 'CURLFILE netrc mode=600 content=[machine http.stub login admin password p@ss word]' "${STUB_LOG}"
    grep -qF 'CURLFILE data mode=600 content=[{"secret": "post-body"}]' "${STUB_LOG}"
    ! grep '^CURL:' "${STUB_LOG}" | grep -qF 'p@ss' || return 1
    ! grep '^CURL:' "${STUB_LOG}" | grep -qF 'post-body' || return 1
    ! grep -q '\[--insecure\]' "${STUB_LOG}" || return 1
}

@test "curl wait_for_http_status: waits until the service answers" {
    load_bamf_helpers curl
    http_response http.stub/healthz "ok"
    printf '503' > "${STUB_DIR}/http/http.stub/healthz.code.1"
    printf '503' > "${STUB_DIR}/http/http.stub/healthz.code.2"
    run curl_wait_for_http_status http://http.stub/healthz 200 5 0
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"[WAIT]"*"answered 503 (attempt 1 of 5)"* ]]
    [[ "${output}" == *"[PASS]"*"answered 200 (attempt 3 of 5)"* ]]
    run curl_wait_for_http_status http://http.stub/never 200 2 0
    [ "${status}" -eq "${E_OPERATION_TIMEOUT}" ]
}

###------------------------------------------------------------------------------------------------
## bamf.git.inc

@test "git github_repository_list: token in a 0600 header file, pages followed, sorted names" {
    load_bamf_helpers git
    export GITHUB_TOKEN="ghp_SECRET_TOKEN"
    local PAGE1="" INDEX
    for (( INDEX=1; INDEX<=100; INDEX++ )); do PAGE1+="{\"full_name\": \"acme/repo-$(printf '%03d' "${INDEX}")\"},"; done
    http_response "api.github.com/orgs/acme/repos?type=all&per_page=100&page=1" "[${PAGE1%,}]"
    http_response "api.github.com/orgs/acme/repos?type=all&per_page=100&page=2" '[{"full_name":"acme/aaa-last-page"}]'
    run git_github_repository_list acme repos.txt
    [ "${status}" -eq 0 ]
    [ "$(wc -l < repos.txt)" -eq 101 ]
    [ "$(head -1 repos.txt)" = "acme/aaa-last-page" ]
    grep -qF 'CURLFILE header mode=600 content=[Authorization: Bearer ghp_SECRET_TOKEN' "${STUB_LOG}"
    ! grep '^CURL:' "${STUB_LOG}" | grep -qF 'ghp_SECRET_TOKEN' || return 1
    [[ "${output}" != *"ghp_SECRET_TOKEN"* ]]
    unset GITHUB_TOKEN
    run git_github_repository_list acme repos.txt
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

@test "git tag_and_push / is_repo_up_to_date: against a local bare repository" {
    command -v git >/dev/null || skip "git is not installed"
    load_bamf_helpers git
    export PATH="${PATH#"${BAMF_ROOT}/tests/helpers/bin-tools:"}"
    git init -q --bare remote.git
    git init -q work
    git -C work -c user.name=t -c user.email=t@example.test commit -q --allow-empty -m first
    git -C work remote add origin "${BATS_TEST_TMPDIR}/remote.git"
    git -C work push -q -u origin HEAD 2>/dev/null
    git_is_repo_up_to_date work
    run git_tag_and_push work v1.0.0 HEAD "Release 1.0.0" origin no Tagger tagger@example.test
    [ "${status}" -eq 0 ]
    [ "$(git -C remote.git tag)" = "v1.0.0" ]
    [ "$(git -C remote.git for-each-ref --format='%(taggername)' refs/tags/v1.0.0)" = "Tagger" ]
    git -C work -c user.name=t -c user.email=t@example.test commit -q --allow-empty -m second
    run git_tag_and_push work v1.0.0
    [ "${status}" -eq "${E_GIT_FAILURE}" ]
    run git_tag_and_push work "bad tag"
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

###------------------------------------------------------------------------------------------------
## bamf.tar.inc

@test "tar: relative and absolute archives round trip, for each compression installed" {
    command -v tar >/dev/null || skip "tar is not installed"
    load_bamf_helpers tar
    export PATH="${PATH#"${BAMF_ROOT}/tests/helpers/bin-tools:"}"
    mkdir -p site/css
    printf 'index\n' > site/index.html
    printf 'body{}\n' > site/css/main.css
    local EXTENSION
    for EXTENSION in tar tar.gz tgz tar.bz2 tar.xz; do
        case "${EXTENSION}" in
            tar.gz|tgz) command -v gzip >/dev/null || continue;;
            tar.bz2) command -v bzip2 >/dev/null || command -v lbzip2 >/dev/null || continue;;
            tar.xz)  command -v xz >/dev/null || continue;;
        esac
        rm -rf restore
        tar_create "site.${EXTENSION}" site > /dev/null
        [ "$(tar -tf "site.${EXTENSION}" | sort | head -1)" = "./" ]
        tar_extract "site.${EXTENSION}" restore > /dev/null
        [ "$(cat restore/css/main.css)" = "body{}" ]
    done
    tar_create absolute.tar "${BATS_TEST_TMPDIR}/site" absolute > /dev/null
    [[ "$(tar -tf absolute.tar | head -1)" == "${BATS_TEST_TMPDIR#/}/site/"* ]]
    tar_create one-file.tar site/index.html > /dev/null
    [ "$(tar -tf one-file.tar)" = "index.html" ]
}

@test "tar: unknown extension, missing target and a failed archive left behind" {
    command -v tar >/dev/null || skip "tar is not installed"
    load_bamf_helpers tar
    export PATH="${PATH#"${BAMF_ROOT}/tests/helpers/bin-tools:"}"
    mkdir -p site
    run tar_create site.rar site
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    run tar_create site.tar missing
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
    mkdir -p readonly
    chmod 555 readonly
    if [ "$(id -u)" -ne 0 ]; then
        run tar_create readonly/site.tar site
        [ "${status}" -eq "${E_FILE_CONVERSION_FAILURE}" ]
        [ ! -e readonly/site.tar ]
    fi
}

###------------------------------------------------------------------------------------------------
## bamf.terraform.inc

@test "terraform remote workspace: runs in its own directory with -chdir; the script's directory is unchanged" {
    load_bamf_helpers terraform
    tool_response terraform '{"vpc_id": "vpc-0abc"}'
    local BEFORE="${PWD}"
    run terraform_s3_remote_workspace_to_file outputs.json prod my-state-bucket network/terraform.tfstate us-east-1
    [ "${status}" -eq 0 ]
    [ "${PWD}" = "${BEFORE}" ]
    grep -qF '[init] [-input=false] [-no-color]' "${STUB_LOG}"
    grep -qF '[-var] [workspace=prod] [-var] [region=us-east-1] [-var] [s3_bucket=my-state-bucket] [-var] [key=network/terraform.tfstate]' "${STUB_LOG}"
    grep -q '^CMD: terraform \[-chdir=.*\] \[output\] \[-json\] \[output\]' "${STUB_LOG}"
    [ "$(cat outputs.json)" = '{"vpc_id": "vpc-0abc"}' ]
    tool_response terraform "" 1
    run terraform_s3_remote_workspace_to_file outputs.json prod bucket "" us-east-1
    [ "${status}" -eq "${E_TERRAFORM_INIT_FAILURE}" ]
}

@test "terraform output_value: nested values, missing values, and names never become jq code" {
    command -v jq >/dev/null || skip "jq is not installed"
    load_bamf_helpers terraform
    export PATH="${PATH#"${BAMF_ROOT}/tests/helpers/bin-tools:"}"
    printf '{"vpc_id": "vpc-0abc", "vpc": {"cidr": "10.0.0.0/16"}, "subnets": ["a", "b"]}' > outputs.json
    [ "$(terraform_output_value outputs.json vpc_id)" = "vpc-0abc" ]
    terraform_output_value outputs.json vpc.cidr CIDR
    [ "${CIDR}" = "10.0.0.0/16" ]
    [ -z "$(terraform_output_value outputs.json missing)" ]
    [ -z "$(terraform_output_value outputs.json 'x) | halt_error(1')" ]
}

###------------------------------------------------------------------------------------------------
## bamf.npm.inc

@test "npm: package values, published versions, and an .npmrc that references the token" {
    load_bamf_helpers npm
    printf '{}' > package.json
    tool_response npm.pkg.get '"1.4.2"'
    npm_package_value VERSION version
    [ "${VERSION}" = "1.4.2" ]
    grep -qF 'CMD: npm [pkg] [get] [version]' "${STUB_LOG}"
    tool_response npm.pkg.get '{}'
    [ -z "$(npm_package_value "" missing)" ]
    tool_response npm.view "1.4.2"
    npm_does_version_exist "@acme/widget" 1.4.2 > /dev/null
    grep -qF 'CMD: npm [view] [@acme/widget@1.4.2] [version]' "${STUB_LOG}"
    tool_response npm.view "" 1
    run npm_does_version_exist "@acme/widget" 9.9.9
    [ "${status}" -eq "${E_NPM_PACKAGE_DOES_NOT_EXIST}" ]
    export NPM_TOKEN="npm_SECRET"
    npm_generate_npmrc https://npm.pkg.github.com . "" NPM_TOKEN > /dev/null
    [ "$(cat .npmrc)" = $'registry=https://npm.pkg.github.com/\n//npm.pkg.github.com/:_authToken=${NPM_TOKEN}' ]
    ! grep -q npm_SECRET .npmrc || return 1
}

###------------------------------------------------------------------------------------------------
## All helper files

@test "helper files: function lists match, every function documented, nothing runs when loaded" {
    local FILE LISTED DEFINED NAME
    for FILE in "${BAMF_ROOT}"/helpers/bamf.*.inc; do
        LISTED="$(sed -n '/^# function list:/,/^###/p' "${FILE}" | grep -oE '^#\s+- [a-z_0-9]+' | awk '{print $3}' | sort)"
        DEFINED="$(grep -oE '^function [a-z][a-z_0-9]*\(\)' "${FILE}" | sed -E 's/^function ([a-z_0-9]+)\(\)/\1/' | sort)"
        [ "${LISTED}" = "${DEFINED}" ] || { echo "list mismatch: ${FILE##*/}"; return 1; }
        for NAME in ${DEFINED}; do
            grep -q "^## FUNCTION: ${NAME}()$" "${FILE}" || { echo "undocumented: ${NAME}"; return 1; }
        done
    done
    use_tool_stubs
    run bash -c "source '${BAMF_ROOT}/bamf.inc'; for f in aws curl docker git npm pkg tar terraform; do source_include \$f; done"
    [ "${status}" -eq 0 ]
    [ -z "${output}" ]
    [ ! -s "${STUB_LOG}" ]
}
