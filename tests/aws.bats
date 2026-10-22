#!/usr/bin/env bats
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
# bamf.aws.inc, against stubs of the AWS CLI and curl (never contacts AWS)

# shellcheck disable=SC2034 # variables are read by the library functions under test
load test_helper

# same_file <a> <b>: true if the files have the same content (cmp is not installed everywhere)
same_file() {
    [ "$(sha256sum < "${1}")" = "$(sha256sum < "${2}")" ]
}

setup() {
    setup_bamf
    load_bamf_aws
    cd "${BATS_TEST_TMPDIR}" || return 1
}

###------------------------------------------------------------------------------------------------
## aws_cli

@test "aws_cli: network errors are retried, then succeed" {
    echo 2 > "${STUB_DIR}/sts.get-caller-identity.fail"
    stub_response sts.get-caller-identity $'123456789012\tarn:aws:iam::123456789012:role/ci'
    run aws_credentials_verify
    [ "${status}" -eq 0 ]
    [ "$(stub_calls sts get-caller-identity)" -eq 3 ]
    [[ "${output}" == *"attempt 1 of 3 failed (aws_cli_exit_code::255)"* ]]
    [[ "${output}" == *"Account:  [123456789012]"* ]]
}

@test "aws_cli: gives up after AWS_RETRY_COUNT attempts with E_AWS_FAILURE" {
    echo 9 > "${STUB_DIR}/sts.get-caller-identity.fail"
    AWS_RETRY_COUNT=2
    run aws_credentials_verify
    [ "${status}" -eq "${E_AWS_FAILURE}" ]
    [ "$(stub_calls sts get-caller-identity)" -eq 2 ]
}

@test "aws_cli: errors that will not change are not retried" {
    stub_response ssm.get-parameter "" 254 "An error occurred (AccessDeniedException) when calling the GetParameter operation"
    run aws_ssm_parameter_get VALUE /app/db
    [ "${status}" -eq "${E_AWS_FAILURE}" ]
    [ "$(stub_calls ssm get-parameter)" -eq 1 ]
    [[ "${output}" == *"AccessDeniedException"* ]]
    stub_response ssm.put-parameter "" 252 "aws: error: argument --name is required"
    run aws_ssm_parameter_put /app/db value
    [ "$(stub_calls ssm put-parameter)" -eq 1 ]
}

@test "aws_cli: throttling is retried" {
    stub_response ssm.get-parameter "" 254 "An error occurred (ThrottlingException) when calling the GetParameter operation: Rate exceeded"
    run aws_ssm_parameter_get VALUE /app/db
    [ "${status}" -eq "${E_AWS_FAILURE}" ]
    [ "$(stub_calls ssm get-parameter)" -eq 3 ]
}

@test "aws_cli: the last error is kept in AWS_CLI_LAST_ERROR" {
    stub_response ssm.get-parameter "" 254 "An error occurred (ParameterNotFound)"
    aws_ssm_parameter_get VALUE /app/db || true
    [[ "${AWS_CLI_LAST_ERROR}" == *"ParameterNotFound"* ]]
}

@test "aws_cli: the Region is passed when given, and left to the AWS CLI when not" {
    stub_response ssm.get-parameter "v"
    aws_ssm_parameter_get VALUE /app/db yes eu-west-1
    aws_ssm_parameter_get VALUE /app/db
    grep -q '^ARGV: \[--region\] \[eu-west-1\] \[ssm\] \[get-parameter\]' "${STUB_LOG}"
    grep -q '^ARGV: \[ssm\] \[get-parameter\]' "${STUB_LOG}"
}

###------------------------------------------------------------------------------------------------
## credentials

@test "aws_credentials_select: a profile is used, and credentials in the environment are cleared" {
    export AWS_ACCESS_KEY_ID=AKIAENVKEY AWS_SECRET_ACCESS_KEY=x AWS_SESSION_TOKEN=tok
    aws_credentials_select ci-role > "${BATS_TEST_TMPDIR}/out"
    grep -q "Using AWS credentials from profile \[ci-role\]" "${BATS_TEST_TMPDIR}/out"
    [ "${AWS_PROFILE}" = "ci-role" ]
    stub_response sts.get-caller-identity $'1\tarn'
    aws_credentials_verify > /dev/null
    grep -q 'ENV: AWS_PROFILE=ci-role AWS_ACCESS_KEY_ID=<unset> AWS_SESSION_TOKEN=<unset>' "${STUB_LOG}"
}

@test "aws_credentials_select: an unknown profile is E_BAD_ARGS" {
    run aws_credentials_select nope
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    [[ "${output}" == *"profile not found in the AWS configuration [nope]"* ]]
}

@test "aws_credentials_select: without a profile, the default chain is left alone and AWS is not called" {
    export AWS_ACCESS_KEY_ID=AKIAENVKEY
    run aws_credentials_select
    [[ "${output}" == *"Using AWS credentials from the environment"* ]]
    unset AWS_ACCESS_KEY_ID
    run aws_credentials_select
    [[ "${output}" == *"default credential chain"* ]]
    [ ! -s "${STUB_LOG}" ]
}

@test "sourcing bamf.aws.inc calls nothing and logs nothing" {
    run bash -c "source '${BAMF_ROOT}/bamf.inc'; source '${BAMF_ROOT}/helpers/bamf.aws.inc'"
    [ "${status}" -eq 0 ]
    [ -z "${output}" ]
    [ ! -s "${STUB_LOG}" ]
}

###------------------------------------------------------------------------------------------------
## SSM Parameter Store

@test "ssm get: the value is returned exactly and never logged" {
    stub_response ssm.get-parameter $'p@ss \'$(touch pwned)\' %s\n'
    run aws_ssm_parameter_get VALUE /app/db
    [[ "${output}" != *"p@ss"* ]]
    aws_ssm_parameter_get VALUE /app/db > /dev/null
    [ "${VALUE}" = "p@ss '\$(touch pwned)' %s" ]
    [ ! -e pwned ]
    grep -q '\[--with-decryption\] \[--query\] \[Parameter.Value\]' "${STUB_LOG}"
    aws_ssm_parameter_get VALUE /app/db no > /dev/null
    grep -q '\[--no-with-decryption\]' "${STUB_LOG}"
}

@test "ssm get silent: no output on success, the log on failure" {
    stub_response ssm.get-parameter "v"
    run aws_ssm_parameter_get_silent VALUE /app/db
    [ "${status}" -eq 0 ]
    [ -z "${output}" ]
    aws_ssm_parameter_get_silent VALUE /app/db
    [ "${VALUE}" = "v" ]
    stub_response ssm.get-parameter "" 254 "AccessDeniedException"
    run aws_ssm_parameter_get_silent VALUE /app/db
    [ "${status}" -eq "${E_AWS_FAILURE}" ]
    [[ "${output}" == *"AccessDeniedException"* ]]
}

@test "ssm put: the value goes through a 0600 file, never the command line or the log" {
    local SECRET='p@ss w0rd $HOME `x` "q" %s \n '
    run aws_ssm_parameter_put /app/db "${SECRET}" alias/app "Database password"
    [ "${status}" -eq 0 ]
    [[ "${output}" != *"p@ss w0rd"* ]]
    ! grep '^ARGV' "${STUB_LOG}" | grep -qF 'p@ss w0rd' || return 1
    grep -qF "FILEARG file mode=600 content=[${SECRET}]" "${STUB_LOG}"
    grep -q '\[--type\] \[SecureString\] \[--overwrite\] \[--key-id\] \[alias/app\] \[--description\] \[Database password\]' "${STUB_LOG}"
}

@test "ssm put: overwrites in place; does not delete the parameter first (which lost its history)" {
    aws_ssm_parameter_put /app/db value > /dev/null
    [ "$(stub_calls ssm delete-parameter)" -eq 0 ]
    aws_ssm_parameter_put /app/db value "" "" no > /dev/null
    [ "$(grep 'put-parameter' "${STUB_LOG}" | grep -c -- '--overwrite')" -eq 1 ]
}

@test "ssm put: a value ending in * is stored with its *" {
    aws_ssm_parameter_put /app/pattern 'abc*' > /dev/null
    grep -qF 'FILEARG file mode=600 content=[abc*]' "${STUB_LOG}"
}

@test "ssm put_file / get_file: base64 round trip, written mode 0400, replaced on the next run" {
    printf 'line1\nTOPSECRET\n' > secret.txt
    run aws_ssm_parameter_put_file /app/cert secret.txt
    [ "${status}" -eq 0 ]
    local ENCODED
    ENCODED="$(base64 -w 0 < secret.txt)"
    [[ "${output}" != *"${ENCODED}"* ]]
    grep -qF "FILEARG file mode=600 content=[${ENCODED}]" "${STUB_LOG}"
    stub_response ssm.get-parameter "${ENCODED}"
    aws_ssm_parameter_get_file out.txt /app/cert > /dev/null
    same_file secret.txt out.txt
    [ "$(stat -c %a out.txt)" = "400" ]
    aws_ssm_parameter_get_file out.txt /app/cert > /dev/null
    same_file secret.txt out.txt
}

@test "ssm put_file_multi_part: the path is cleared, every part goes through a file, parts join to the file" {
    head -c 7000 /dev/urandom > big.bin
    stub_response ssm.get-parameters-by-path $'/app/big/part_0001\n/app/big/part_0002\n/app/big/part_0003'
    run aws_ssm_parameter_put_file_multi_part /app/big big.bin
    [ "${status}" -eq 0 ]
    [ "$(stub_calls ssm delete-parameter)" -eq 3 ]
    [ "$(stub_calls ssm put-parameter)" -eq 3 ]
    grep -q '\[--name\] \[/app/big/part_0003\]' "${STUB_LOG}"
    [ "$(grep -a FILEARG "${STUB_LOG}" | sed -E 's/.*content=\[(.*)\]$/\1/' | tr -d '\n')" = "$(base64 -w 0 < big.bin)" ]
}

@test "ssm get_file_multi_part: parts sorted by name and joined" {
    printf 'hello multi-part world' > original.txt
    local B64
    B64="$(base64 -w 0 < original.txt)"
    stub_response ssm.get-parameters-by-path "$(printf '/app/f/part_0002\t%s\n/app/f/part_0001\t%s\n/app/f/other\tx\n' "${B64:10}" "${B64:0:10}")"
    run aws_ssm_parameter_get_file_multi_part out.txt /app/f
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"joined 2 parts"* ]]
    same_file original.txt out.txt
    stub_response ssm.get-parameters-by-path ""
    run aws_ssm_parameter_get_file_multi_part out2.txt /app/none
    [ "${status}" -eq "${E_AWS_RESOURCE_NOT_FOUND}" ]
}

@test "ssm parameters_to_properties_file / key_value_file: format, files skipped, mode 0600" {
    stub_response ssm.get-parameters-by-path "$(printf '/app/db_host\tdb.internal\n/app/db_password\tp=ss\n/app/files/cert\tQUJD\n')"
    aws_ssm_parameters_to_properties_file /app/ app.properties > /dev/null
    [ "$(stat -c %a app.properties)" = "600" ]
    [ "$(grep -v '^#' app.properties)" = $'db_host=db.internal\ndb_password=p=ss' ]
    [ "$(load_property_from_file "" db_password app.properties)" = "p=ss" ]
    aws_ssm_parameters_to_key_value_file /app kv.dat > /dev/null
    [ "$(load_key_value_from_file "" db_host kv.dat)" = "db.internal" ]
    ! grep -q cert kv.dat || return 1
    aws_ssm_parameters_to_key_value_file_multipart /app kv2.dat > /dev/null
    grep -q '^\[cert\]=QUJD$' kv2.dat
}

@test "ssm parameter_exists / delete: not found is not an error, other errors are" {
    stub_response ssm.get-parameter "" 254 "An error occurred (ParameterNotFound) when calling the GetParameter operation"
    run aws_ssm_parameter_exists /app/missing
    [ "${status}" -eq "${E_AWS_RESOURCE_NOT_FOUND}" ]
    run aws_ssm_parameter_delete /app/missing
    [ "${status}" -eq 0 ]
    [ "$(stub_calls ssm delete-parameter)" -eq 0 ]
    stub_response ssm.get-parameter "" 254 "An error occurred (AccessDeniedException)"
    run aws_ssm_parameter_delete /app/locked
    [ "${status}" -eq "${E_AWS_FAILURE}" ]
}

@test "ssm parameter_path_delete: refuses the root path" {
    run aws_ssm_parameter_path_delete /
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    [ ! -s "${STUB_LOG}" ]
}

@test "ssm parameter_tag: tags written as escaped JSON" {
    aws_ssm_parameter_tag /app/db 'team:web;note:say "hi":ok' > /dev/null
    grep -qF 'FILEARG file mode=600 content=[[{"Key": "team", "Value": "web"}, {"Key": "note", "Value": "say \"hi\":ok"}]]' "${STUB_LOG}"
    run aws_ssm_parameter_tag /app/db 'nocolon'
    [ "${status}" -eq "${E_BAD_ARGS}" ]
}

###------------------------------------------------------------------------------------------------
## KMS

@test "kms encrypt_string: plaintext through a 0600 fileb:// file, never the command line or the log" {
    stub_response kms.encrypt "Q0lQSEVSVEVYVA=="
    run aws_kms_encrypt_string RESULT 'kms-PLAINTEXT secret' alias/test
    [ "${status}" -eq 0 ]
    [[ "${output}" != *"kms-PLAINTEXT"* ]]
    grep -qF 'FILEARG fileb mode=600 content=[kms-PLAINTEXT secret]' "${STUB_LOG}"
    ! grep '^ARGV' "${STUB_LOG}" | grep -qF 'kms-PLAINTEXT' || return 1
    aws_kms_encrypt_string RESULT 'x' alias/test > /dev/null
    [ "${RESULT}" = "Q0lQSEVSVEVYVA==" ]
}

@test "kms decrypt_string: returns the plaintext, never logs it" {
    stub_response kms.decrypt "$(printf 'top secret' | base64)"
    run aws_kms_decrypt_string RESULT "Q0lQSEVSVEVYVA=="
    [[ "${output}" != *"top secret"* ]]
    aws_kms_decrypt_string RESULT "Q0lQSEVSVEVYVA==" > /dev/null
    [ "${RESULT}" = "top secret" ]
    run aws_kms_decrypt_string RESULT "!!!"
    [ "${status}" -eq "${E_BASE64_CONVERSION_FAILURE}" ]
}

kms_stub_data_key() {
    DATA_KEY_PLAINTEXT="$(head -c 32 /dev/urandom | base64 -w 0)"
    stub_response kms.generate-data-key "$(printf '%s\t%s' "${DATA_KEY_PLAINTEXT}" "RU5DUllQVEVEREFUQUtFWQ==")"
    stub_response kms.decrypt "${DATA_KEY_PLAINTEXT}"
}

@test "kms encrypt_file / decrypt_file: round trip, data key never on a command line" {
    command -v openssl >/dev/null || skip "openssl is not installed"
    openssl enc -help 2>&1 | grep -q -- -pbkdf2 || skip "OpenSSL older than 1.1.1"
    kms_stub_data_key
    head -c 5000 /dev/urandom > data.bin
    mkdir -p encrypted decrypted
    run aws_kms_encrypt_file data.bin encrypted alias/backups
    [ "${status}" -eq 0 ]
    [ -f encrypted/data.bin.enc ]
    ! grep -qF "${DATA_KEY_PLAINTEXT}" "${STUB_LOG}" || return 1
    run aws_kms_decrypt_file encrypted/data.bin.enc decrypted
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"(format 2)"* ]]
    same_file data.bin decrypted/data.bin
}

@test "kms encrypt_file: no copy of the data key is left on disk when the script exits" {
    command -v openssl >/dev/null || skip "openssl is not installed"
    openssl enc -help 2>&1 | grep -q -- -pbkdf2 || skip "OpenSSL older than 1.1.1"
    kms_stub_data_key
    head -c 100 /dev/urandom > data.bin
    mkdir -p encrypted
    run bash -c "SELF_IDENTITY=t; source '${BAMF_ROOT}/bamf.inc'; REQUIRED_SOURCE_FILES+=( aws ); start_logic; aws_kms_encrypt_file data.bin encrypted alias/backups || exit_logic \$?; exit_logic 0"
    [ "${status}" -eq 0 ]
    [ -f encrypted/data.bin.enc ]
    [ -z "$(ls -A "${BAMF_DIRECTORY_TEMPORARY}")" ]
    ! grep -rqF "${DATA_KEY_PLAINTEXT}" "${BATS_TEST_TMPDIR}" --exclude-dir=stub || return 1
}

@test "kms decrypt_file: reads format 1 files (no format line, default digest)" {
    command -v openssl >/dev/null || skip "openssl is not installed"
    kms_stub_data_key
    mkdir -p bundle decrypted
    printf 'old secret data\n' > old.txt
    openssl enc -e -aes-256-cbc -md sha256 -pass "pass:${DATA_KEY_PLAINTEXT}" -in old.txt -out bundle/payload 2>/dev/null
    printf '[filename]=old.txt\n[envelope_key]=RU5DUllQVEVEREFUQUtFWQ==\n[kms_key]=alias/x\n' > bundle/metadata
    tar -czf old.txt.enc -C bundle .
    run aws_kms_decrypt_file old.txt.enc decrypted
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"(format 1)"* ]]
    same_file old.txt decrypted/old.txt
}

@test "kms decrypt_file: a file name in the metadata cannot escape the output directory" {
    command -v openssl >/dev/null || skip "openssl is not installed"
    kms_stub_data_key
    mkdir -p bundle out/inner
    printf 'x\n' > x.txt
    openssl enc -e -aes-256-cbc -md sha256 -pass "pass:${DATA_KEY_PLAINTEXT}" -in x.txt -out bundle/payload 2>/dev/null
    printf '[filename]=../escaped.txt\n[envelope_key]=RU5DUllQVEVEREFUQUtFWQ==\n' > bundle/metadata
    tar -czf crafted.enc -C bundle .
    run aws_kms_decrypt_file crafted.enc out/inner
    [ "${status}" -eq 0 ]
    [ -f out/inner/escaped.txt ]
    [ ! -e out/escaped.txt ]
}

###------------------------------------------------------------------------------------------------
## Route 53

@test "route53 upsert: change batch is valid JSON with escaped values" {
    run aws_route53_upsert_record Z123 Example.com TXT 60 '"v=spf1 include:_spf.example.com -all";"second \ value"'
    [ "${status}" -eq 0 ]
    grep -qF '"Name": "example.com.", "Type": "TXT", "TTL": 60' "${STUB_LOG}"
    grep -qF '{"Value": "\"v=spf1 include:_spf.example.com -all\""}, {"Value": "\"second \\ value\""}' "${STUB_LOG}"
    if command -v python3 >/dev/null; then
        local BATCH_FILE
        BATCH_FILE="$(grep -o '\[file://[^]]*\]' "${STUB_LOG}" | head -1 | sed -E 's/^\[file:\/\///; s/\]$//')"
        python3 -c 'import json,sys; c=json.load(open(sys.argv[1]))["Changes"][0]["ResourceRecordSet"]["ResourceRecords"]; assert c[1]["Value"] == "\"second \\ value\"", c' "${BATCH_FILE}"
    fi
}

@test "route53 upsert: weighted records need a set identifier" {
    run aws_route53_upsert_record Z123 www.example.com A 60 192.0.2.10 10
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    run aws_route53_upsert_record Z123 www.example.com A 60 "192.0.2.10;192.0.2.11" 10 blue
    [ "${status}" -eq 0 ]
    grep -qF '"Weight": 10, "SetIdentifier": "blue"' "${STUB_LOG}"
}

@test "route53 delete: the change batch comes from the record sets themselves; nothing to delete is fine" {
    stub_response route53.list-resource-record-sets '{"Changes": [{"Action": "DELETE", "ResourceRecordSet": {"Name": "www.example.com.", "Type": "A"}}]}'
    run aws_route53_delete_record Z123 WWW.example.com "" A
    [ "${status}" -eq 0 ]
    grep -qF "[{Changes: ResourceRecordSets[?Name=='www.example.com.' && Type=='A'].{Action: 'DELETE', ResourceRecordSet: @}}]" "${STUB_LOG}"
    [ "$(stub_calls route53 change-resource-record-sets)" -eq 1 ]
    stub_response route53.list-resource-record-sets $'{\n    "Changes": []\n}'
    run aws_route53_delete_record Z123 gone.example.com
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"nothing to delete"* ]]
    [ "$(stub_calls route53 change-resource-record-sets)" -eq 1 ]
}

@test "route53 get_record: exact name, all values, alias and weighted records" {
    stub_response route53.list-resource-record-sets "$(printf 'www.example.com.\tA\t60\tNone\tNone\tNone\t192.0.2.10;192.0.2.11\nwww.example.com.\tAAAA\tNone\tNone\tNone\tdualstack.lb.example.\t\nwww.example.com.\tA\t60\t10\tblue\tNone\t192.0.2.20\n')"
    aws_route53_get_record RECORDS Z123 www.example.com > /dev/null
    [ "${#RECORDS[@]}" -eq 3 ]
    [ "${RECORDS[0]}" = "www.example.com.|A|60|192.0.2.10;192.0.2.11||" ]
    [ "${RECORDS[1]}" = "www.example.com.|AAAA||ALIAS:dualstack.lb.example.||" ]
    [ "${RECORDS[2]}" = "www.example.com.|A|60|192.0.2.20|10|blue" ]
    grep -qF "ResourceRecordSets[?Name=='www.example.com.']" "${STUB_LOG}"
}

###------------------------------------------------------------------------------------------------
## CodeDeploy

@test "codedeploy create_deployment: bundle type from the extension, deployment ID returned" {
    stub_response deploy.create-deployment "d-ABC123"
    aws_codedeploy_create_deployment DEPLOYMENT_ID app group CodeDeployDefault.OneAtATime bucket releases/app-1.2.3.tar.gz "release 1.2.3" > /dev/null
    [ "${DEPLOYMENT_ID}" = "d-ABC123" ]
    grep -qF '[bucket=bucket,key=releases/app-1.2.3.tar.gz,bundleType=tgz]' "${STUB_LOG}"
    run aws_codedeploy_create_deployment DEPLOYMENT_ID app group config bucket app.rar
    [ "${status}" -eq "${E_BAD_ARGS}" ]
    stub_response deploy.create-deployment "" 254 "ApplicationDoesNotExistException"
    run aws_codedeploy_create_deployment DEPLOYMENT_ID app group config bucket app.zip
    [ "${status}" -eq "${E_CODEDEPLOY_CREATE_DEPLOYMENT_FAILURE}" ]
}

@test "codedeploy wait_for_deployment: follows the deployment to success, failure or timeout" {
    printf 'InProgress\t1\t1\t0\t0\t0\tNone\tNone' > "${STUB_DIR}/deploy.get-deployment.out.1"
    printf 'Succeeded\t0\t0\t2\t0\t0\tNone\tNone' > "${STUB_DIR}/deploy.get-deployment.out.2"
    run aws_codedeploy_wait_for_deployment d-ABC123 "" 5 0
    [ "${status}" -eq 0 ]
    [[ "${output}" == *"InProgress (Pending::1 / InProgress::1"* ]]
    [[ "${output}" == *"deployment succeeded [d-ABC123]"* ]]
    rm -f "${STUB_DIR}"/deploy.get-deployment.*
    stub_response deploy.get-deployment $'Failed\t0\t0\t1\t1\t0\tHEALTH_CONSTRAINTS\tThe overall deployment failed'
    run aws_codedeploy_wait_for_deployment d-ABC123 "" 5 0
    [ "${status}" -eq "${E_CODEDEPLOY_DEPLOYMENT_FAILURE}" ]
    [[ "${output}" == *"Error: [HEALTH_CONSTRAINTS - The overall deployment failed]"* ]]
    stub_response deploy.get-deployment $'InProgress\t1\t0\t0\t0\t0\tNone\tNone'
    run aws_codedeploy_wait_for_deployment d-ABC123 "" 2 0
    [ "${status}" -eq "${E_OPERATION_TIMEOUT}" ]
}

@test "codedeploy get_deployed_revisions: file names, one per line" {
    stub_response deploy.list-application-revisions $'releases/app-1.0.0.zip\nreleases/app-1.1.0.zip\n'
    aws_codedeploy_get_deployed_revisions app revisions.txt > /dev/null
    [ "$(cat revisions.txt)" = $'app-1.0.0.zip\napp-1.1.0.zip' ]
}

@test "codedeploy get_current_revision_json: never deployed is E_AWS_RESOURCE_NOT_FOUND" {
    stub_response deploy.get-deployment-group "null"
    run aws_codedeploy_get_current_revision_json revision.json app group
    [ "${status}" -eq "${E_AWS_RESOURCE_NOT_FOUND}" ]
}

###------------------------------------------------------------------------------------------------
## EC2 instance metadata

@test "metadata: values read with one IMDSv2 token" {
    mkdir -p "${STUB_DIR}/imds/meta-data/network/interfaces/macs/0a:1b:2c:3d:4e:5f" "${STUB_DIR}/imds/dynamic/instance-identity"
    printf 'i-0123456789abcdef0' > "${STUB_DIR}/imds/meta-data/instance-id"
    printf '0a:1b:2c:3d:4e:5f' > "${STUB_DIR}/imds/meta-data/mac"
    printf 'vpc-0abc' > "${STUB_DIR}/imds/meta-data/network/interfaces/macs/0a:1b:2c:3d:4e:5f/vpc-id"
    printf '{\n  "accountId" : "123456789012",\n  "region" : "us-east-2"\n}' > "${STUB_DIR}/imds/dynamic/instance-identity/document"
    aws_metadata_instance_id INSTANCE_ID
    aws_metadata_vpc_id VPC_ID
    aws_metadata_account_id ACCOUNT_ID
    aws_metadata_region REGION
    [ "${INSTANCE_ID}" = "i-0123456789abcdef0" ]
    [ "${VPC_ID}" = "vpc-0abc" ]
    [ "${ACCOUNT_ID}" = "123456789012" ]
    [ "${REGION}" = "us-east-2" ]
    [ "$(cat "${STUB_DIR}/imds.token.count")" -eq 1 ]
}

@test "metadata vpc_dns: the VPC base address plus two, for any CIDR size" {
    local MAC_DIR="${STUB_DIR}/imds/meta-data/network/interfaces/macs/0a:1b"
    mkdir -p "${MAC_DIR}"
    printf '0a:1b' > "${STUB_DIR}/imds/meta-data/mac"
    printf '10.1.64.0/18' > "${MAC_DIR}/vpc-ipv4-cidr-block"
    [ "$(aws_metadata_vpc_dns)" = "10.1.64.2" ]
    printf '172.31.0.0/16' > "${MAC_DIR}/vpc-ipv4-cidr-block"
    [ "$(aws_metadata_vpc_dns)" = "172.31.0.2" ]
    printf '10.0.255.254/31' > "${MAC_DIR}/vpc-ipv4-cidr-block"
    [ "$(aws_metadata_vpc_dns)" = "10.1.0.0" ]
}

@test "metadata hostname: falls back to the private name when there is no public one" {
    mkdir -p "${STUB_DIR}/imds/meta-data"
    printf 'ip-10-0-0-5.ec2.internal' > "${STUB_DIR}/imds/meta-data/local-hostname"
    run aws_metadata_hostname_public
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
    [ -z "${output}" ]
    [ "$(aws_metadata_hostname)" = "ip-10-0-0-5.ec2.internal" ]
}

@test "metadata: not on EC2 is E_AWS_FAILURE with a clear message" {
    run bash -c "PATH=/usr/bin:/bin; source '${BAMF_ROOT}/bamf.inc'; source '${BAMF_ROOT}/helpers/bamf.aws.inc'; AWS_METADATA_URL=http://127.0.0.1:9; aws_metadata_instance_id X"
    [ "${status}" -eq "${E_AWS_FAILURE}" ] || [ "${status}" -eq "${E_MISSING_DEPENDENCIES}" ]
    [[ "${output}" == *"could not get an instance metadata token"* || "${output}" == *"curl is not installed"* ]]
}

###------------------------------------------------------------------------------------------------
## S3, EC2, ENI, CloudFront

@test "s3 upload: the bucket's default encryption unless asked; no Reduced Redundancy" {
    printf 'x' > f.txt
    aws_s3_cp_upload bucket /path/f.txt f.txt > /dev/null
    grep -qF 'ARGV: [s3] [cp] [--only-show-errors] [f.txt] [s3://bucket/path/f.txt]' "${STUB_LOG}"
    aws_s3_cp_upload bucket path/f.txt f.txt AES256 STANDARD_IA > /dev/null
    grep -qF '[--sse] [AES256] [--storage-class] [STANDARD_IA] [f.txt]' "${STUB_LOG}"
    ! grep -q REDUCED_REDUNDANCY "${STUB_LOG}" || return 1
    run aws_s3_cp_upload bucket key missing.txt
    [ "${status}" -eq "${E_OBJECT_NOT_FOUND}" ]
}

@test "s3 sync: options, excludes then includes, patterns kept whole" {
    mkdir -p site
    aws_s3_sync_upload bucket www site yes no "" "" '*.tmp;logs/*' 'logs/keep me.txt' > /dev/null
    grep -qF '[s3] [sync] [--only-show-errors] [--delete] [--exclude] [*.tmp] [--exclude] [logs/*] [--include] [logs/keep me.txt] [site] [s3://bucket/www]' "${STUB_LOG}"
}

@test "ec2 tag_resource: values with commas and quotes, as JSON" {
    aws_ec2_tag_resource i-0abc Name 'web, "blue"' > /dev/null
    grep -qF 'FILEARG file mode=600 content=[[{"Key": "Name", "Value": "web, \"blue\""}]]' "${STUB_LOG}"
}

@test "ec2 vpc_route_update: replace, or create when there is no route" {
    aws_ec2_vpc_route_update rtb-1 i-1 > /dev/null
    [ "$(stub_calls ec2 create-route)" -eq 0 ]
    stub_response ec2.replace-route "" 254 "InvalidParameterValue: There is no route defined for '0.0.0.0/0'"
    aws_ec2_vpc_route_update rtb-1 i-1 > /dev/null
    [ "$(stub_calls ec2 create-route)" -eq 1 ]
}

@test "eni attach: detaches from the old instance and waits first; attaches directly when free" {
    stub_response ec2.describe-network-interfaces "eni-attach-1"
    aws_eni_attach eni-1 i-new > /dev/null
    [ "$(stub_calls ec2 detach-network-interface)" -eq 1 ]
    [ "$(stub_calls ec2 wait)" -eq 1 ]
    [ "$(stub_calls ec2 attach-network-interface)" -eq 1 ]
    grep -qF '[--device-index] [3]' "${STUB_LOG}"
    stub_response ec2.describe-network-interfaces "None"
    aws_eni_attach eni-1 i-new 2 > /dev/null
    [ "$(stub_calls ec2 detach-network-interface)" -eq 1 ]
    [ "$(stub_calls ec2 attach-network-interface)" -eq 2 ]
}

@test "cloudfront invalidate_cache: several paths" {
    stub_response cloudfront.create-invalidation "I2J3K4"
    run aws_cloudfront_invalidate_cache E123 '/index.html;/css/*'
    [ "${status}" -eq 0 ]
    grep -qF '[--paths] [/index.html] [/css/*]' "${STUB_LOG}"
    [[ "${output}" == *"invalidation requested [I2J3K4]"* ]]
}

###------------------------------------------------------------------------------------------------
## Library checks

@test "bamf.aws.inc: the function list in the header matches the functions defined" {
    local LISTED DEFINED
    LISTED="$(sed -n '/^# function list:/,/^###/p' "${BAMF_ROOT}/helpers/bamf.aws.inc" | grep -oE '^#\s+- [a-z_0-9]+' | awk '{print $3}' | sort)"
    DEFINED="$(grep -oE '^function [a-z][a-z_0-9]*\(\)' "${BAMF_ROOT}/helpers/bamf.aws.inc" | sed -E 's/^function ([a-z_0-9]+)\(\)/\1/' | sort)"
    [ "${LISTED}" = "${DEFINED}" ]
}

@test "bamf.aws.inc: loaded by name through REQUIRED_SOURCE_FILES" {
    run bash -c "SELF_IDENTITY=t; source '${BAMF_ROOT}/bamf.inc'; REQUIRED_SOURCE_FILES+=( aws ); start_logic_quiet; lookup_exit_code 81; exit_logic_quiet 0"
    [ "${status}" -eq 0 ]
    [ "${output}" = "E_AWS_FAILURE / There was a failure communicating with AWS" ]
}

###------------------------------------------------------------------------------------------------
## Arguments kept compatible with earlier versions

@test "compatible: route53 delete takes (zone, name, region, type), so an old call with a region still deletes" {
    stub_response route53.list-resource-record-sets '{"Changes": [{"Action": "DELETE", "ResourceRecordSet": {"Name": "www.example.com.", "Type": "A"}}]}'
    aws_route53_delete_record Z123 www.example.com us-east-1 > /dev/null
    grep -q '^ARGV: \[--region\] \[us-east-1\] \[route53\] \[list-resource-record-sets\]' "${STUB_LOG}"
    grep -qF "ResourceRecordSets[?Name=='www.example.com.'].{" "${STUB_LOG}"
}

@test "compatible: ssm delete takes (name, skip exists check, region)" {
    aws_ssm_parameter_delete /app/db yes eu-west-1 > /dev/null
    [ "$(stub_calls ssm get-parameter)" -eq 0 ]
    grep -q '^ARGV: \[--region\] \[eu-west-1\] \[ssm\] \[delete-parameter\] \[--name\] \[/app/db\]' "${STUB_LOG}"
}

@test "compatible: put_file_multi_part takes (path, file, key, description, replace, region)" {
    printf 'data' > f.bin
    stub_response ssm.get-parameters-by-path "1"
    run aws_ssm_parameter_put_file_multi_part /app/f f.bin "" "" no eu-west-1
    [ "${status}" -eq "${E_OBJECT_ALREADY_EXISTS}" ]
    [ "$(stub_calls ssm put-parameter)" -eq 0 ]
    stub_response ssm.get-parameters-by-path ""
    aws_ssm_parameter_put_file_multi_part /app/f f.bin "" "" yes eu-west-1 > /dev/null
    grep -q '^ARGV: \[--region\] \[eu-west-1\] \[ssm\] \[put-parameter\] \[--name\] \[/app/f/part_0001\]' "${STUB_LOG}"
}

@test "compatible: kms decrypt_string takes (variable, ciphertext, unused, region)" {
    stub_response kms.decrypt "$(printf 'x' | base64)"
    aws_kms_decrypt_string RESULT "Q0lQSEVSVEVYVA==" yes us-west-2 > /dev/null
    grep -q '^ARGV: \[--region\] \[us-west-2\] \[kms\] \[decrypt\]' "${STUB_LOG}"
}

@test "compatible: s3 upload understands the old yes/no flags; Reduced Redundancy is never requested" {
    printf 'x' > f.txt
    aws_s3_cp_upload bucket key f.txt yes yes > /dev/null
    grep -qF 'ARGV: [s3] [cp] [--only-show-errors] [--sse] [AES256] [f.txt] [s3://bucket/key]' "${STUB_LOG}"
    aws_s3_cp_upload bucket key f.txt no no > /dev/null
    grep -qF 'ARGV: [s3] [cp] [--only-show-errors] [f.txt] [s3://bucket/key]' "${STUB_LOG}"
    ! grep -q REDUCED_REDUNDANCY "${STUB_LOG}" || return 1
}

@test "compatible: cloudfront takes (id, path, region)" {
    stub_response cloudfront.create-invalidation "I1"
    aws_cloudfront_invalidate_cache E123 "/*" us-east-1 > /dev/null
    grep -qF 'ARGV: [--region] [us-east-1] [cloudfront] [create-invalidation] [--distribution-id] [E123] [--paths] [/*]' "${STUB_LOG}"
}
