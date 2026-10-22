#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  aws.sh
# purpose: Checks bamf.aws.inc against a real AWS account: SSM, KMS, Route 53, S3 and STS
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# What it creates, all named bamf-live-<random>, and removes when it exits (however it exits):
#          - SSM parameters under /bamf-live-<random>/
#          - a KMS key (scheduled for deletion after 7 days, the shortest wait AWS allows)
#          - a public Route 53 hosted zone bamf-live-<random>.test (deleted within minutes, so it is
#            not billed: AWS bills a hosted zone only when it exists for more than 12 hours)
#          - an S3 bucket with a few small objects
# Run it only in an account meant for testing, as a maintainer. It is not part of ci/test.sh.
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="aws_live_test"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-a <account id> : The AWS account the test may run in (required; it stops if the credentials are for another)'
    '-r <region> : Region (optional, defaults to us-east-1)'
    '-P <profile> : AWS profile (optional, defaults to the AWS CLI default credentials)'
)

###------------------------------------------------------------------------------------------------
## Load BAMF
# shellcheck source=../../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../../bamf.inc" || exit 3
REQUIRED_SOURCE_FILES+=( aws )
REQUIRED_EXECUTABLES+=( 'openssl' 'tar' )

###------------------------------------------------------------------------------------------------
## Variables
EXPECTED_ACCOUNT=""
REGION="us-east-1"
PROFILE=""
NAME=""
FAILURES=0
CHECKS=0
KMS_KEY_ID=""
ZONE_ID=""
BUCKET=""

###------------------------------------------------------------------------------------------------
## Functions

## FUNCTION: check()
## - Records one check: logs PASS or FAIL with a description
## - Arguments
##   - $1: Exit code of the check (0 passes)
##   - $2: Description
function check() {
    CHECKS=$(( CHECKS + 1 ))
    if [[ "${1}" == "0" ]]; then
        log_status PASS "${2}"
    else
        log_status FAIL "${2} (returned::${1})"
        FAILURES=$(( FAILURES + 1 ))
    fi
}

## FUNCTION: check_true()
## - Records one check by running a command or test: PASS if it succeeds
## - Arguments
##   - $1: Description
##   - $2+: Command (example: test -f file)
function check_true() {
    local TMP_DESCRIPTION="${1}"
    shift
    if "$@"; then check 0 "${TMP_DESCRIPTION}"; else check 1 "${TMP_DESCRIPTION}"; fi
}

## FUNCTION: same()
## - Returns 0 if two values are equal, 1 if not
## - Arguments
##   - $1: Value
##   - $2: Expected value
function same() {
    [[ "${1}" == "${2}" ]]
}

## FUNCTION: remove_ssm_parameters()
## - Exit function: deletes the test's SSM parameters
function remove_ssm_parameters() {
    aws_ssm_parameter_path_delete "/${NAME}" "${REGION}"
}

## FUNCTION: remove_kms_key()
## - Exit function: schedules the test's KMS key for deletion
function remove_kms_key() {
    aws_cli "" "Live Test (Remove KMS Key)" --region "${REGION}" kms schedule-key-deletion --key-id "${KMS_KEY_ID}" --pending-window-in-days 7
}

## FUNCTION: remove_hosted_zone()
## - Exit function: deletes the test's records, then its hosted zone
function remove_hosted_zone() {
    local TMP_RECORD=""
    for TMP_RECORD in "multi.${NAME}.test" "txt.${NAME}.test" "weighted.${NAME}.test"; do
        aws_route53_delete_record "${ZONE_ID}" "${TMP_RECORD}" || true
    done
    aws_cli "" "Live Test (Remove Hosted Zone)" route53 delete-hosted-zone --id "${ZONE_ID}"
}

## FUNCTION: remove_bucket()
## - Exit function: empties and deletes the test's S3 bucket
function remove_bucket() {
    aws_cli "" "Live Test (Remove Bucket)" --region "${REGION}" s3 rb "s3://${BUCKET}" --force
}

###------------------------------------------------------------------------------------------------
## Main
while getopts "hVa:r:P:" OPTION; do
    case "${OPTION}" in
        a) EXPECTED_ACCOUNT="${OPTARG}";;
        r) REGION="${OPTARG}";;
        P) PROFILE="${OPTARG}";;
        h) usage; exit 0;;
        V) return_script_version "${0}"; printf '\n'; exit 0;;
        *) usage; exit "${E_BAD_ARGS}";;
    esac
done

start_logic

line_break
log_highlight "Initialize"
if [[ ! "${EXPECTED_ACCOUNT}" =~ ^[0-9]{12}$ ]]; then
    log_error "The account the test may run in is required (-a <account id>)"
    exit_logic "${E_BAD_ARGS}"
fi
aws_credentials_select "${PROFILE}" || exit_logic $?
aws_credentials_verify ACCOUNT || exit_logic $?
if [[ "${ACCOUNT}" != "${EXPECTED_ACCOUNT}" ]]; then
    log_error "The credentials are for account ${ACCOUNT}, not ${EXPECTED_ACCOUNT}; nothing was created"
    exit_logic "${E_NOT_AUTHORIZED}"
fi
NAME="bamf-live-$(generate_uuid "" 10)"
generate_temp_directory WORK "live test files"
log "- Name:   [${NAME}]"
log "- Region: [${REGION}]"

line_break
log_highlight "STS"
aws_metadata_account_id_from_cli VALUE; check $? "account ID from the AWS CLI"
same "${VALUE}" "${ACCOUNT}"; check $? "account ID matches"

line_break
log_highlight "SSM Parameter Store"
add_exit_function remove_ssm_parameters
SECRET=$'p@ss \'$(touch pwned)\' "q" %s \\ `x`'
aws_ssm_parameter_put "/${NAME}/db/password" "${SECRET}" "" "BAMF live test" yes "${REGION}"; check $? "put a SecureString with quotes and shell syntax"
aws_ssm_parameter_get VALUE "/${NAME}/db/password" yes "${REGION}"; check $? "get it back"
same "${VALUE}" "${SECRET}"; check $? "the value is exact"
check_true "the value was never run as code" test ! -e pwned
RESULT=0; aws_ssm_parameter_put "/${NAME}/db/password" "changed" "" "" no "${REGION}" >/dev/null 2>&1 || RESULT=$?
check_true "overwrite=no refuses an existing parameter" test "${RESULT}" -ne 0
aws_ssm_parameter_put "/${NAME}/db/password" "changed" "" "" yes "${REGION}"; check $? "overwrite=yes replaces it"
aws_cli "${WORK}/history" "Live Test (History)" --region "${REGION}" ssm get-parameter-history --name "/${NAME}/db/password" --query 'length(Parameters)' --output text
same "$(<"${WORK}/history")" "2"; check $? "the history is kept (2 versions; the old code deleted it)"
aws_ssm_parameter_put "/${NAME}/db/host" "db.internal" "" "" yes "${REGION}"; check $? "put a second parameter"
aws_ssm_parameter_tag "/${NAME}/db/host" "team:web;note:a b:c/d=e+f-g@h" "${REGION}"; check $? "tag a parameter (spaces, and a colon in the value)"
aws_cli "${WORK}/tags" "Live Test (Tags)" --region "${REGION}" ssm list-tags-for-resource --resource-type Parameter --resource-id "/${NAME}/db/host" --query "TagList[?Key=='note'].Value" --output text
same "$(<"${WORK}/tags")" "a b:c/d=e+f-g@h"; check $? "the tag value is exact"
RESULT=0; aws_ssm_parameter_tag "/${NAME}/db/host" 'note:say "hi"' "${REGION}" >/dev/null 2>&1 || RESULT=$?
check_true "a value AWS rejects (quotes) fails with E_AWS_FAILURE, without retrying" test "${RESULT}" -eq "${E_AWS_FAILURE}"
aws_ssm_parameters_to_properties_file "/${NAME}/db" "${WORK}/db.properties" yes "${REGION}"; check $? "parameters to a properties file"
same "$(load_property_from_file "" host "${WORK}/db.properties")" "db.internal"; check $? "the properties file reads back"
aws_ssm_parameter_exists "/${NAME}/db/host" "${REGION}"; check $? "exists: an existing parameter"
aws_ssm_parameter_exists "/${NAME}/missing" "${REGION}"; same "$?" "${E_AWS_RESOURCE_NOT_FOUND}"; check $? "exists: a missing parameter is E_AWS_RESOURCE_NOT_FOUND"
head -c 2000 /dev/urandom > "${WORK}/small.bin"
aws_ssm_parameter_put_file "/${NAME}/files/small" "${WORK}/small.bin" "" "" yes "${REGION}"; check $? "put a file"
aws_ssm_parameter_get_file "${WORK}/small.out" "/${NAME}/files/small" yes "${REGION}"; check $? "get the file"
same "$(return_file_sha256sum "${WORK}/small.out")" "$(return_file_sha256sum "${WORK}/small.bin")"; check $? "the file is exact"
head -c 10000 /dev/urandom > "${WORK}/large.bin"
aws_ssm_parameter_put_file_multi_part "/${NAME}/files/large" "${WORK}/large.bin" "" "" yes "${REGION}"; check $? "put a 10 KB file in parts"
aws_ssm_parameter_get_file_multi_part "${WORK}/large.out" "/${NAME}/files/large" yes "${REGION}"; check $? "get the parts"
same "$(return_file_sha256sum "${WORK}/large.out")" "$(return_file_sha256sum "${WORK}/large.bin")"; check $? "the parts join to the exact file"

line_break
log_highlight "KMS"
aws_cli "${WORK}/key" "Live Test (Create KMS Key)" --region "${REGION}" kms create-key --description "BAMF live test ${NAME}" --query KeyMetadata.KeyId --output text; check $? "create a test key"
KMS_KEY_ID="$(<"${WORK}/key")"
add_exit_function remove_kms_key
aws_kms_encrypt_string CIPHERTEXT "${SECRET}" "${KMS_KEY_ID}" "${REGION}"; check $? "encrypt a string"
aws_kms_decrypt_string VALUE "${CIPHERTEXT}" "" "${REGION}"; check $? "decrypt it"
same "${VALUE}" "${SECRET}"; check $? "the string is exact"
aws_kms_generate_envelope_key VALUE "${KMS_KEY_ID}" "${REGION}"; check $? "generate an encrypted data key"
head -c 50000 /dev/urandom > "${WORK}/data.bin"
mkdir -p "${WORK}/encrypted" "${WORK}/decrypted"
aws_kms_encrypt_file "${WORK}/data.bin" "${WORK}/encrypted" "${KMS_KEY_ID}" "${REGION}"; check $? "encrypt a file"
aws_kms_decrypt_file "${WORK}/encrypted/data.bin.enc" "${WORK}/decrypted" "${REGION}"; check $? "decrypt the file"
same "$(return_file_sha256sum "${WORK}/decrypted/data.bin")" "$(return_file_sha256sum "${WORK}/data.bin")"; check $? "the file is exact"

line_break
log_highlight "Route 53"
aws_cli "${WORK}/zone" "Live Test (Create Hosted Zone)" route53 create-hosted-zone --name "${NAME}.test" \
    --caller-reference "${NAME}" --query 'HostedZone.Id' --output text; check $? "create a test hosted zone"
ZONE_ID="$(<"${WORK}/zone")"
ZONE_ID="${ZONE_ID##*/}"
add_exit_function remove_hosted_zone
aws_route53_upsert_record "${ZONE_ID}" "multi.${NAME}.test" A 60 "192.0.2.10;192.0.2.11"; check $? "upsert a record with two values"
aws_route53_upsert_record "${ZONE_ID}" "txt.${NAME}.test" TXT 60 '"v=spf1 -all";"a \"quoted\" value"'; check $? "upsert a TXT record with quotes"
aws_route53_upsert_record "${ZONE_ID}" "weighted.${NAME}.test" A 60 "192.0.2.20" 10 blue; check $? "upsert a weighted record (blue)"
aws_route53_upsert_record "${ZONE_ID}" "weighted.${NAME}.test" A 60 "192.0.2.21" 20 green; check $? "upsert a weighted record (green)"
aws_route53_get_record RECORDS "${ZONE_ID}" "multi.${NAME}.test"; check $? "get the two-value record"
same "${RECORDS[0]:-}" "multi.${NAME}.test.|A|60|192.0.2.10;192.0.2.11||"; check $? "both values are returned [${RECORDS[0]:-}]"
aws_route53_get_record RECORDS "${ZONE_ID}" "txt.${NAME}.test"
same "${RECORDS[0]:-}" "txt.${NAME}.test.|TXT|60|\"v=spf1 -all\";\"a \\\"quoted\\\" value\"||"; check $? "the TXT values are exact [${RECORDS[0]:-}]"
aws_route53_get_record RECORDS "${ZONE_ID}" "weighted.${NAME}.test"
same "${#RECORDS[@]}" "2"; check $? "both weighted records are returned"
aws_route53_get_record RECORDS "${ZONE_ID}" "${NAME}.test"
same "${#RECORDS[@]}" "2"; check $? "the zone apex has only its NS and SOA (no prefix matches)"
aws_route53_delete_record "${ZONE_ID}" "weighted.${NAME}.test"; check $? "delete both weighted records"
aws_route53_delete_record "${ZONE_ID}" "multi.${NAME}.test" "" A; check $? "delete the two-value record by type"
aws_route53_delete_record "${ZONE_ID}" "txt.${NAME}.test"; check $? "delete the TXT record"
aws_route53_delete_record "${ZONE_ID}" "gone.${NAME}.test"; check $? "delete a record that does not exist (no change)"
aws_route53_get_record RECORDS "${ZONE_ID}" "multi.${NAME}.test"
same "${#RECORDS[@]}" "0"; check $? "the records are gone"

line_break
log_highlight "S3"
BUCKET="${NAME}"
aws_cli "" "Live Test (Create Bucket)" --region "${REGION}" s3 mb "s3://${BUCKET}"; check $? "create a test bucket"
add_exit_function remove_bucket
printf 'hello\n' > "${WORK}/hello.txt"
aws_s3_cp_upload "${BUCKET}" "in/hello.txt" "${WORK}/hello.txt" "" "" "${REGION}"; check $? "upload with the bucket's default encryption"
aws_s3_cp_upload "${BUCKET}" "in/ia.txt" "${WORK}/hello.txt" AES256 STANDARD_IA "${REGION}"; check $? "upload with AES256 and STANDARD_IA"
aws_cli "${WORK}/class" "Live Test (Storage Class)" --region "${REGION}" s3api head-object --bucket "${BUCKET}" --key in/ia.txt --query StorageClass --output text
same "$(<"${WORK}/class")" "STANDARD_IA"; check $? "the storage class was applied"
aws_s3_move_in_bucket "${BUCKET}" "in/hello.txt" "moved/hello.txt" "" "" "${REGION}"; check $? "move within the bucket"
aws_s3_cp_download "${BUCKET}" "moved/hello.txt" "${WORK}/hello.out" "${REGION}"; check $? "download"
same "$(<"${WORK}/hello.out")" "hello"; check $? "the download is exact"
mkdir -p "${WORK}/site/logs" "${WORK}/restore"
printf 'a\n' > "${WORK}/site/index.html"; printf 'b\n' > "${WORK}/site/tmp.tmp"; printf 'c\n' > "${WORK}/site/logs/keep me.txt"; printf 'd\n' > "${WORK}/site/logs/drop.txt"
aws_s3_sync_upload "${BUCKET}" site "${WORK}/site" no no "" "" '*.tmp;logs/*' 'logs/keep me.txt' "${REGION}"; check $? "sync up with excludes and an include"
aws_s3_sync_download "${BUCKET}" site "${WORK}/restore" no no "" "" "${REGION}"; check $? "sync down"
check_true "the filters were applied" test -f "${WORK}/restore/index.html" -a -f "${WORK}/restore/logs/keep me.txt" -a ! -e "${WORK}/restore/tmp.tmp" -a ! -e "${WORK}/restore/logs/drop.txt"
aws_s3_verify "${BUCKET}" "site/" yes "${REGION}"; check $? "verify a prefix"
aws_s3_delete "${BUCKET}" "moved/hello.txt" "${REGION}"; check $? "delete an object"

line_break
if (( FAILURES > 0 )); then
    log_error "${FAILURES} of ${CHECKS} checks failed"
    exit_logic "${E_AWS_FAILURE}"
fi
log_success "All ${CHECKS} checks passed"
exit_logic 0
