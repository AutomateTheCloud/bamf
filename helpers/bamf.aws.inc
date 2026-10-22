###------------------------------------------------------------------------------------------------
# script:  bamf.aws.inc
# purpose: BAMF helper file for AWS: credentials, EC2 instance metadata, CloudFront, CodeDeploy, EC2, ENI, KMS, Route 53, S3 and SSM Parameter Store
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( aws )
#          start_logic
#          aws_credentials_select "${AWS_PROFILE_TARGET:-}"   # optional, see the function
#
# requirements:
#          - AWS CLI version 2
#          - curl (EC2 instance metadata functions only), openssl and tar (KMS file functions only)
#
# conventions:
#          - the Region is the last argument of most functions; leave it empty to use the AWS CLI's
#            Region (AWS_REGION, AWS_DEFAULT_REGION or the profile's region)
#          - every AWS call goes through aws_cli, which retries failures that can succeed on a second
#            try (AWS_RETRY_COUNT, AWS_RETRY_TIMER_MAX_SEC) and logs the AWS CLI's error output
#          - secret values (SSM SecureString values, KMS plaintext) are never logged and never passed
#            on the command line, where other users could see them in the process list
#
# function list:
#          - aws_cli
#          - aws_cloudfront_invalidate_cache
#          - aws_codedeploy_create_bootstrap_deployment_group
#          - aws_codedeploy_create_deployment
#          - aws_codedeploy_delete_deployment_group
#          - aws_codedeploy_get_current_revision_json
#          - aws_codedeploy_get_deployed_revisions
#          - aws_codedeploy_get_deployment
#          - aws_codedeploy_get_deployment_revision_s3_bucket
#          - aws_codedeploy_get_deployment_revision_s3_key
#          - aws_codedeploy_get_service_role_arn
#          - aws_codedeploy_register_revision
#          - aws_codedeploy_wait_for_deployment
#          - aws_credentials_select
#          - aws_credentials_verify
#          - aws_ec2_get_availability_zone
#          - aws_ec2_get_ipaddress_private
#          - aws_ec2_source_destination_check_disable
#          - aws_ec2_tag_resource
#          - aws_ec2_tag_resource_from_file
#          - aws_ec2_vpc_route_update
#          - aws_eni_attach
#          - aws_eni_detach
#          - aws_eni_return_attachment_id
#          - aws_eni_return_status
#          - aws_kms_decrypt_file
#          - aws_kms_decrypt_string
#          - aws_kms_encrypt_file
#          - aws_kms_encrypt_string
#          - aws_kms_generate_envelope_key
#          - aws_metadata_account_id
#          - aws_metadata_account_id_from_cli
#          - aws_metadata_auto_scaling_group_name
#          - aws_metadata_get_tag
#          - aws_metadata_hostname
#          - aws_metadata_hostname_private
#          - aws_metadata_hostname_public
#          - aws_metadata_instance_id
#          - aws_metadata_ipaddress_private
#          - aws_metadata_ipaddress_public
#          - aws_metadata_mac
#          - aws_metadata_region
#          - aws_metadata_route_id_private
#          - aws_metadata_route_id_public
#          - aws_metadata_token
#          - aws_metadata_vpc_cidr
#          - aws_metadata_vpc_dns
#          - aws_metadata_vpc_id
#          - aws_route53_delete_record
#          - aws_route53_get_record
#          - aws_route53_upsert_record
#          - aws_s3_cp_download
#          - aws_s3_cp_upload
#          - aws_s3_delete
#          - aws_s3_move_in_bucket
#          - aws_s3_sync_download
#          - aws_s3_sync_upload
#          - aws_s3_verify
#          - aws_ssm_parameter_delete
#          - aws_ssm_parameter_exists
#          - aws_ssm_parameter_get
#          - aws_ssm_parameter_get_file
#          - aws_ssm_parameter_get_file_multi_part
#          - aws_ssm_parameter_get_file_silent
#          - aws_ssm_parameter_get_silent
#          - aws_ssm_parameter_path_delete
#          - aws_ssm_parameter_put
#          - aws_ssm_parameter_put_file
#          - aws_ssm_parameter_put_file_multi_part
#          - aws_ssm_parameter_tag
#          - aws_ssm_parameter_tag_path
#          - aws_ssm_parameters_to_key_value_file
#          - aws_ssm_parameters_to_key_value_file_multipart
#          - aws_ssm_parameters_to_properties_file
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Dependencies
REQUIRED_EXECUTABLES+=('aws')

###------------------------------------------------------------------------------------------------
## Exit Codes
define_exit_code E_AWS_FAILURE                          81 "There was a failure communicating with AWS"
define_exit_code E_CODEDEPLOY_CREATE_DEPLOYMENT_FAILURE 84 "There was a failure creating deployment with CodeDeploy"
define_exit_code E_CODEDEPLOY_DEPLOYMENT_FAILURE        85 "There was a failure deploying a revision with CodeDeploy"
define_exit_code E_KMS_FAILURE                         152 "There was a failure related to KMS"
define_exit_code E_AWS_RESOURCE_NOT_FOUND              154 "AWS Resource does not exist or could not be found"
define_exit_code E_AWS_RESOURCE_NOT_READY              155 "AWS Resource exists but is not in a ready state"

###------------------------------------------------------------------------------------------------
## Variables
# Set these before start_logic to change them for the whole script
declare -g AWS_RETRY_COUNT="${AWS_RETRY_COUNT:-3}"                  # attempts per AWS call
declare -g AWS_RETRY_TIMER_MAX_SEC="${AWS_RETRY_TIMER_MAX_SEC:-30}"  # random wait between attempts, up to this many seconds
declare -g AWS_ROUTE53_DEFAULT_TTL="${AWS_ROUTE53_DEFAULT_TTL:-600}"
declare -g AWS_CODEDEPLOY_WAIT_MAX_ATTEMPTS="${AWS_CODEDEPLOY_WAIT_MAX_ATTEMPTS:-120}"
declare -g AWS_CODEDEPLOY_WAIT_SLEEP="${AWS_CODEDEPLOY_WAIT_SLEEP:-15}"
declare -g AWS_SSM_MULTI_PART_SIZE=4096                              # characters per part (the Standard tier limit)
declare -g AWS_METADATA_URL="${AWS_METADATA_URL:-http://169.254.169.254}"
declare -g AWS_METADATA_TOKEN=""
declare -g AWS_METADATA_TOKEN_EXPIRES=0
# Error output of the last failed aws_cli call, for callers that need to tell errors apart
declare -g AWS_CLI_LAST_ERROR=""

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_cli()
## - Runs an AWS CLI command, retrying failures that can succeed on another try
## - Retries network errors and AWS errors that mean "try again later" (throttling, service
##   unavailable, internal errors). Errors that will not change, such as access denied, a missing
##   resource or an invalid argument, fail at once. The AWS CLI also retries throttling itself.
## - On failure, logs the AWS CLI's error output, stores it in AWS_CLI_LAST_ERROR, and returns E_AWS_FAILURE
## - Arguments
##   - $1: File to write the response (standard output) to (optional; empty discards it)
##   - $2: Description for the log (example: "SSM (Get Parameter)")
##   - $3+: AWS CLI arguments (example: ssm get-parameter --name /app/db)
function aws_cli() {
    local __AC_RESPONSE_FILE="${1:-}"
    local __AC_DESCRIPTION="${2:-AWS CLI}"
    local __AC_ERROR_FILE=""
    local __AC_ATTEMPT=0
    local __AC_RETURNVAL=0
    shift 2 || true
    if [[ $# -eq 0 ]]; then
        log_error "${__AC_DESCRIPTION}: no AWS CLI arguments given"
        return $E_BAD_ARGS
    fi
    [[ -n "${__AC_RESPONSE_FILE}" ]] || __AC_RESPONSE_FILE=/dev/null
    generate_temp_file __AC_ERROR_FILE "aws error output"
    AWS_CLI_LAST_ERROR=""
    while (( __AC_ATTEMPT < AWS_RETRY_COUNT )); do
        __AC_ATTEMPT=$(( __AC_ATTEMPT + 1 ))
        if (( __AC_ATTEMPT > 1 )); then
            call_sleep_random "${AWS_RETRY_TIMER_MAX_SEC}"
        fi
        __AC_RETURNVAL=0
        command aws "$@" >"${__AC_RESPONSE_FILE}" 2>"${__AC_ERROR_FILE}" || __AC_RETURNVAL=$?
        if (( __AC_RETURNVAL == 0 )); then
            return 0
        fi
        AWS_CLI_LAST_ERROR="$(<"${__AC_ERROR_FILE}")"
        log_add_from_file "${__AC_ERROR_FILE}" "${__AC_DESCRIPTION}: AWS CLI error output"
        log_warning "${__AC_DESCRIPTION}: attempt ${__AC_ATTEMPT} of ${AWS_RETRY_COUNT} failed (aws_cli_exit_code::${__AC_RETURNVAL})"
        if ! _aws_cli_error_is_retryable "${__AC_RETURNVAL}" "${AWS_CLI_LAST_ERROR}"; then
            break
        fi
    done
    log_error "${__AC_DESCRIPTION}: operation failed"
    return $E_AWS_FAILURE
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_credentials_select()
## - Chooses the AWS credentials for the rest of the script, and logs which ones are used
## - With a profile name: uses that profile, of any type (static keys, assumed role, IAM Identity
##   Center, credential_process). Credentials exported in the environment are cleared, because the
##   AWS CLI would otherwise use them instead of the profile.
## - Without one: leaves the AWS CLI's default credential chain unchanged (environment variables,
##   AWS_PROFILE, web identity, container and instance roles)
## - Returns E_BAD_ARGS if the profile does not exist
## - Arguments
##   - $1: Profile name (optional)
function aws_credentials_select() {
    local FUNCTION_DESCRIPTION="AWS (Select Credentials)"
    local TMP_PROFILE="${1:-}"
    if [[ -n "${TMP_PROFILE}" ]]; then
        if ! command aws configure list --profile "${TMP_PROFILE}" >/dev/null 2>&1; then
            log_error "${FUNCTION_DESCRIPTION}: profile not found in the AWS configuration [${TMP_PROFILE}]"
            return $E_BAD_ARGS
        fi
        unset AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY AWS_SESSION_TOKEN
        export AWS_PROFILE="${TMP_PROFILE}"
        log_highlight "Using AWS credentials from profile [${TMP_PROFILE}]"
    elif [[ -n "${AWS_ACCESS_KEY_ID:-}" ]]; then
        log_highlight "Using AWS credentials from the environment"
    elif [[ -n "${AWS_PROFILE:-}" ]]; then
        log_highlight "Using AWS credentials from profile [${AWS_PROFILE}] (AWS_PROFILE)"
    else
        log_highlight "Using AWS credentials from the AWS CLI default credential chain"
    fi
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_credentials_verify()
## - Checks that the AWS credentials work (sts get-caller-identity), and logs the account and identity
## - Arguments
##   - $1: Variable name to pass the account ID to (optional)
function aws_credentials_verify() {
    local FUNCTION_DESCRIPTION="AWS (Verify Credentials)"
    local __CV_RESPONSE=""
    local __CV_ACCOUNT=""
    local __CV_ARN=""
    generate_temp_file __CV_RESPONSE "aws response"
    aws_cli "${__CV_RESPONSE}" "${FUNCTION_DESCRIPTION}" sts get-caller-identity --query '[Account,Arn]' --output text || return $?
    read -r __CV_ACCOUNT __CV_ARN < "${__CV_RESPONSE}" || true
    log "- Account:  [${__CV_ACCOUNT}]"
    log "- Identity: [${__CV_ARN}]"
    if [[ -n "${1:-}" ]]; then
        _bamf_return "${1}" "${__CV_ACCOUNT}"
    fi
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_cloudfront_invalidate_cache()
## - Invalidates paths in a CloudFront distribution's cache
## - Arguments
##   - $1: CloudFront distribution ID
##   - $2: Path to invalidate, or several separated by ";" (example: /* or /index.html;/css/*)
##   - $3: Region (optional; CloudFront is global, so this is rarely needed)
function aws_cloudfront_invalidate_cache() {
    local FUNCTION_DESCRIPTION="AWS CloudFront (Invalidate Cache)"
    local TMP_DISTRIBUTION_ID="${1:-}"
    local TMP_INVALIDATION_ID=""
    local TMP_RESPONSE=""
    local TMP_PATH=""
    local -a TMP_PATHS=()
    _aws_require "${FUNCTION_DESCRIPTION}" "CloudFront distribution ID" "${TMP_DISTRIBUTION_ID}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Path to invalidate" "${2:-}" || return $?
    IFS=';' read -r -a TMP_PATHS <<< "${2}"
    for TMP_PATH in "${TMP_PATHS[@]}"; do
        [[ "${TMP_PATH}" == /* ]] || { log_error "${FUNCTION_DESCRIPTION}: paths start with / [${TMP_PATH}]"; return $E_BAD_ARGS; }
    done
    generate_temp_file TMP_RESPONSE "aws response"
    log_notice "${FUNCTION_DESCRIPTION}: started"
    log "- Distribution ID: [${TMP_DISTRIBUTION_ID}]"
    log "- Paths:           [${TMP_PATHS[*]}]"
    aws_cli "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} cloudfront create-invalidation --distribution-id "${TMP_DISTRIBUTION_ID}" \
        --paths "${TMP_PATHS[@]}" --query 'Invalidation.Id' --output text || return $?
    TMP_INVALIDATION_ID="$(<"${TMP_RESPONSE}")"
    log "${FUNCTION_DESCRIPTION}: invalidation requested [${TMP_INVALIDATION_ID}]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_account_id()
## - Returns the AWS account ID of this EC2 instance (instance metadata)
## - Returns E_AWS_FAILURE if instance metadata cannot be reached (not on EC2)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_account_id() {
    _aws_metadata_identity_value accountId "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_account_id_from_cli()
## - Returns the AWS account ID of the current credentials (sts get-caller-identity)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_account_id_from_cli() {
    local __MA_RESPONSE=""
    generate_temp_file __MA_RESPONSE "aws response"
    aws_cli "${__MA_RESPONSE}" "AWS STS (Account ID)" sts get-caller-identity --query Account --output text || return $?
    _bamf_return "${1:-}" "$(<"${__MA_RESPONSE}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_auto_scaling_group_name()
## - Returns the name of the Auto Scaling group this EC2 instance belongs to (empty if none)
## - Needs ec2:DescribeTags
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Region (optional, defaults to this instance's Region)
##   - $3: Instance ID (optional, defaults to this instance)
function aws_metadata_auto_scaling_group_name() {
    aws_metadata_get_tag "${1:-}" "aws:autoscaling:groupName" "${2:-}" "${3:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_get_tag()
## - Returns the value of a tag on an EC2 instance (empty if the tag is not set)
## - Needs ec2:DescribeTags
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Tag key
##   - $3: Region (optional, defaults to this instance's Region)
##   - $4: Instance ID (optional, defaults to this instance)
function aws_metadata_get_tag() {
    local __MT_KEY="${2:-}"
    local __MT_REGION="${3:-}"
    local __MT_INSTANCE_ID="${4:-}"
    local __MT_RESPONSE=""
    local __MT_VALUE=""
    _aws_require "AWS Metadata (Get Tag)" "Tag key" "${__MT_KEY}" || return $?
    [[ -n "${__MT_REGION}" ]] || aws_metadata_region __MT_REGION || return $?
    [[ -n "${__MT_INSTANCE_ID}" ]] || aws_metadata_instance_id __MT_INSTANCE_ID || return $?
    generate_temp_file __MT_RESPONSE "aws response"
    aws_cli "${__MT_RESPONSE}" "AWS Metadata (Get Tag)" --region "${__MT_REGION}" ec2 describe-tags \
        --filters "Name=resource-id,Values=${__MT_INSTANCE_ID}" "Name=key,Values=${__MT_KEY}" \
        --query 'Tags[0].Value' --output text || return $?
    __MT_VALUE="$(<"${__MT_RESPONSE}")"
    [[ "${__MT_VALUE}" == "None" ]] && __MT_VALUE=""
    _bamf_return "${1:-}" "${__MT_VALUE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_hostname()
## - Returns this EC2 instance's host name: the public one if it has one, otherwise the private one
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_hostname() {
    local __MH_HOSTNAME=""
    aws_metadata_hostname_public __MH_HOSTNAME || true
    if [[ -z "${__MH_HOSTNAME}" ]]; then
        aws_metadata_hostname_private __MH_HOSTNAME || return $?
    fi
    _bamf_return "${1:-}" "${__MH_HOSTNAME}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_hostname_private()
## - Returns this EC2 instance's private host name (instance metadata)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_hostname_private() {
    _aws_metadata_get meta-data/local-hostname "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_hostname_public()
## - Returns this EC2 instance's public host name (instance metadata); empty, with E_OBJECT_NOT_FOUND,
##   if it has none
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_hostname_public() {
    _aws_metadata_get meta-data/public-hostname "${1:-}" optional
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_instance_id()
## - Returns this EC2 instance's ID (instance metadata)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_instance_id() {
    _aws_metadata_get meta-data/instance-id "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_ipaddress_private()
## - Returns this EC2 instance's private IPv4 address (instance metadata)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_ipaddress_private() {
    _aws_metadata_get meta-data/local-ipv4 "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_ipaddress_public()
## - Returns this EC2 instance's public IPv4 address (instance metadata); empty, with
##   E_OBJECT_NOT_FOUND, if it has none
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_ipaddress_public() {
    _aws_metadata_get meta-data/public-ipv4 "${1:-}" optional
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_mac()
## - Returns the MAC address of this EC2 instance's primary network interface (instance metadata)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_mac() {
    _aws_metadata_get meta-data/mac "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_region()
## - Returns this EC2 instance's Region (instance metadata)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_region() {
    _aws_metadata_identity_value region "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_route_id_private()
## - Returns the IDs of the route tables in a VPC tagged Network=private (space separated)
## - Needs ec2:DescribeRouteTables. The Network tag is a naming convention: tag route tables
##   Network=private and Network=public to use this and aws_metadata_route_id_public
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Region (optional, defaults to this instance's Region)
##   - $3: VPC ID (optional, defaults to this instance's VPC)
function aws_metadata_route_id_private() {
    _aws_metadata_route_ids private "${1:-}" "${2:-}" "${3:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_route_id_public()
## - Returns the IDs of the route tables in a VPC tagged Network=public (space separated)
## - Needs ec2:DescribeRouteTables; see aws_metadata_route_id_private
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
##   - $2: Region (optional, defaults to this instance's Region)
##   - $3: VPC ID (optional, defaults to this instance's VPC)
function aws_metadata_route_id_public() {
    _aws_metadata_route_ids public "${1:-}" "${2:-}" "${3:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_token()
## - Gets an instance metadata (IMDSv2) session token into AWS_METADATA_TOKEN
## - The token is reused until it is five minutes from expiring (tokens last six hours)
## - Returns E_AWS_FAILURE if instance metadata cannot be reached (not on EC2)
function aws_metadata_token() {
    local TMP_NOW=0
    local TMP_TOKEN=""
    printf -v TMP_NOW '%(%s)T' -1
    if [[ -n "${AWS_METADATA_TOKEN}" ]] && (( AWS_METADATA_TOKEN_EXPIRES - TMP_NOW > 300 )); then
        return 0
    fi
    if ! command -v curl >/dev/null 2>&1; then
        log_error "AWS Metadata: curl is not installed"
        return $E_MISSING_DEPENDENCIES
    fi
    if ! TMP_TOKEN="$(command curl --silent --fail --retry 3 --connect-timeout 2 -X PUT "${AWS_METADATA_URL}/latest/api/token" -H "X-aws-ec2-metadata-token-ttl-seconds: 21600")" || [[ -z "${TMP_TOKEN}" ]]; then
        log_error "AWS Metadata: could not get an instance metadata token (is this an EC2 instance?)"
        return $E_AWS_FAILURE
    fi
    AWS_METADATA_TOKEN="${TMP_TOKEN}"
    AWS_METADATA_TOKEN_EXPIRES=$(( TMP_NOW + 21600 ))
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_vpc_cidr()
## - Returns the IPv4 CIDR block of this EC2 instance's VPC (instance metadata)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_vpc_cidr() {
    local __MV_MAC=""
    aws_metadata_mac __MV_MAC || return $?
    _aws_metadata_get "meta-data/network/interfaces/macs/${__MV_MAC}/vpc-ipv4-cidr-block" "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_vpc_dns()
## - Returns the IP address of this EC2 instance's VPC DNS server (the VPC's base address plus two)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_vpc_dns() {
    local __MD_CIDR=""
    local -a __MD_OCTETS=()
    local __MD_ADDRESS=0
    aws_metadata_vpc_cidr __MD_CIDR || return $?
    IFS='./' read -r -a __MD_OCTETS <<< "${__MD_CIDR}"
    if [[ ${#__MD_OCTETS[@]} -ne 5 ]]; then
        log_error "AWS Metadata (VPC DNS): unexpected VPC CIDR block [${__MD_CIDR}]"
        return $E_AWS_FAILURE
    fi
    # base address as a 32-bit number, plus two, back to dotted form
    __MD_ADDRESS=$(( (__MD_OCTETS[0] << 24) + (__MD_OCTETS[1] << 16) + (__MD_OCTETS[2] << 8) + __MD_OCTETS[3] + 2 ))
    _bamf_return "${1:-}" "$(( (__MD_ADDRESS >> 24) & 255 )).$(( (__MD_ADDRESS >> 16) & 255 )).$(( (__MD_ADDRESS >> 8) & 255 )).$(( __MD_ADDRESS & 255 ))"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_metadata_vpc_id()
## - Returns the ID of this EC2 instance's VPC (instance metadata)
## - Arguments
##   - $1: Variable name to pass info to (optional, if not specified the value is printed)
function aws_metadata_vpc_id() {
    local __MI_MAC=""
    aws_metadata_mac __MI_MAC || return $?
    _aws_metadata_get "meta-data/network/interfaces/macs/${__MI_MAC}/vpc-id" "${1:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_create_bootstrap_deployment_group()
## - Creates a deployment group that targets EC2 instances tagged BootstrapCodeDeployDeploymentGroup=<name>,
##   using the service role of an existing deployment group
## - A bootstrap group deploys to one new instance (tag it first) before it joins the real group
## - Arguments
##   - $1: CodeDeploy application name
##   - $2: Existing deployment group name (its service role is reused)
##   - $3: Name for the bootstrap deployment group
##   - $4: Region (optional)
function aws_codedeploy_create_bootstrap_deployment_group() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Create Bootstrap Deployment Group)"
    local TMP_APPLICATION="${1:-}"
    local TMP_DEPLOYMENT_GROUP="${2:-}"
    local TMP_BOOTSTRAP_GROUP="${3:-}"
    local TMP_REGION="${4:-}"
    local TMP_SERVICE_ROLE_ARN=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Application name" "${TMP_APPLICATION}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Deployment group" "${TMP_DEPLOYMENT_GROUP}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Bootstrap deployment group" "${TMP_BOOTSTRAP_GROUP}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: started"
    aws_codedeploy_get_service_role_arn TMP_SERVICE_ROLE_ARN "${TMP_APPLICATION}" "${TMP_DEPLOYMENT_GROUP}" "${TMP_REGION}" || return $?
    log "- Application:         [${TMP_APPLICATION}]"
    log "- Bootstrap Group:     [${TMP_BOOTSTRAP_GROUP}]"
    log "- Service Role:        [${TMP_SERVICE_ROLE_ARN}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${TMP_REGION:+--region "${TMP_REGION}"} deploy create-deployment-group \
        --application-name "${TMP_APPLICATION}" --deployment-group-name "${TMP_BOOTSTRAP_GROUP}" \
        --deployment-config-name "CodeDeployDefault.AllAtOnce" \
        --ec2-tag-filters "Key=BootstrapCodeDeployDeploymentGroup,Value=${TMP_BOOTSTRAP_GROUP},Type=KEY_AND_VALUE" \
        --service-role-arn "${TMP_SERVICE_ROLE_ARN}" || return $?
    log "${FUNCTION_DESCRIPTION}: created deployment group [${TMP_BOOTSTRAP_GROUP}]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_create_deployment()
## - Starts a CodeDeploy deployment of a revision stored in S3, and returns its deployment ID
## - The bundle type comes from the key's extension: .zip, .tgz or .tar.gz, .tar, .yaml or .yml, .json
## - Returns E_CODEDEPLOY_CREATE_DEPLOYMENT_FAILURE if the deployment cannot be created
## - Arguments
##   - $1: Variable name to pass the deployment ID to
##   - $2: CodeDeploy application name
##   - $3: Deployment group name
##   - $4: Deployment configuration (example: CodeDeployDefault.OneAtATime)
##   - $5: S3 bucket
##   - $6: S3 key of the revision
##   - $7: Description (optional)
##   - $8: Region (optional)
function aws_codedeploy_create_deployment() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Create Deployment)"
    local __CD_APPLICATION="${2:-}"
    local __CD_GROUP="${3:-}"
    local __CD_CONFIG="${4:-}"
    local __CD_BUCKET="${5:-}"
    local __CD_KEY="${6:-}"
    local __CD_DESCRIPTION="${7:-}"
    local __CD_REGION="${8:-}"
    local __CD_BUNDLE_TYPE=""
    local __CD_RESPONSE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Application name" "${__CD_APPLICATION}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Deployment group" "${__CD_GROUP}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Deployment configuration" "${__CD_CONFIG}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${__CD_BUCKET}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 key" "${__CD_KEY}" || return $?
    _aws_codedeploy_bundle_type __CD_BUNDLE_TYPE "${__CD_KEY}" || return $?
    generate_temp_file __CD_RESPONSE "aws response"
    log_notice "${FUNCTION_DESCRIPTION}: started"
    log "- Application:      [${__CD_APPLICATION}]"
    log "- Deployment Group: [${__CD_GROUP}]"
    log "- Configuration:    [${__CD_CONFIG}]"
    log "- Revision:         [s3://${__CD_BUCKET}/${__CD_KEY}] (${__CD_BUNDLE_TYPE})"
    if ! aws_cli "${__CD_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${__CD_REGION:+--region "${__CD_REGION}"} deploy create-deployment \
        --application-name "${__CD_APPLICATION}" --deployment-group-name "${__CD_GROUP}" \
        --deployment-config-name "${__CD_CONFIG}" --description "${__CD_DESCRIPTION}" \
        --s3-location "bucket=${__CD_BUCKET},key=${__CD_KEY},bundleType=${__CD_BUNDLE_TYPE}" \
        --query deploymentId --output text; then
        return $E_CODEDEPLOY_CREATE_DEPLOYMENT_FAILURE
    fi
    _bamf_return "${1:-}" "$(<"${__CD_RESPONSE}")"
    log "${FUNCTION_DESCRIPTION}: deployment started [$(<"${__CD_RESPONSE}")]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_delete_deployment_group()
## - Deletes a CodeDeploy deployment group
## - Arguments
##   - $1: CodeDeploy application name
##   - $2: Deployment group name
##   - $3: Region (optional)
function aws_codedeploy_delete_deployment_group() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Delete Deployment Group)"
    _aws_require "${FUNCTION_DESCRIPTION}" "Application name" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Deployment group" "${2:-}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: deleting [${1}/${2}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} deploy delete-deployment-group \
        --application-name "${1}" --deployment-group-name "${2}" || return $?
    log "${FUNCTION_DESCRIPTION}: deleted deployment group [${2}]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_get_current_revision_json()
## - Writes the target revision of a deployment group (JSON) to a file
## - Returns E_AWS_RESOURCE_NOT_FOUND if the group has never been deployed
## - Arguments
##   - $1: File to write the JSON to
##   - $2: CodeDeploy application name
##   - $3: Deployment group name
##   - $4: Region (optional)
function aws_codedeploy_get_current_revision_json() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Get Current Revision JSON)"
    local TMP_FILE="${1:-}"
    local TMP_RESPONSE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Output file" "${TMP_FILE}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Application name" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Deployment group" "${3:-}" || return $?
    generate_temp_file TMP_RESPONSE "aws response"
    aws_cli "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} deploy get-deployment-group \
        --application-name "${2}" --deployment-group-name "${3}" \
        --query 'deploymentGroupInfo.targetRevision' --output json || return $?
    if [[ "$(<"${TMP_RESPONSE}")" == "null" ]]; then
        log_warning "${FUNCTION_DESCRIPTION}: the deployment group has no revision [${2}/${3}]"
        return $E_AWS_RESOURCE_NOT_FOUND
    fi
    if ! command cat "${TMP_RESPONSE}" > "${TMP_FILE}"; then
        log_error "${FUNCTION_DESCRIPTION}: could not write to file [${TMP_FILE}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    log "${FUNCTION_DESCRIPTION}: wrote the current revision to [${TMP_FILE}]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_get_deployed_revisions()
## - Writes the file names (S3 keys without their path) of an application's deployed revisions to a
##   file, one per line
## - Arguments
##   - $1: CodeDeploy application name
##   - $2: File to write the list to
##   - $3: Region (optional)
function aws_codedeploy_get_deployed_revisions() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Get Deployed Revisions)"
    local TMP_FILE="${2:-}"
    local TMP_RESPONSE=""
    local TMP_LINE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Application name" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Output file" "${TMP_FILE}" || return $?
    generate_temp_file TMP_RESPONSE "aws response"
    aws_cli "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} deploy list-application-revisions \
        --application-name "${1}" --deployed include --query 'revisions[].[s3Location.key]' --output text || return $?
    if ! : > "${TMP_FILE}" 2>/dev/null; then
        log_error "${FUNCTION_DESCRIPTION}: could not write to file [${TMP_FILE}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    while IFS= read -r TMP_LINE || [[ -n "${TMP_LINE}" ]]; do
        [[ -n "${TMP_LINE}" && "${TMP_LINE}" != "None" ]] && printf '%s\n' "${TMP_LINE##*/}" >> "${TMP_FILE}"
    done < "${TMP_RESPONSE}"
    log_add_from_file "${TMP_FILE}" "${FUNCTION_DESCRIPTION}: deployed revisions"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_get_deployment()
## - Returns the status of a CodeDeploy deployment (Created, Queued, InProgress, Baking, Succeeded,
##   Failed, Stopped or Ready), and logs its instance counts and any error
## - Arguments
##   - $1: Variable name to pass the status to
##   - $2: Deployment ID
##   - $3: Region (optional)
function aws_codedeploy_get_deployment() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Get Deployment)"
    local __GD_RESPONSE=""
    local __GD_STATUS="" __GD_CODE="" __GD_MESSAGE=""
    local __GD_PENDING="" __GD_IN_PROGRESS="" __GD_SUCCEEDED="" __GD_FAILED="" __GD_SKIPPED=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Deployment ID" "${2:-}" || return $?
    generate_temp_file __GD_RESPONSE "aws response"
    aws_cli "${__GD_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} deploy get-deployment --deployment-id "${2}" \
        --query 'deploymentInfo.[status,deploymentOverview.Pending,deploymentOverview.InProgress,deploymentOverview.Succeeded,deploymentOverview.Failed,deploymentOverview.Skipped,errorInformation.code,errorInformation.message]' \
        --output text || return $?
    IFS=$'\t' read -r __GD_STATUS __GD_PENDING __GD_IN_PROGRESS __GD_SUCCEEDED __GD_FAILED __GD_SKIPPED __GD_CODE __GD_MESSAGE < "${__GD_RESPONSE}" || true
    log "- Deployment [${2}]: ${__GD_STATUS} (Pending::${__GD_PENDING} / InProgress::${__GD_IN_PROGRESS} / Succeeded::${__GD_SUCCEEDED} / Failed::${__GD_FAILED} / Skipped::${__GD_SKIPPED})"
    if [[ -n "${__GD_CODE}" && "${__GD_CODE}" != "None" ]]; then
        log_warning "- Error: [${__GD_CODE} - ${__GD_MESSAGE}]"
    fi
    _bamf_return "${1:-}" "${__GD_STATUS}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_get_deployment_revision_s3_bucket()
## - Returns the S3 bucket of a deployment group's target revision
## - Arguments
##   - $1: Variable name to pass info to
##   - $2: CodeDeploy application name
##   - $3: Deployment group name
##   - $4: Region (optional)
function aws_codedeploy_get_deployment_revision_s3_bucket() {
    _aws_codedeploy_group_value "${1:-}" "${2:-}" "${3:-}" "${4:-}" 'deploymentGroupInfo.targetRevision.s3Location.bucket' "Revision S3 Bucket"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_get_deployment_revision_s3_key()
## - Returns the S3 key of a deployment group's target revision
## - Arguments
##   - $1: Variable name to pass info to
##   - $2: CodeDeploy application name
##   - $3: Deployment group name
##   - $4: Region (optional)
function aws_codedeploy_get_deployment_revision_s3_key() {
    _aws_codedeploy_group_value "${1:-}" "${2:-}" "${3:-}" "${4:-}" 'deploymentGroupInfo.targetRevision.s3Location.key' "Revision S3 Key"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_get_service_role_arn()
## - Returns the service role ARN of a deployment group
## - Arguments
##   - $1: Variable name to pass info to
##   - $2: CodeDeploy application name
##   - $3: Deployment group name
##   - $4: Region (optional)
function aws_codedeploy_get_service_role_arn() {
    _aws_codedeploy_group_value "${1:-}" "${2:-}" "${3:-}" "${4:-}" 'deploymentGroupInfo.serviceRoleArn' "Service Role ARN"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_register_revision()
## - Registers a revision stored in S3 with a CodeDeploy application
## - The bundle type comes from the key's extension, as in aws_codedeploy_create_deployment
## - Arguments
##   - $1: CodeDeploy application name
##   - $2: S3 bucket
##   - $3: S3 key of the revision
##   - $4: Region (optional)
function aws_codedeploy_register_revision() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Register Revision)"
    local TMP_BUNDLE_TYPE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Application name" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 key" "${3:-}" || return $?
    _aws_codedeploy_bundle_type TMP_BUNDLE_TYPE "${3}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: registering [s3://${2}/${3}] (${TMP_BUNDLE_TYPE}) with [${1}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} deploy register-application-revision \
        --application-name "${1}" --s3-location "bucket=${2},key=${3},bundleType=${TMP_BUNDLE_TYPE}" || return $?
    log "${FUNCTION_DESCRIPTION}: registered revision"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_codedeploy_wait_for_deployment()
## - Checks a CodeDeploy deployment until it finishes, logging its progress each time
## - Returns 0 when it succeeds, E_CODEDEPLOY_DEPLOYMENT_FAILURE when it fails or is stopped, and
##   E_OPERATION_TIMEOUT if it is still running after the last check
## - Arguments
##   - $1: Deployment ID
##   - $2: Region (optional)
##   - $3: Number of checks (optional, defaults to AWS_CODEDEPLOY_WAIT_MAX_ATTEMPTS, 120)
##   - $4: Seconds between checks (optional, defaults to AWS_CODEDEPLOY_WAIT_SLEEP, 15)
function aws_codedeploy_wait_for_deployment() {
    local FUNCTION_DESCRIPTION="AWS CodeDeploy (Wait for Deployment)"
    local TMP_DEPLOYMENT_ID="${1:-}"
    local TMP_REGION="${2:-}"
    local TMP_MAX_ATTEMPTS="${3:-${AWS_CODEDEPLOY_WAIT_MAX_ATTEMPTS}}"
    local TMP_SLEEP="${4:-${AWS_CODEDEPLOY_WAIT_SLEEP}}"
    local TMP_ATTEMPT=0
    local TMP_STATUS=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Deployment ID" "${TMP_DEPLOYMENT_ID}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: tracking deployment [${TMP_DEPLOYMENT_ID}]"
    while (( TMP_ATTEMPT < TMP_MAX_ATTEMPTS )); do
        TMP_ATTEMPT=$(( TMP_ATTEMPT + 1 ))
        (( TMP_ATTEMPT > 1 )) && call_sleep "${TMP_SLEEP}"
        log "${FUNCTION_DESCRIPTION}: check ${TMP_ATTEMPT} of ${TMP_MAX_ATTEMPTS}"
        aws_codedeploy_get_deployment TMP_STATUS "${TMP_DEPLOYMENT_ID}" "${TMP_REGION}" || return $?
        case "${TMP_STATUS}" in
            Succeeded)
                log_success "${FUNCTION_DESCRIPTION}: deployment succeeded [${TMP_DEPLOYMENT_ID}]"
                return 0;;
            Failed|Stopped)
                log_error "${FUNCTION_DESCRIPTION}: deployment ${TMP_STATUS,,} [${TMP_DEPLOYMENT_ID}]"
                return $E_CODEDEPLOY_DEPLOYMENT_FAILURE;;
        esac
    done
    log_error "${FUNCTION_DESCRIPTION}: deployment still ${TMP_STATUS} after ${TMP_MAX_ATTEMPTS} checks [${TMP_DEPLOYMENT_ID}]"
    return $E_OPERATION_TIMEOUT
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ec2_get_availability_zone()
## - Returns the Availability Zone of an EC2 instance
## - Arguments
##   - $1: Variable name to pass info to
##   - $2: Instance ID
##   - $3: Region (optional)
function aws_ec2_get_availability_zone() {
    _aws_ec2_instance_value "${1:-}" "${2:-}" "${3:-}" 'Reservations[0].Instances[0].Placement.AvailabilityZone' "Availability Zone"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ec2_get_ipaddress_private()
## - Returns the private IPv4 address of an EC2 instance
## - Arguments
##   - $1: Variable name to pass info to
##   - $2: Instance ID
##   - $3: Region (optional)
function aws_ec2_get_ipaddress_private() {
    _aws_ec2_instance_value "${1:-}" "${2:-}" "${3:-}" 'Reservations[0].Instances[0].PrivateIpAddress' "Private IP Address"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ec2_source_destination_check_disable()
## - Turns off the source/destination check of an EC2 instance, so it can route traffic for other
##   addresses (a NAT instance, VPN gateway or router)
## - Arguments
##   - $1: Instance ID
##   - $2: Region (optional)
function aws_ec2_source_destination_check_disable() {
    local FUNCTION_DESCRIPTION="AWS EC2 (Disable Source/Destination Check)"
    _aws_require "${FUNCTION_DESCRIPTION}" "Instance ID" "${1:-}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: [${1}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${2:+--region "${2}"} ec2 modify-instance-attribute --instance-id "${1}" --no-source-dest-check || return $?
    log "${FUNCTION_DESCRIPTION}: source/destination check disabled"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ec2_tag_resource()
## - Adds or replaces a tag on an EC2 resource (instance, volume, network interface, ...)
## - Arguments
##   - $1: Resource ID
##   - $2: Tag key
##   - $3: Tag value
##   - $4: Region (optional)
function aws_ec2_tag_resource() {
    local FUNCTION_DESCRIPTION="AWS EC2 (Tag Resource)"
    local TMP_FILE_TAGS=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Resource ID" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Tag key" "${2:-}" || return $?
    generate_temp_file TMP_FILE_TAGS "tags json"
    printf '[{"Key": "%s", "Value": "%s"}]\n' "$(json_escape "${2}")" "$(json_escape "${3:-}")" > "${TMP_FILE_TAGS}"
    log_notice "${FUNCTION_DESCRIPTION}: [${1}] ${2}=${3:-}"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} ec2 create-tags --resources "${1}" --tags "file://${TMP_FILE_TAGS}" || return $?
    log "${FUNCTION_DESCRIPTION}: tagged resource"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ec2_tag_resource_from_file()
## - Adds or replaces tags on an EC2 resource from a JSON file: [{"Key": "Name", "Value": "web"}, ...]
## - Arguments
##   - $1: Resource ID
##   - $2: Tags file (JSON)
##   - $3: Region (optional)
function aws_ec2_tag_resource_from_file() {
    local FUNCTION_DESCRIPTION="AWS EC2 (Tag Resource from File)"
    _aws_require "${FUNCTION_DESCRIPTION}" "Resource ID" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Tags file" "${2:-}" || return $?
    if [[ ! -f "${2}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: tags file not found [${2}]"
        return $E_OBJECT_NOT_FOUND
    fi
    log_notice "${FUNCTION_DESCRIPTION}: [${1}] from [${2}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} ec2 create-tags --resources "${1}" --tags "file://${2}" || return $?
    log "${FUNCTION_DESCRIPTION}: tagged resource"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ec2_vpc_route_update()
## - Points a route table's default route (0.0.0.0/0) at an EC2 instance, replacing the route if it
##   exists and creating it if not (a NAT instance taking over a subnet's internet traffic)
## - Arguments
##   - $1: Route table ID
##   - $2: Instance ID
##   - $3: Region (optional)
function aws_ec2_vpc_route_update() {
    local FUNCTION_DESCRIPTION="AWS EC2 (Update Default Route)"
    _aws_require "${FUNCTION_DESCRIPTION}" "Route table ID" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Instance ID" "${2:-}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: [${1}] 0.0.0.0/0 -> [${2}]"
    # replace-route fails when there is no default route yet; that case is expected, so it is not logged
    if command aws ${3:+--region "${3}"} ec2 replace-route --route-table-id "${1}" --instance-id "${2}" --destination-cidr-block 0.0.0.0/0 >/dev/null 2>&1; then
        log "${FUNCTION_DESCRIPTION}: replaced the default route"
        return 0
    fi
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} ec2 create-route --route-table-id "${1}" --instance-id "${2}" --destination-cidr-block 0.0.0.0/0 || return $?
    log "${FUNCTION_DESCRIPTION}: created the default route"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_eni_attach()
## - Attaches an elastic network interface (ENI) to an EC2 instance, detaching it from any other
##   instance first (a floating ENI that moves to whichever instance is active)
## - Arguments
##   - $1: Network interface ID
##   - $2: Instance ID
##   - $3: Device index (optional, defaults to 3)
##   - $4: Region (optional)
function aws_eni_attach() {
    local FUNCTION_DESCRIPTION="AWS ENI (Attach)"
    local TMP_ENI_ID="${1:-}"
    local TMP_INSTANCE_ID="${2:-}"
    local TMP_DEVICE_INDEX="${3:-3}"
    local TMP_REGION="${4:-}"
    local TMP_ATTACHMENT_ID=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Network interface ID" "${TMP_ENI_ID}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Instance ID" "${TMP_INSTANCE_ID}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_ENI_ID}] -> [${TMP_INSTANCE_ID}] (device index ${TMP_DEVICE_INDEX})"
    aws_eni_return_attachment_id TMP_ATTACHMENT_ID "${TMP_ENI_ID}" "${TMP_REGION}" || return $?
    if [[ -n "${TMP_ATTACHMENT_ID}" ]]; then
        aws_eni_detach "${TMP_ATTACHMENT_ID}" "${TMP_REGION}" || return $?
        log "${FUNCTION_DESCRIPTION}: waiting for the network interface to become available"
        aws_cli "" "${FUNCTION_DESCRIPTION}" ${TMP_REGION:+--region "${TMP_REGION}"} ec2 wait network-interface-available --network-interface-ids "${TMP_ENI_ID}" || return $?
    fi
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${TMP_REGION:+--region "${TMP_REGION}"} ec2 attach-network-interface \
        --network-interface-id "${TMP_ENI_ID}" --instance-id "${TMP_INSTANCE_ID}" --device-index "${TMP_DEVICE_INDEX}" || return $?
    log "${FUNCTION_DESCRIPTION}: attached network interface"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_eni_detach()
## - Detaches an elastic network interface (forced)
## - Arguments
##   - $1: Attachment ID
##   - $2: Region (optional)
function aws_eni_detach() {
    local FUNCTION_DESCRIPTION="AWS ENI (Detach)"
    _aws_require "${FUNCTION_DESCRIPTION}" "Attachment ID" "${1:-}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: [${1}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${2:+--region "${2}"} ec2 detach-network-interface --attachment-id "${1}" --force || return $?
    log "${FUNCTION_DESCRIPTION}: detached network interface"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_eni_return_attachment_id()
## - Returns the attachment ID of an elastic network interface (empty when it is not attached)
## - Arguments
##   - $1: Variable name to pass info to
##   - $2: Network interface ID
##   - $3: Region (optional)
function aws_eni_return_attachment_id() {
    _aws_eni_value "${1:-}" "${2:-}" "${3:-}" 'NetworkInterfaces[0].Attachment.AttachmentId' "Attachment ID"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_eni_return_status()
## - Returns the status of an elastic network interface (available, in-use, attaching, detaching, ...)
## - Arguments
##   - $1: Variable name to pass info to
##   - $2: Network interface ID
##   - $3: Region (optional)
function aws_eni_return_status() {
    _aws_eni_value "${1:-}" "${2:-}" "${3:-}" 'NetworkInterfaces[0].Status' "Status"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_kms_decrypt_file()
## - Decrypts a file made by aws_kms_encrypt_file into a directory, under its original file name
## - Also reads files from earlier BAMF versions (format 1)
## - Arguments
##   - $1: Encrypted file
##   - $2: Output directory
##   - $3: Region (optional)
function aws_kms_decrypt_file() {
    local FUNCTION_DESCRIPTION="AWS KMS (Decrypt File)"
    local TMP_FILE_ENCRYPTED="${1:-}"
    local TMP_DIRECTORY_OUTPUT="${2:-}"
    local TMP_REGION="${3:-}"
    local TMP_DIRECTORY_WORKING=""
    local TMP_FILE_LOG=""
    local TMP_FILE_ENVELOPE_KEY=""
    local TMP_FILE_PASSPHRASE=""
    local TMP_FORMAT=""
    local TMP_FILENAME=""
    local TMP_ENVELOPE_KEY=""
    local TMP_DIGEST=""
    local TMP_DECRYPTED=no
    _aws_require "${FUNCTION_DESCRIPTION}" "Encrypted file" "${TMP_FILE_ENCRYPTED}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Output directory" "${TMP_DIRECTORY_OUTPUT}" || return $?
    TMP_DIRECTORY_OUTPUT="${TMP_DIRECTORY_OUTPUT%/}"
    if [[ ! -f "${TMP_FILE_ENCRYPTED}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: encrypted file not found [${TMP_FILE_ENCRYPTED}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if [[ ! -d "${TMP_DIRECTORY_OUTPUT}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: output directory not found [${TMP_DIRECTORY_OUTPUT}]"
        return $E_OBJECT_NOT_FOUND
    fi
    _aws_kms_require_tools "${FUNCTION_DESCRIPTION}" || return $?
    generate_temp_directory TMP_DIRECTORY_WORKING "kms bundle"
    generate_temp_file TMP_FILE_LOG "operation log"
    generate_temp_file TMP_FILE_ENVELOPE_KEY "encrypted data key"
    log_notice "${FUNCTION_DESCRIPTION}: started"
    log "- Encrypted File:   [${TMP_FILE_ENCRYPTED}]"
    log "- Output Directory: [${TMP_DIRECTORY_OUTPUT}]"

    if ! command tar -xzf "${TMP_FILE_ENCRYPTED}" -C "${TMP_DIRECTORY_WORKING}" >"${TMP_FILE_LOG}" 2>&1; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: tar output"
        log_error "${FUNCTION_DESCRIPTION}: could not unpack the encrypted file"
        return $E_FILE_CONVERSION_FAILURE
    fi
    if [[ ! -f "${TMP_DIRECTORY_WORKING}/metadata" || ! -f "${TMP_DIRECTORY_WORKING}/payload" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: not a BAMF encrypted file (metadata or payload missing)"
        return $E_FILE_CONVERSION_FAILURE
    fi
    load_key_value_from_file TMP_FORMAT format "${TMP_DIRECTORY_WORKING}/metadata"
    load_key_value_from_file TMP_FILENAME filename "${TMP_DIRECTORY_WORKING}/metadata"
    load_key_value_from_file TMP_ENVELOPE_KEY envelope_key "${TMP_DIRECTORY_WORKING}/metadata"
    # only the file name is used, so a crafted file cannot write outside the output directory
    TMP_FILENAME="${TMP_FILENAME##*/}"
    if [[ -z "${TMP_FILENAME}" || "${TMP_FILENAME}" == "." || "${TMP_FILENAME}" == ".." || -z "${TMP_ENVELOPE_KEY}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: the metadata does not name the file or hold the data key"
        return $E_FILE_CONVERSION_FAILURE
    fi
    log "- Decrypted File:   [${TMP_DIRECTORY_OUTPUT}/${TMP_FILENAME}] (format ${TMP_FORMAT:-1})"

    if ! printf '%s' "${TMP_ENVELOPE_KEY}" | command base64 -d > "${TMP_FILE_ENVELOPE_KEY}" 2>/dev/null; then
        log_error "${FUNCTION_DESCRIPTION}: the data key in the metadata is not valid base64"
        return $E_BASE64_CONVERSION_FAILURE
    fi
    # the plaintext data key (base64) is the passphrase; it goes to openssl through a 0600 file
    generate_temp_file TMP_FILE_PASSPHRASE "data key"
    aws_cli "${TMP_FILE_PASSPHRASE}" "${FUNCTION_DESCRIPTION}" ${TMP_REGION:+--region "${TMP_REGION}"} kms decrypt \
        --ciphertext-blob "fileb://${TMP_FILE_ENVELOPE_KEY}" --query Plaintext --output text || return $E_KMS_FAILURE

    case "${TMP_FORMAT:-1}" in
        2)
            if command openssl enc -d -aes-256-cbc -pbkdf2 -iter 100000 -md sha256 -pass "file:${TMP_FILE_PASSPHRASE}" \
                -in "${TMP_DIRECTORY_WORKING}/payload" -out "${TMP_DIRECTORY_OUTPUT}/${TMP_FILENAME}" >"${TMP_FILE_LOG}" 2>&1; then
                TMP_DECRYPTED=yes
            fi;;
        1)
            # format 1 used openssl's default digest, which changed from md5 to sha256 in OpenSSL 1.1.0
            for TMP_DIGEST in sha256 md5; do
                if command openssl enc -d -aes-256-cbc -md "${TMP_DIGEST}" -pass "file:${TMP_FILE_PASSPHRASE}" \
                    -in "${TMP_DIRECTORY_WORKING}/payload" -out "${TMP_DIRECTORY_OUTPUT}/${TMP_FILENAME}" >"${TMP_FILE_LOG}" 2>&1; then
                    TMP_DECRYPTED=yes
                    break
                fi
            done;;
        *)
            log_error "${FUNCTION_DESCRIPTION}: unknown format [${TMP_FORMAT}]"
            return $E_FILE_CONVERSION_FAILURE;;
    esac
    if [[ "${TMP_DECRYPTED}" != "yes" ]]; then
        command rm -f -- "${TMP_DIRECTORY_OUTPUT:?}/${TMP_FILENAME:?}"
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: openssl output"
        log_error "${FUNCTION_DESCRIPTION}: could not decrypt the payload"
        return $E_OPENSSL_FAILURE
    fi
    log "${FUNCTION_DESCRIPTION}: decrypted file [${TMP_DIRECTORY_OUTPUT}/${TMP_FILENAME}]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_kms_decrypt_string()
## - Decrypts a string encrypted with KMS (base64 ciphertext, as aws_kms_encrypt_string returns)
## - The decrypted value is never logged
## - Arguments
##   - $1: Variable name to pass the decrypted string to
##   - $2: Encrypted string (base64)
##   - $3: Not used (earlier versions masked the value in the log on request; it is now never logged)
##   - $4: Region (optional)
function aws_kms_decrypt_string() {
    local FUNCTION_DESCRIPTION="AWS KMS (Decrypt String)"
    local __KD_FILE_CIPHERTEXT=""
    local __KD_RESPONSE=""
    local __KD_PLAINTEXT=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Result variable" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Encrypted string" "${2:-}" || return $?
    generate_temp_file __KD_FILE_CIPHERTEXT "ciphertext"
    generate_temp_file __KD_RESPONSE "aws response"
    if ! printf '%s' "${2}" | command base64 -d > "${__KD_FILE_CIPHERTEXT}" 2>/dev/null; then
        log_error "${FUNCTION_DESCRIPTION}: the encrypted string is not valid base64"
        return $E_BASE64_CONVERSION_FAILURE
    fi
    log "${FUNCTION_DESCRIPTION}: decrypting"
    aws_cli "${__KD_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} kms decrypt \
        --ciphertext-blob "fileb://${__KD_FILE_CIPHERTEXT}" --query Plaintext --output text || return $E_KMS_FAILURE
    if ! __KD_PLAINTEXT="$(command base64 -d < "${__KD_RESPONSE}")"; then
        log_error "${FUNCTION_DESCRIPTION}: KMS returned plaintext that is not valid base64"
        return $E_BASE64_CONVERSION_FAILURE
    fi
    _bamf_return "${1}" "${__KD_PLAINTEXT}"
    log "${FUNCTION_DESCRIPTION}: decrypted [${LOG_MASK}]"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_kms_encrypt_file()
## - Encrypts a file with a KMS key (envelope encryption) into <output directory>/<file name>.enc
## - KMS creates a one-time data key; the file is encrypted with it locally (openssl, AES-256-CBC,
##   PBKDF2), and only the KMS-encrypted copy of the data key is stored with it. KMS is called once,
##   however large the file. Decrypt with aws_kms_decrypt_file.
## - The data key reaches openssl through a 0600 file, never the command line
## - Requires OpenSSL 1.1.1 or newer (for PBKDF2)
## - Arguments
##   - $1: File to encrypt
##   - $2: Output directory
##   - $3: KMS key (ID, ARN or alias, example: alias/backups)
##   - $4: Region (optional)
function aws_kms_encrypt_file() {
    local FUNCTION_DESCRIPTION="AWS KMS (Encrypt File)"
    local TMP_FILE="${1:-}"
    local TMP_DIRECTORY_OUTPUT="${2:-}"
    local TMP_KMS_KEY="${3:-}"
    local TMP_REGION="${4:-}"
    local TMP_DIRECTORY_WORKING=""
    local TMP_FILE_LOG=""
    local TMP_FILE_DATA_KEY=""
    local TMP_FILE_PASSPHRASE=""
    local TMP_FILENAME=""
    local TMP_DATA_KEY_PLAINTEXT=""
    local TMP_ENVELOPE_KEY=""
    _aws_require "${FUNCTION_DESCRIPTION}" "File to encrypt" "${TMP_FILE}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Output directory" "${TMP_DIRECTORY_OUTPUT}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "KMS key" "${TMP_KMS_KEY}" || return $?
    TMP_DIRECTORY_OUTPUT="${TMP_DIRECTORY_OUTPUT%/}"
    if [[ ! -f "${TMP_FILE}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: file not found [${TMP_FILE}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if [[ ! -d "${TMP_DIRECTORY_OUTPUT}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: output directory not found [${TMP_DIRECTORY_OUTPUT}]"
        return $E_OBJECT_NOT_FOUND
    fi
    _aws_kms_require_tools "${FUNCTION_DESCRIPTION}" || return $?
    if ! command openssl enc -help 2>&1 | command grep -q -- '-pbkdf2'; then
        log_error "${FUNCTION_DESCRIPTION}: OpenSSL 1.1.1 or newer is required (this one has no -pbkdf2)"
        return $E_MISSING_DEPENDENCIES
    fi
    TMP_FILENAME="${TMP_FILE##*/}"
    generate_temp_directory TMP_DIRECTORY_WORKING "kms bundle"
    generate_temp_file TMP_FILE_LOG "operation log"
    generate_temp_file TMP_FILE_DATA_KEY "data key response"
    generate_temp_file TMP_FILE_PASSPHRASE "data key"
    log_notice "${FUNCTION_DESCRIPTION}: started"
    log "- File:             [${TMP_FILE}]"
    log "- KMS Key:          [${TMP_KMS_KEY}]"
    log "- Encrypted File:   [${TMP_DIRECTORY_OUTPUT}/${TMP_FILENAME}.enc]"

    # one call returns the data key twice: in plaintext (used here, then discarded) and encrypted by KMS (stored)
    aws_cli "${TMP_FILE_DATA_KEY}" "${FUNCTION_DESCRIPTION}" ${TMP_REGION:+--region "${TMP_REGION}"} kms generate-data-key \
        --key-id "${TMP_KMS_KEY}" --key-spec AES_256 --query '[Plaintext,CiphertextBlob]' --output text || return $E_KMS_FAILURE
    IFS=$'\t' read -r TMP_DATA_KEY_PLAINTEXT TMP_ENVELOPE_KEY < "${TMP_FILE_DATA_KEY}" || true
    printf '%s' "${TMP_DATA_KEY_PLAINTEXT}" > "${TMP_FILE_PASSPHRASE}"
    TMP_DATA_KEY_PLAINTEXT=""
    : > "${TMP_FILE_DATA_KEY}"
    if [[ -z "${TMP_ENVELOPE_KEY}" || ! -s "${TMP_FILE_PASSPHRASE}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: KMS did not return a data key"
        return $E_KMS_FAILURE
    fi
    if ! command openssl enc -e -aes-256-cbc -pbkdf2 -iter 100000 -md sha256 -salt -pass "file:${TMP_FILE_PASSPHRASE}" \
        -in "${TMP_FILE}" -out "${TMP_DIRECTORY_WORKING}/payload" >"${TMP_FILE_LOG}" 2>&1; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: openssl output"
        log_error "${FUNCTION_DESCRIPTION}: could not encrypt the file"
        return $E_OPENSSL_FAILURE
    fi
    write_key_value_to_file "${TMP_DIRECTORY_WORKING}/metadata" format 2 || return $?
    write_key_value_to_file "${TMP_DIRECTORY_WORKING}/metadata" filename "${TMP_FILENAME}" || return $?
    write_key_value_to_file "${TMP_DIRECTORY_WORKING}/metadata" envelope_key "${TMP_ENVELOPE_KEY}" || return $?
    write_key_value_to_file "${TMP_DIRECTORY_WORKING}/metadata" kms_key "${TMP_KMS_KEY}" || return $?
    write_key_value_to_file "${TMP_DIRECTORY_WORKING}/metadata" cipher "aes-256-cbc, pbkdf2 sha256 100000 iterations" || return $?
    if ! command tar -czf "${TMP_DIRECTORY_OUTPUT}/${TMP_FILENAME}.enc" -C "${TMP_DIRECTORY_WORKING}" ./metadata ./payload >"${TMP_FILE_LOG}" 2>&1; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: tar output"
        command rm -f -- "${TMP_DIRECTORY_OUTPUT:?}/${TMP_FILENAME:?}.enc"
        log_error "${FUNCTION_DESCRIPTION}: could not write the encrypted file"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    log "${FUNCTION_DESCRIPTION}: encrypted file [${TMP_DIRECTORY_OUTPUT}/${TMP_FILENAME}.enc]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_kms_encrypt_string()
## - Encrypts a string with a KMS key (up to 4 KB), and returns the ciphertext as base64
## - The plaintext is passed to the AWS CLI through a 0600 file, never the command line, and never logged
## - Arguments
##   - $1: Variable name to pass the encrypted string to
##   - $2: String to encrypt
##   - $3: KMS key (ID, ARN or alias)
##   - $4: Region (optional)
function aws_kms_encrypt_string() {
    local FUNCTION_DESCRIPTION="AWS KMS (Encrypt String)"
    local __KE_FILE_PLAINTEXT=""
    local __KE_RESPONSE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Result variable" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "String to encrypt" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "KMS key" "${3:-}" || return $?
    generate_temp_file_with_content __KE_FILE_PLAINTEXT "${2}" "plaintext"
    generate_temp_file __KE_RESPONSE "aws response"
    log "${FUNCTION_DESCRIPTION}: encrypting [${LOG_MASK}] with [${3}]"
    aws_cli "${__KE_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} kms encrypt --key-id "${3}" \
        --plaintext "fileb://${__KE_FILE_PLAINTEXT}" --query CiphertextBlob --output text || return $E_KMS_FAILURE
    _bamf_return "${1}" "$(<"${__KE_RESPONSE}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_kms_generate_envelope_key()
## - Creates a 256-bit data key with a KMS key, and returns only its encrypted form (base64); decrypt it
##   with KMS when it is needed
## - Arguments
##   - $1: Variable name to pass the encrypted data key to
##   - $2: KMS key (ID, ARN or alias)
##   - $3: Region (optional)
function aws_kms_generate_envelope_key() {
    local FUNCTION_DESCRIPTION="AWS KMS (Generate Data Key)"
    local __KG_RESPONSE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "KMS key" "${2:-}" || return $?
    generate_temp_file __KG_RESPONSE "aws response"
    aws_cli "${__KG_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} kms generate-data-key-without-plaintext \
        --key-id "${2}" --key-spec AES_256 --query CiphertextBlob --output text || return $E_KMS_FAILURE
    _bamf_return "${1:-}" "$(<"${__KG_RESPONSE}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_route53_delete_record()
## - Deletes the record sets with a name (all types, or one type) from a Route 53 hosted zone
## - Deletes them exactly as Route 53 holds them, so alias, weighted and multi-value records work too
## - Succeeds without a change when there is nothing to delete
## - Arguments
##   - $1: Hosted zone ID
##   - $2: Record name (example: www.example.com)
##   - $3: Region (optional; Route 53 is global, so this is rarely needed)
##   - $4: Record type (optional, example: A; empty deletes every type)
function aws_route53_delete_record() {
    local FUNCTION_DESCRIPTION="AWS Route 53 (Delete Record)"
    local TMP_ZONE_ID="${1:-}"
    local TMP_NAME=""
    local TMP_TYPE="${4:-}"
    local TMP_FILTER=""
    local TMP_FILE_CHANGES=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Hosted zone ID" "${TMP_ZONE_ID}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Record name" "${2:-}" || return $?
    _aws_route53_record_name TMP_NAME "${2}"
    TMP_FILTER="Name=='$(_aws_jmespath_string "${TMP_NAME}")'"
    if [[ -n "${TMP_TYPE}" ]]; then
        TMP_FILTER+=" && Type=='$(_aws_jmespath_string "${TMP_TYPE^^}")'"
    fi
    generate_temp_file TMP_FILE_CHANGES "route53 change batch"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_NAME}] ${TMP_TYPE:+(${TMP_TYPE^^}) }in [${TMP_ZONE_ID}]"
    # The query turns the matching record sets into a change batch:
    #   {"Changes": [{"Action": "DELETE", "ResourceRecordSet": <the record set, exactly as returned>}, ...]}
    aws_cli "${TMP_FILE_CHANGES}" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} route53 list-resource-record-sets \
        --hosted-zone-id "${TMP_ZONE_ID}" \
        --query "{Changes: ResourceRecordSets[?${TMP_FILTER}].{Action: 'DELETE', ResourceRecordSet: @}}" --output json || return $?
    if command grep -q '"Changes": \[\]' "${TMP_FILE_CHANGES}"; then
        log "${FUNCTION_DESCRIPTION}: no matching records, nothing to delete"
        return 0
    fi
    log_add_from_file "${TMP_FILE_CHANGES}" "${FUNCTION_DESCRIPTION}: change batch"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} route53 change-resource-record-sets \
        --hosted-zone-id "${TMP_ZONE_ID}" --change-batch "file://${TMP_FILE_CHANGES}" || return $?
    log "${FUNCTION_DESCRIPTION}: deleted records"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_route53_get_record()
## - Loads the record sets with a name from a Route 53 hosted zone into an array, one element per
##   record set: NAME|TYPE|TTL|VALUES|WEIGHT|SET_IDENTIFIER, with multiple values separated by ";"
##   (an alias record shows ALIAS:<target> as its value)
## - Arguments
##   - $1: Array name to pass info to
##   - $2: Hosted zone ID
##   - $3: Record name (example: www.example.com)
##   - $4: Region (optional; Route 53 is global, so this is rarely needed)
function aws_route53_get_record() {
    local FUNCTION_DESCRIPTION="AWS Route 53 (Get Record)"
    local __RG_ARRAY="${1:-}"
    local __RG_NAME=""
    local __RG_RESPONSE=""
    local __RG_LINE=""
    local __RG_RECORD_NAME="" __RG_TYPE="" __RG_TTL="" __RG_WEIGHT="" __RG_SET_ID="" __RG_ALIAS="" __RG_VALUES=""
    if [[ ! "${__RG_ARRAY}" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
        log_error "${FUNCTION_DESCRIPTION}: invalid array name [${__RG_ARRAY}]"
        return $E_BAD_ARGS
    fi
    _aws_require "${FUNCTION_DESCRIPTION}" "Hosted zone ID" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Record name" "${3:-}" || return $?
    _aws_route53_record_name __RG_NAME "${3}"
    generate_temp_file __RG_RESPONSE "aws response"
    aws_cli "${__RG_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} route53 list-resource-record-sets \
        --hosted-zone-id "${2}" \
        --query "ResourceRecordSets[?Name=='$(_aws_jmespath_string "${__RG_NAME}")'].[Name,Type,TTL,Weight,SetIdentifier,AliasTarget.DNSName,join(';',ResourceRecords[].Value || \`[]\`)]" \
        --output text || return $?
    # the array name was checked above, so only the empty array is evaluated here
    eval "${__RG_ARRAY}=()"
    while IFS=$'\t' read -r __RG_RECORD_NAME __RG_TYPE __RG_TTL __RG_WEIGHT __RG_SET_ID __RG_ALIAS __RG_VALUES || [[ -n "${__RG_RECORD_NAME}" ]]; do
        [[ -n "${__RG_RECORD_NAME}" ]] || continue
        [[ "${__RG_TTL}" == "None" ]] && __RG_TTL=""
        [[ "${__RG_WEIGHT}" == "None" ]] && __RG_WEIGHT=""
        [[ "${__RG_SET_ID}" == "None" ]] && __RG_SET_ID=""
        [[ "${__RG_ALIAS}" != "None" && -n "${__RG_ALIAS}" ]] && __RG_VALUES="ALIAS:${__RG_ALIAS}"
        __RG_LINE="${__RG_RECORD_NAME}|${__RG_TYPE}|${__RG_TTL}|${__RG_VALUES}|${__RG_WEIGHT}|${__RG_SET_ID}"
        add_element_to_array "${__RG_ARRAY}" "${__RG_LINE}"
    done < "${__RG_RESPONSE}"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_route53_upsert_record()
## - Creates or replaces a record set in a Route 53 hosted zone
## - Values are written as given: a TXT value needs its own double quotes ("v=spf1 -all"), which
##   are escaped for JSON here
## - Arguments
##   - $1: Hosted zone ID
##   - $2: Record name (example: www.example.com)
##   - $3: Record type (example: A, CNAME, TXT)
##   - $4: TTL in seconds (optional, defaults to AWS_ROUTE53_DEFAULT_TTL, 600)
##   - $5: Values (separated by ";", example: 192.0.2.10;192.0.2.11)
##   - $6: Weight (optional, for weighted records)
##   - $7: Set identifier (optional, required with a weight)
##   - $8: Region (optional; Route 53 is global, so this is rarely needed)
function aws_route53_upsert_record() {
    local FUNCTION_DESCRIPTION="AWS Route 53 (Upsert Record)"
    local TMP_ZONE_ID="${1:-}"
    local TMP_NAME=""
    local TMP_TYPE="${3:-}"
    local TMP_TTL="${4:-${AWS_ROUTE53_DEFAULT_TTL}}"
    local TMP_WEIGHT="${6:-}"
    local TMP_SET_ID="${7:-}"
    local TMP_FILE_CHANGES=""
    local TMP_RECORDS=""
    local TMP_VALUE=""
    local -a TMP_VALUES=()
    _aws_require "${FUNCTION_DESCRIPTION}" "Hosted zone ID" "${TMP_ZONE_ID}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Record name" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Record type" "${TMP_TYPE}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Record value" "${5:-}" || return $?
    if ! is_int "${TMP_TTL}" || (( TMP_TTL < 0 )); then
        log_error "${FUNCTION_DESCRIPTION}: invalid TTL [${TMP_TTL}]"
        return $E_BAD_ARGS
    fi
    if [[ -n "${TMP_WEIGHT}" ]] && { ! is_int "${TMP_WEIGHT}" || [[ -z "${TMP_SET_ID}" ]]; }; then
        log_error "${FUNCTION_DESCRIPTION}: a weight must be a number and needs a set identifier"
        return $E_BAD_ARGS
    fi
    _aws_route53_record_name TMP_NAME "${2}"
    IFS=';' read -r -a TMP_VALUES <<< "${5}"
    for TMP_VALUE in "${TMP_VALUES[@]+"${TMP_VALUES[@]}"}"; do
        [[ -n "${TMP_VALUE}" ]] || continue
        TMP_RECORDS+="${TMP_RECORDS:+, }{\"Value\": \"$(json_escape "${TMP_VALUE}")\"}"
    done
    generate_temp_file TMP_FILE_CHANGES "route53 change batch"
    {
        printf '{"Changes": [{"Action": "UPSERT", "ResourceRecordSet": {\n'
        printf '  "Name": "%s", "Type": "%s", "TTL": %s,\n' "$(json_escape "${TMP_NAME}")" "$(json_escape "${TMP_TYPE^^}")" "${TMP_TTL}"
        if [[ -n "${TMP_WEIGHT}" ]]; then
            printf '  "Weight": %s, "SetIdentifier": "%s",\n' "${TMP_WEIGHT}" "$(json_escape "${TMP_SET_ID}")"
        fi
        printf '  "ResourceRecords": [%s]\n}}]}\n' "${TMP_RECORDS}"
    } > "${TMP_FILE_CHANGES}"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_NAME}] ${TMP_TYPE^^} in [${TMP_ZONE_ID}]"
    log_add_from_file "${TMP_FILE_CHANGES}" "${FUNCTION_DESCRIPTION}: change batch"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${8:+--region "${8}"} route53 change-resource-record-sets \
        --hosted-zone-id "${TMP_ZONE_ID}" --change-batch "file://${TMP_FILE_CHANGES}" || return $?
    log "${FUNCTION_DESCRIPTION}: record set saved"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_s3_cp_download()
## - Downloads an object from S3 to a local file
## - Arguments
##   - $1: S3 bucket
##   - $2: S3 key
##   - $3: Local file
##   - $4: Region (optional)
function aws_s3_cp_download() {
    local FUNCTION_DESCRIPTION="AWS S3 (Download)"
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 key" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Local file" "${3:-}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: [s3://${1}/${2#/}] -> [${3}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} s3 cp --only-show-errors "s3://${1}/${2#/}" "${3}" || return $?
    log "${FUNCTION_DESCRIPTION}: downloaded ($(return_filesize_of_file "${3}"))"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_s3_cp_upload()
## - Uploads a local file to S3
## - Arguments
##   - $1: S3 bucket
##   - $2: S3 key
##   - $3: Local file
##   - $4: Server-side encryption, AES256 or aws:kms (optional; empty uses the bucket's default encryption;
##         yes means AES256, as in earlier versions)
##   - $5: Storage class (optional, example: STANDARD_IA; empty uses STANDARD)
##   - $6: Region (optional)
function aws_s3_cp_upload() {
    local -a TMP_STORAGE=()
    local FUNCTION_DESCRIPTION="AWS S3 (Upload)"
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 key" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Local file" "${3:-}" || return $?
    if [[ ! -f "${3}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: local file not found [${3}]"
        return $E_OBJECT_NOT_FOUND
    fi
    _aws_s3_storage_options TMP_STORAGE "${4:-}" "${5:-}"
    log_notice "${FUNCTION_DESCRIPTION}: [${3}] ($(return_filesize_of_file "${3}")) -> [s3://${1}/${2#/}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${6:+--region "${6}"} s3 cp --only-show-errors \
        "${TMP_STORAGE[@]+"${TMP_STORAGE[@]}"}" "${3}" "s3://${1}/${2#/}" || return $?
    log "${FUNCTION_DESCRIPTION}: uploaded"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_s3_delete()
## - Deletes an object from S3
## - Arguments
##   - $1: S3 bucket
##   - $2: S3 key
##   - $3: Region (optional)
function aws_s3_delete() {
    local FUNCTION_DESCRIPTION="AWS S3 (Delete)"
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 key" "${2:-}" || return $?
    log_notice "${FUNCTION_DESCRIPTION}: [s3://${1}/${2#/}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} s3 rm --only-show-errors "s3://${1}/${2#/}" || return $?
    log "${FUNCTION_DESCRIPTION}: deleted"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_s3_move_in_bucket()
## - Moves (renames) an object within an S3 bucket
## - Arguments
##   - $1: S3 bucket
##   - $2: Source key
##   - $3: Target key
##   - $4: Server-side encryption, AES256 or aws:kms (optional; empty uses the bucket's default encryption;
##         yes means AES256, as in earlier versions)
##   - $5: Storage class (optional; empty uses STANDARD)
##   - $6: Region (optional)
function aws_s3_move_in_bucket() {
    local -a TMP_STORAGE=()
    local FUNCTION_DESCRIPTION="AWS S3 (Move)"
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Source key" "${2:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Target key" "${3:-}" || return $?
    _aws_s3_storage_options TMP_STORAGE "${4:-}" "${5:-}"
    log_notice "${FUNCTION_DESCRIPTION}: [s3://${1}/${2#/}] -> [s3://${1}/${3#/}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${6:+--region "${6}"} s3 mv --only-show-errors \
        "${TMP_STORAGE[@]+"${TMP_STORAGE[@]}"}" "s3://${1}/${2#/}" "s3://${1}/${3#/}" || return $?
    log "${FUNCTION_DESCRIPTION}: moved"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_s3_sync_download()
## - Copies new and changed files from an S3 prefix to a local directory (aws s3 sync)
## - Arguments
##   - $1: S3 bucket
##   - $2: S3 prefix (directory)
##   - $3: Local directory
##   - $4: Delete local files that are not in S3, yes or no (optional, defaults to no)
##   - $5: Exact timestamps, yes or no: also copy same-size files whose times differ (optional, defaults to no)
##   - $6: Exclude patterns, separated by ";" (optional, example: *.tmp;logs/*)
##   - $7: Include patterns, separated by ";" (optional, applied after the excludes)
##   - $8: Region (optional)
function aws_s3_sync_download() {
    local FUNCTION_DESCRIPTION="AWS S3 (Sync Download)"
    local -a TMP_OPTIONS=()
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Local directory" "${3:-}" || return $?
    if [[ ! -d "${3}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: local directory not found [${3}]"
        return $E_OBJECT_NOT_FOUND
    fi
    _aws_s3_sync_options TMP_OPTIONS "${4:-}" "${5:-}" "" "" "${6:-}" "${7:-}"
    log_notice "${FUNCTION_DESCRIPTION}: [s3://${1}/${2#/}] -> [${3}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${8:+--region "${8}"} s3 sync --only-show-errors "${TMP_OPTIONS[@]+"${TMP_OPTIONS[@]}"}" "s3://${1}/${2#/}" "${3}" || return $?
    log "${FUNCTION_DESCRIPTION}: synchronized"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_s3_sync_upload()
## - Copies new and changed files from a local directory to an S3 prefix (aws s3 sync)
## - Arguments
##   - $1: S3 bucket
##   - $2: S3 prefix (directory)
##   - $3: Local directory
##   - $4: Delete S3 objects that are not in the local directory, yes or no (optional, defaults to no)
##   - $5: Exact timestamps, yes or no (optional, defaults to no)
##   - $6: Server-side encryption, AES256 or aws:kms (optional; empty uses the bucket's default encryption)
##   - $7: Storage class (optional; empty uses STANDARD)
##   - $8: Exclude patterns, separated by ";" (optional)
##   - $9: Include patterns, separated by ";" (optional, applied after the excludes)
##   - $10: Region (optional)
function aws_s3_sync_upload() {
    local FUNCTION_DESCRIPTION="AWS S3 (Sync Upload)"
    local -a TMP_OPTIONS=()
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Local directory" "${3:-}" || return $?
    if [[ ! -d "${3}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: local directory not found [${3}]"
        return $E_OBJECT_NOT_FOUND
    fi
    _aws_s3_sync_options TMP_OPTIONS "${4:-}" "${5:-}" "${6:-}" "${7:-}" "${8:-}" "${9:-}"
    log_notice "${FUNCTION_DESCRIPTION}: [${3}] -> [s3://${1}/${2#/}]"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${10:+--region "${10}"} s3 sync --only-show-errors "${TMP_OPTIONS[@]+"${TMP_OPTIONS[@]}"}" "${3}" "s3://${1}/${2#/}" || return $?
    log "${FUNCTION_DESCRIPTION}: synchronized"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_s3_verify()
## - Checks that an S3 bucket (or a prefix in it) can be listed with the current credentials
## - Arguments
##   - $1: S3 bucket
##   - $2: S3 prefix (optional, defaults to the top of the bucket)
##   - $3: Log the listing, yes or no (optional, defaults to no)
##   - $4: Region (optional)
function aws_s3_verify() {
    local FUNCTION_DESCRIPTION="AWS S3 (Verify)"
    local TMP_RESPONSE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "S3 bucket" "${1:-}" || return $?
    generate_temp_file TMP_RESPONSE "aws response"
    aws_cli "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} s3 ls "s3://${1}/${2#/}" || return $?
    case "${3:-no}" in
        y|yes|Y|YES|1|true|TRUE) log_add_from_file "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}: s3://${1}/${2#/}";;
    esac
    log "${FUNCTION_DESCRIPTION}: [s3://${1}/${2#/}] is reachable"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_delete()
## - Deletes an SSM parameter; a parameter that does not exist is not an error
## - Arguments
##   - $1: Parameter name
##   - $2: Skip the check that it exists, yes or no (optional, defaults to no; yes saves a call when you
##         know it exists, and then a missing parameter is an error)
##   - $3: Region (optional)
function aws_ssm_parameter_delete() {
    local FUNCTION_DESCRIPTION="AWS SSM (Delete Parameter)"
    local TMP_RETURNVAL=0
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter name" "${1:-}" || return $?
    case "${2:-no}" in
        y|yes|Y|YES|1|true|TRUE) ;;
        *)
            aws_ssm_parameter_exists "${1}" "${3:-}" || TMP_RETURNVAL=$?
            if (( TMP_RETURNVAL == E_AWS_RESOURCE_NOT_FOUND )); then
                log "${FUNCTION_DESCRIPTION}: [${1}] does not exist, nothing to delete"
                return 0
            elif (( TMP_RETURNVAL != 0 )); then
                return ${TMP_RETURNVAL}
            fi;;
    esac
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} ssm delete-parameter --name "${1}" || return $?
    log "${FUNCTION_DESCRIPTION}: deleted [${1}]"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_exists()
## - Returns true if an SSM parameter exists (E_AWS_RESOURCE_NOT_FOUND if it does not; E_AWS_FAILURE if
##   AWS could not be asked, for example without permission)
## - Arguments
##   - $1: Parameter name
##   - $2: Region (optional)
function aws_ssm_parameter_exists() {
    local TMP_ERROR=""
    if TMP_ERROR="$(command aws ${2:+--region "${2}"} ssm get-parameter --name "${1:-}" --query Parameter.Name --output text 2>&1 >/dev/null)"; then
        return 0
    fi
    if [[ "${TMP_ERROR}" == *ParameterNotFound* ]]; then
        return $E_AWS_RESOURCE_NOT_FOUND
    fi
    log_warning "AWS SSM (Parameter Exists): could not check [${1:-}]: ${TMP_ERROR}"
    return $E_AWS_FAILURE
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_get()
## - Returns the value of an SSM parameter; a SecureString is decrypted unless told otherwise
## - The value is never logged
## - Arguments
##   - $1: Variable name to pass the value to
##   - $2: Parameter name
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameter_get() {
    local FUNCTION_DESCRIPTION="AWS SSM (Get Parameter)"
    local __SG_RESPONSE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Result variable" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter name" "${2:-}" || return $?
    generate_temp_file __SG_RESPONSE "aws response"
    log "${FUNCTION_DESCRIPTION}: [${2}]"
    aws_cli "${__SG_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} ssm get-parameter --name "${2}" \
        "$(_aws_ssm_decrypt_option "${3:-yes}")" --query Parameter.Value --output text || return $?
    _bamf_return "${1}" "$(<"${__SG_RESPONSE}")"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_get_file()
## - Writes a file stored in an SSM parameter (by aws_ssm_parameter_put_file) back to disk, mode 0400
## - Arguments
##   - $1: File to write
##   - $2: Parameter name
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameter_get_file() {
    local FUNCTION_DESCRIPTION="AWS SSM (Get File)"
    local TMP_FILE="${1:-}"
    local TMP_RESPONSE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Output file" "${TMP_FILE}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter name" "${2:-}" || return $?
    generate_temp_file TMP_RESPONSE "aws response"
    log "${FUNCTION_DESCRIPTION}: [${2}] -> [${TMP_FILE}]"
    aws_cli "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${4:+--region "${4}"} ssm get-parameter --name "${2}" \
        "$(_aws_ssm_decrypt_option "${3:-yes}")" --query Parameter.Value --output text || return $?
    _aws_ssm_write_decoded_file "${FUNCTION_DESCRIPTION}" "${TMP_RESPONSE}" "${TMP_FILE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_get_file_multi_part()
## - Writes a file stored in several SSM parameters (by aws_ssm_parameter_put_file_multi_part) back to
##   disk, mode 0400
## - Arguments
##   - $1: File to write
##   - $2: Parameter path holding the parts
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameter_get_file_multi_part() {
    local FUNCTION_DESCRIPTION="AWS SSM (Get File, Multi-Part)"
    local TMP_FILE="${1:-}"
    local TMP_PATH="${2:-}"
    local TMP_RESPONSE=""
    local TMP_ENCODED=""
    local TMP_NAME=""
    local TMP_VALUE=""
    local TMP_COUNT=0
    _aws_require "${FUNCTION_DESCRIPTION}" "Output file" "${TMP_FILE}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter path" "${TMP_PATH}" || return $?
    TMP_PATH="${TMP_PATH%/}"
    generate_temp_file TMP_RESPONSE "aws response"
    generate_temp_file TMP_ENCODED "encoded file"
    log "${FUNCTION_DESCRIPTION}: [${TMP_PATH}/part_*] -> [${TMP_FILE}]"
    _aws_ssm_parameters_by_path "${FUNCTION_DESCRIPTION}" "${TMP_RESPONSE}" "${TMP_PATH}" "${3:-yes}" "${4:-}" || return $?
    # the parts are named part_0001, part_0002, ...: sorted by name, they join in order
    while IFS=$'\t' read -r TMP_NAME TMP_VALUE; do
        if [[ "${TMP_NAME}" == "${TMP_PATH}"/part_[0-9][0-9][0-9][0-9] ]]; then
            printf '%s' "${TMP_VALUE}" >> "${TMP_ENCODED}"
            TMP_COUNT=$(( TMP_COUNT + 1 ))
        fi
    done < <(LC_ALL=C command sort "${TMP_RESPONSE}")
    if (( TMP_COUNT == 0 )); then
        log_error "${FUNCTION_DESCRIPTION}: no parts found under [${TMP_PATH}]"
        return $E_AWS_RESOURCE_NOT_FOUND
    fi
    log "${FUNCTION_DESCRIPTION}: joined ${TMP_COUNT} parts"
    _aws_ssm_write_decoded_file "${FUNCTION_DESCRIPTION}" "${TMP_ENCODED}" "${TMP_FILE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_get_file_silent()
## - Same as aws_ssm_parameter_get_file, but logs only if it fails
## - Arguments
##   - $1: File to write
##   - $2: Parameter name
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameter_get_file_silent() {
    run_silently aws_ssm_parameter_get_file "$@"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_get_silent()
## - Same as aws_ssm_parameter_get, but logs only if it fails
## - Arguments
##   - $1: Variable name to pass the value to
##   - $2: Parameter name
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameter_get_silent() {
    run_silently aws_ssm_parameter_get "$@"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_path_delete()
## - Deletes every SSM parameter under a path (recursively)
## - Arguments
##   - $1: Parameter path (example: /app/staging)
##   - $2: Region (optional)
function aws_ssm_parameter_path_delete() {
    local FUNCTION_DESCRIPTION="AWS SSM (Delete Parameter Path)"
    local TMP_PATH="${1:-}"
    local TMP_RESPONSE=""
    local TMP_NAME=""
    local TMP_COUNT=0
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter path" "${TMP_PATH}" || return $?
    TMP_PATH="${TMP_PATH%/}"
    if [[ -z "${TMP_PATH}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: refusing to delete every parameter in the account (path [/])"
        return $E_BAD_ARGS
    fi
    generate_temp_file TMP_RESPONSE "aws response"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_PATH}/]"
    aws_cli "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${2:+--region "${2}"} ssm get-parameters-by-path \
        --path "${TMP_PATH}/" --recursive --query 'Parameters[].[Name]' --output text || return $?
    while IFS= read -r TMP_NAME || [[ -n "${TMP_NAME}" ]]; do
        [[ -n "${TMP_NAME}" ]] || continue
        aws_cli "" "${FUNCTION_DESCRIPTION}" ${2:+--region "${2}"} ssm delete-parameter --name "${TMP_NAME}" || return $?
        log "- deleted [${TMP_NAME}]"
        TMP_COUNT=$(( TMP_COUNT + 1 ))
    done < "${TMP_RESPONSE}"
    log "${FUNCTION_DESCRIPTION}: deleted ${TMP_COUNT} parameters"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_put()
## - Creates or updates an SSM SecureString parameter
## - The value is passed to the AWS CLI through a 0600 file, never the command line, and never logged
## - Arguments
##   - $1: Parameter name
##   - $2: Value
##   - $3: KMS key (optional, defaults to the account's aws/ssm key)
##   - $4: Description (optional)
##   - $5: Overwrite an existing parameter, yes or no (optional, defaults to yes)
##   - $6: Region (optional)
function aws_ssm_parameter_put() {
    local FUNCTION_DESCRIPTION="AWS SSM (Put Parameter)"
    local TMP_FILE_VALUE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter name" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Value" "${2:-}" || return $?
    generate_temp_file_with_content TMP_FILE_VALUE "${2}" "parameter value"
    _aws_ssm_put "${FUNCTION_DESCRIPTION}" "${1}" "${TMP_FILE_VALUE}" "${3:-}" "${4:-}" "${5:-yes}" "${6:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_put_file()
## - Stores a file in an SSM SecureString parameter (base64 encoded); up to about 3 KB with the
##   Standard tier, use aws_ssm_parameter_put_file_multi_part for larger files
## - Arguments
##   - $1: Parameter name
##   - $2: File
##   - $3: KMS key (optional, defaults to the account's aws/ssm key)
##   - $4: Description (optional)
##   - $5: Overwrite an existing parameter, yes or no (optional, defaults to yes)
##   - $6: Region (optional)
function aws_ssm_parameter_put_file() {
    local FUNCTION_DESCRIPTION="AWS SSM (Put File)"
    local TMP_FILE_VALUE=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter name" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "File" "${2:-}" || return $?
    if [[ ! -f "${2}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: file not found [${2}]"
        return $E_OBJECT_NOT_FOUND
    fi
    generate_temp_file TMP_FILE_VALUE "parameter value"
    command base64 -w 0 < "${2}" > "${TMP_FILE_VALUE}"
    _aws_ssm_put "${FUNCTION_DESCRIPTION}" "${1}" "${TMP_FILE_VALUE}" "${3:-}" "${4:-}" "${5:-yes}" "${6:-}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_put_file_multi_part()
## - Stores a file of any size in SSM SecureString parameters <path>/part_0001, part_0002, ...
##   (base64 encoded, AWS_SSM_MULTI_PART_SIZE characters each)
## - With replace (the default), parameters already under the path are deleted first, so no stale
##   parts are left behind; without it, existing parameters under the path are an error
## - Arguments
##   - $1: Parameter path
##   - $2: File
##   - $3: KMS key (optional, defaults to the account's aws/ssm key)
##   - $4: Description (optional)
##   - $5: Replace what is under the path, yes or no (optional, defaults to yes)
##   - $6: Region (optional)
function aws_ssm_parameter_put_file_multi_part() {
    local FUNCTION_DESCRIPTION="AWS SSM (Put File, Multi-Part)"
    local TMP_PATH="${1:-}"
    local TMP_FILE_ENCODED=""
    local TMP_FILE_PART=""
    local TMP_PART=""
    local TMP_COUNT=0
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter path" "${TMP_PATH}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "File" "${2:-}" || return $?
    TMP_PATH="${TMP_PATH%/}"
    if [[ ! -f "${2}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: file not found [${2}]"
        return $E_OBJECT_NOT_FOUND
    fi
    generate_temp_file TMP_FILE_ENCODED "encoded file"
    command base64 -w "${AWS_SSM_MULTI_PART_SIZE}" < "${2}" > "${TMP_FILE_ENCODED}"
    log_notice "${FUNCTION_DESCRIPTION}: [${2}] -> [${TMP_PATH}/part_*]"
    case "${5:-yes}" in
        n|no|N|NO|0|false|FALSE)
            generate_temp_file TMP_FILE_PART "existing parameters"
            aws_cli "${TMP_FILE_PART}" "${FUNCTION_DESCRIPTION}" ${6:+--region "${6}"} ssm get-parameters-by-path \
                --path "${TMP_PATH}/" --recursive --query 'length(Parameters)' --output text || return $?
            if [[ "$(<"${TMP_FILE_PART}")" != "0" ]]; then
                log_error "${FUNCTION_DESCRIPTION}: parameters already exist under [${TMP_PATH}/] and replace is off"
                return $E_OBJECT_ALREADY_EXISTS
            fi;;
        *)
            aws_ssm_parameter_path_delete "${TMP_PATH}" "${6:-}" || return $?;;
    esac
    while IFS= read -r TMP_PART || [[ -n "${TMP_PART}" ]]; do
        [[ -n "${TMP_PART}" ]] || continue
        TMP_COUNT=$(( TMP_COUNT + 1 ))
        generate_temp_file_with_content TMP_FILE_PART "${TMP_PART}" "parameter part"
        _aws_ssm_put "${FUNCTION_DESCRIPTION}" "$(printf '%s/part_%04d' "${TMP_PATH}" "${TMP_COUNT}")" "${TMP_FILE_PART}" "${3:-}" "${4:-}" yes "${6:-}" || return $?
    done < "${TMP_FILE_ENCODED}"
    log "${FUNCTION_DESCRIPTION}: stored ${TMP_COUNT} parts"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_tag()
## - Adds or replaces tags on an SSM parameter
## - Arguments
##   - $1: Parameter name
##   - $2: Tags, as Key:Value pairs separated by ";" (example: team:web;environment:staging); the value
##         is everything after the first ":". AWS accepts letters, numbers, spaces and _ . : / = + - @
##   - $3: Region (optional)
function aws_ssm_parameter_tag() {
    local FUNCTION_DESCRIPTION="AWS SSM (Tag Parameter)"
    local TMP_FILE_TAGS=""
    local TMP_TAG=""
    local TMP_JSON=""
    local -a TMP_TAGS=()
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter name" "${1:-}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Tags" "${2:-}" || return $?
    IFS=';' read -r -a TMP_TAGS <<< "${2}"
    for TMP_TAG in "${TMP_TAGS[@]+"${TMP_TAGS[@]}"}"; do
        [[ -n "${TMP_TAG}" ]] || continue
        if [[ "${TMP_TAG}" != *:* ]]; then
            log_error "${FUNCTION_DESCRIPTION}: tag is not Key:Value [${TMP_TAG}]"
            return $E_BAD_ARGS
        fi
        TMP_JSON+="${TMP_JSON:+, }{\"Key\": \"$(json_escape "${TMP_TAG%%:*}")\", \"Value\": \"$(json_escape "${TMP_TAG#*:}")\"}"
    done
    generate_temp_file TMP_FILE_TAGS "tags json"
    printf '[%s]\n' "${TMP_JSON}" > "${TMP_FILE_TAGS}"
    log "${FUNCTION_DESCRIPTION}: [${1}] ${2}"
    aws_cli "" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} ssm add-tags-to-resource --resource-type Parameter \
        --resource-id "${1}" --tags "file://${TMP_FILE_TAGS}" || return $?
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameter_tag_path()
## - Adds or replaces tags on every SSM parameter under a path (recursively)
## - Arguments
##   - $1: Parameter path
##   - $2: Tags, as Key:Value pairs separated by ";"
##   - $3: Region (optional)
function aws_ssm_parameter_tag_path() {
    local FUNCTION_DESCRIPTION="AWS SSM (Tag Parameter Path)"
    local TMP_PATH="${1:-}"
    local TMP_RESPONSE=""
    local TMP_NAME=""
    _aws_require "${FUNCTION_DESCRIPTION}" "Parameter path" "${TMP_PATH}" || return $?
    _aws_require "${FUNCTION_DESCRIPTION}" "Tags" "${2:-}" || return $?
    TMP_PATH="${TMP_PATH%/}"
    generate_temp_file TMP_RESPONSE "aws response"
    log_notice "${FUNCTION_DESCRIPTION}: [${TMP_PATH}/]"
    aws_cli "${TMP_RESPONSE}" "${FUNCTION_DESCRIPTION}" ${3:+--region "${3}"} ssm get-parameters-by-path \
        --path "${TMP_PATH}/" --recursive --query 'Parameters[].[Name]' --output text || return $?
    while IFS= read -r TMP_NAME || [[ -n "${TMP_NAME}" ]]; do
        [[ -n "${TMP_NAME}" ]] || continue
        aws_ssm_parameter_tag "${TMP_NAME}" "${2}" "${3:-}" || return $?
    done < "${TMP_RESPONSE}"
    log "${FUNCTION_DESCRIPTION}: tagged path"
    return 0
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameters_to_key_value_file()
## - Writes the parameters under a path to a file as [name]=value lines, named by the last part of
##   each parameter name (read them with load_key_value_from_file); the file is created with mode 0600
## - Parameters under a "files" path element (files stored by the put_file functions) are skipped.
##   Values must be single lines.
## - Arguments
##   - $1: Parameter path
##   - $2: Output file
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameters_to_key_value_file() {
    _aws_ssm_parameters_to_file "AWS SSM (Parameters to Key Value File)" key_value skip_files "$@"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameters_to_key_value_file_multipart()
## - Same as aws_ssm_parameters_to_key_value_file, but keeps the parameters under a "files" path element
## - Arguments
##   - $1: Parameter path
##   - $2: Output file
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameters_to_key_value_file_multipart() {
    _aws_ssm_parameters_to_file "AWS SSM (Parameters to Key Value File)" key_value keep_files "$@"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: aws_ssm_parameters_to_properties_file()
## - Writes the parameters under a path to a file as name=value lines, named by the last part of each
##   parameter name (read them with load_property_from_file); the file is created with mode 0600
## - Parameters under a "files" path element are skipped. Values must be single lines.
## - Arguments
##   - $1: Parameter path
##   - $2: Output file
##   - $3: Decrypt, yes or no (optional, defaults to yes)
##   - $4: Region (optional)
function aws_ssm_parameters_to_properties_file() {
    _aws_ssm_parameters_to_file "AWS SSM (Parameters to Properties File)" properties skip_files "$@"
}

###------------------------------------------------------------------------------------------------
# Internal helpers (not part of the public function list; names start with an underscore)

# _aws_require <description> <what> <value>: logs and returns E_BAD_ARGS if the value is empty
function _aws_require() {
    if [[ -z "${3}" ]]; then
        log_error "${1}: ${2} not specified"
        return $E_BAD_ARGS
    fi
}

# _aws_cli_error_is_retryable <aws cli exit code> <error output>: true if another attempt can succeed
#   AWS CLI exit codes: 252 invalid command or arguments, 253 invalid configuration or credentials,
#   254 the service returned an error, 255 any other failure (usually the network)
function _aws_cli_error_is_retryable() {
    case "${1}" in
        252|253) return 1;;
        254)
            [[ "${2}" =~ (Throttl|RequestLimitExceeded|TooManyRequests|ServiceUnavailable|InternalError|InternalFailure|SlowDown|RequestTimeout|PriorRequestNotComplete) ]]
            return;;
    esac
    return 0
}

# _aws_metadata_get <path> <variable name> [optional]: an instance metadata value (IMDSv2);
# with "optional", a missing value returns E_OBJECT_NOT_FOUND without logging an error
function _aws_metadata_get() {
    local __AM_VALUE=""
    local __AM_HTTP_CODE=""
    local __AM_RESPONSE=""
    aws_metadata_token || return $?
    generate_temp_file __AM_RESPONSE "metadata response"
    __AM_HTTP_CODE="$(command curl --silent --retry 3 --connect-timeout 2 -o "${__AM_RESPONSE}" -w '%{http_code}' \
        -H "X-aws-ec2-metadata-token: ${AWS_METADATA_TOKEN}" "${AWS_METADATA_URL}/latest/${1}")" || true
    if [[ "${__AM_HTTP_CODE}" == "200" ]]; then
        __AM_VALUE="$(<"${__AM_RESPONSE}")"
        _bamf_return "${2}" "${__AM_VALUE}"
        return 0
    fi
    _bamf_return "${2}" ""
    if [[ "${__AM_HTTP_CODE}" == "404" ]]; then
        [[ "${3:-}" == "optional" ]] || log_error "AWS Metadata: value not available [${1}]"
        return $E_OBJECT_NOT_FOUND
    fi
    log_error "AWS Metadata: could not read [${1}] (http_code::${__AM_HTTP_CODE:-none})"
    return $E_AWS_FAILURE
}

# _aws_metadata_identity_value <key> <variable name>: a value from the instance identity document
function _aws_metadata_identity_value() {
    local __AI_DOCUMENT=""
    _aws_metadata_get dynamic/instance-identity/document __AI_DOCUMENT || return $?
    json_extract_from_string "${2}" "${1}" "${__AI_DOCUMENT}"
}

# _aws_metadata_route_ids <private or public> <variable name> <region> <vpc id>: see aws_metadata_route_id_private
function _aws_metadata_route_ids() {
    local __AR_REGION="${3}"
    local __AR_VPC_ID="${4}"
    local __AR_RESPONSE=""
    local -a __AR_IDS=()
    [[ -n "${__AR_REGION}" ]] || aws_metadata_region __AR_REGION || return $?
    [[ -n "${__AR_VPC_ID}" ]] || aws_metadata_vpc_id __AR_VPC_ID || return $?
    generate_temp_file __AR_RESPONSE "aws response"
    aws_cli "${__AR_RESPONSE}" "AWS Metadata (Route Table IDs)" --region "${__AR_REGION}" ec2 describe-route-tables \
        --filters "Name=vpc-id,Values=${__AR_VPC_ID}" "Name=tag:Network,Values=${1}" \
        --query 'RouteTables[].RouteTableId' --output text || return $?
    read -r -a __AR_IDS < "${__AR_RESPONSE}" || true
    local IFS=' '
    _bamf_return "${2}" "${__AR_IDS[*]+"${__AR_IDS[*]}"}"
}

# _aws_query_value <variable name> <description> <response file> <aws cli arguments...>: runs a query
# that returns one text value; "None" (the AWS CLI's text for a missing value) becomes empty
function _aws_query_value() {
    local __AQ_RESULT="${1}"
    local __AQ_DESCRIPTION="${2}"
    local __AQ_RESPONSE="${3}"
    local __AQ_VALUE=""
    shift 3
    aws_cli "${__AQ_RESPONSE}" "${__AQ_DESCRIPTION}" "$@" --output text || return $?
    __AQ_VALUE="$(<"${__AQ_RESPONSE}")"
    [[ "${__AQ_VALUE}" == "None" ]] && __AQ_VALUE=""
    _bamf_return "${__AQ_RESULT}" "${__AQ_VALUE}"
}

# _aws_codedeploy_group_value <variable name> <application> <group> <region> <query> <label>
function _aws_codedeploy_group_value() {
    local __AG_DESCRIPTION="AWS CodeDeploy (Get ${6})"
    local __AG_RESPONSE=""
    _aws_require "${__AG_DESCRIPTION}" "Application name" "${2}" || return $?
    _aws_require "${__AG_DESCRIPTION}" "Deployment group" "${3}" || return $?
    generate_temp_file __AG_RESPONSE "aws response"
    _aws_query_value "${1}" "${__AG_DESCRIPTION}" "${__AG_RESPONSE}" ${4:+--region "${4}"} deploy get-deployment-group \
        --application-name "${2}" --deployment-group-name "${3}" --query "${5}"
}

# _aws_codedeploy_bundle_type <variable name> <s3 key>: the CodeDeploy bundle type for a file name
function _aws_codedeploy_bundle_type() {
    local __AB_TYPE=""
    case "${2,,}" in
        *.zip)            __AB_TYPE="zip";;
        *.tgz|*.tar.gz)   __AB_TYPE="tgz";;
        *.tar)            __AB_TYPE="tar";;
        *.yaml|*.yml)     __AB_TYPE="YAML";;
        *.json)           __AB_TYPE="JSON";;
        *)
            log_error "AWS CodeDeploy: cannot tell the bundle type from the file name [${2}] (expected .zip, .tgz, .tar.gz, .tar, .yaml, .yml or .json)"
            return $E_BAD_ARGS;;
    esac
    _bamf_return "${1}" "${__AB_TYPE}"
}

# _aws_ec2_instance_value <variable name> <instance id> <region> <query> <label>
function _aws_ec2_instance_value() {
    local __AE_DESCRIPTION="AWS EC2 (Get ${5})"
    local __AE_RESPONSE=""
    _aws_require "${__AE_DESCRIPTION}" "Instance ID" "${2}" || return $?
    generate_temp_file __AE_RESPONSE "aws response"
    _aws_query_value "${1}" "${__AE_DESCRIPTION}" "${__AE_RESPONSE}" ${3:+--region "${3}"} ec2 describe-instances --instance-ids "${2}" --query "${4}"
}

# _aws_eni_value <variable name> <network interface id> <region> <query> <label>
function _aws_eni_value() {
    local __AN_DESCRIPTION="AWS ENI (Get ${5})"
    local __AN_RESPONSE=""
    _aws_require "${__AN_DESCRIPTION}" "Network interface ID" "${2}" || return $?
    generate_temp_file __AN_RESPONSE "aws response"
    _aws_query_value "${1}" "${__AN_DESCRIPTION}" "${__AN_RESPONSE}" ${3:+--region "${3}"} ec2 describe-network-interfaces --network-interface-ids "${2}" --query "${4}"
}

# _aws_kms_require_tools <description>: openssl and tar are needed for the KMS file functions
function _aws_kms_require_tools() {
    local TMP_COMMAND=""
    for TMP_COMMAND in openssl tar; do
        if ! command -v "${TMP_COMMAND}" >/dev/null 2>&1; then
            log_error "${1}: ${TMP_COMMAND} is not installed"
            return $E_MISSING_DEPENDENCIES
        fi
    done
}

# _aws_jmespath_string <value>: the value escaped for a JMESPath raw string literal ('...')
function _aws_jmespath_string() {
    local TMP_VALUE="${1}"
    TMP_VALUE="${TMP_VALUE//\\/\\\\}"
    printf '%s' "${TMP_VALUE//\'/\\\'}"
}

# _aws_route53_record_name <variable name> <name>: the name as Route 53 returns it (lowercase, final dot)
function _aws_route53_record_name() {
    local __RN_NAME="${2,,}"
    [[ "${__RN_NAME}" == *. ]] || __RN_NAME+="."
    _bamf_return "${1}" "${__RN_NAME}"
}

# _aws_s3_sync_options <array name> <delete> <exact timestamps> <sse> <storage class> <excludes> <includes>
function _aws_s3_sync_options() {
    local __SO_ARRAY="${1}"
    local __SO_PATTERN=""
    local -a __SO_PATTERNS=()
    case "${2}" in y|yes|Y|YES|1|true|TRUE) add_element_to_array "${__SO_ARRAY}" --delete;; esac
    case "${3}" in y|yes|Y|YES|1|true|TRUE) add_element_to_array "${__SO_ARRAY}" --exact-timestamps;; esac
    _aws_s3_storage_options "${__SO_ARRAY}" "${4}" "${5}"
    IFS=';' read -r -a __SO_PATTERNS <<< "${6}"
    for __SO_PATTERN in "${__SO_PATTERNS[@]+"${__SO_PATTERNS[@]}"}"; do
        [[ -n "${__SO_PATTERN}" ]] && add_element_to_array "${__SO_ARRAY}" --exclude "${__SO_PATTERN}"
    done
    __SO_PATTERNS=()
    IFS=';' read -r -a __SO_PATTERNS <<< "${7}"
    for __SO_PATTERN in "${__SO_PATTERNS[@]+"${__SO_PATTERNS[@]}"}"; do
        [[ -n "${__SO_PATTERN}" ]] && add_element_to_array "${__SO_ARRAY}" --include "${__SO_PATTERN}"
    done
    return 0
}

# _aws_ssm_decrypt_option <yes or no>: prints --with-decryption or --no-with-decryption
function _aws_ssm_decrypt_option() {
    case "${1}" in
        n|no|N|NO|0|false|FALSE) printf '%s' "--no-with-decryption";;
        *)                       printf '%s' "--with-decryption";;
    esac
}

# _aws_ssm_put <description> <name> <value file> <kms key> <description text> <overwrite> <region>
function _aws_ssm_put() {
    local -a __SP_OPTIONS=()
    case "${6}" in
        n|no|N|NO|0|false|FALSE) ;;
        *) __SP_OPTIONS+=( --overwrite );;
    esac
    [[ -n "${4}" ]] && __SP_OPTIONS+=( --key-id "${4}" )
    [[ -n "${5}" ]] && __SP_OPTIONS+=( --description "${5}" )
    log "${1}: [${2}] = [${LOG_MASK}]"
    aws_cli "" "${1}" ${7:+--region "${7}"} ssm put-parameter --name "${2}" --value "file://${3}" --type SecureString \
        "${__SP_OPTIONS[@]+"${__SP_OPTIONS[@]}"}" || return $?
    return 0
}

# _aws_ssm_parameters_by_path <description> <response file> <path> <decrypt> <region>: name<TAB>value lines
function _aws_ssm_parameters_by_path() {
    # the AWS CLI follows the pages of the results itself
    aws_cli "${2}" "${1}" ${5:+--region "${5}"} ssm get-parameters-by-path --path "${3%/}/" --recursive \
        "$(_aws_ssm_decrypt_option "${4}")" --query 'Parameters[].[Name,Value]' --output text
}

# _aws_ssm_write_decoded_file <description> <base64 file> <output file>: decodes into the output file, mode 0400
function _aws_ssm_write_decoded_file() {
    local TMP_DECODED=""
    generate_temp_file TMP_DECODED "decoded file"
    if ! command base64 -d < "${2}" > "${TMP_DECODED}" 2>/dev/null; then
        log_error "${1}: the parameter does not hold a base64 encoded file"
        return $E_BASE64_CONVERSION_FAILURE
    fi
    # replace the output file, even one left read-only (0400) by an earlier run; ${3:?} stops the
    # removal if the name is ever empty
    command rm -f -- "${3:?}"
    if ! command cp -- "${TMP_DECODED}" "${3}" 2>/dev/null; then
        log_error "${1}: could not write file [${3}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    command chmod 0400 "${3}"
    log "${1}: wrote [${3}] ($(return_filesize_of_file "${3}"))"
    return 0
}

# _aws_ssm_parameters_to_file <description> <key_value or properties> <skip_files or keep_files> <path> <output file> <decrypt> <region>
function _aws_ssm_parameters_to_file() {
    local TMP_DESCRIPTION="${1}"
    local TMP_FORMAT="${2}"
    local TMP_FILES="${3}"
    local TMP_PATH="${4:-}"
    local TMP_FILE="${5:-}"
    local TMP_RESPONSE=""
    local TMP_NAME=""
    local TMP_VALUE=""
    local TMP_COUNT=0
    _aws_require "${TMP_DESCRIPTION}" "Parameter path" "${TMP_PATH}" || return $?
    _aws_require "${TMP_DESCRIPTION}" "Output file" "${TMP_FILE}" || return $?
    TMP_PATH="${TMP_PATH%/}"
    generate_temp_file TMP_RESPONSE "aws response"
    log "${TMP_DESCRIPTION}: [${TMP_PATH}/] -> [${TMP_FILE}]"
    _aws_ssm_parameters_by_path "${TMP_DESCRIPTION}" "${TMP_RESPONSE}" "${TMP_PATH}" "${6:-yes}" "${7:-}" || return $?
    # the file holds decrypted values, so it is created readable by its owner only
    if ! ( umask 077 && printf '## START\n# Generated: %(%Y-%m-%d %H:%M:%S %Z)T\n' -1 > "${TMP_FILE}" ) 2>/dev/null; then
        log_error "${TMP_DESCRIPTION}: could not write file [${TMP_FILE}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    while IFS=$'\t' read -r TMP_NAME TMP_VALUE || [[ -n "${TMP_NAME}" ]]; do
        [[ -n "${TMP_NAME}" ]] || continue
        if [[ "${TMP_FILES}" == "skip_files" && "${TMP_NAME}" == */files/* ]]; then
            continue
        fi
        if [[ "${TMP_FORMAT}" == "properties" ]]; then
            printf '%s=%s\n' "${TMP_NAME##*/}" "${TMP_VALUE}" >> "${TMP_FILE}"
        else
            printf '[%s]=%s\n' "${TMP_NAME##*/}" "${TMP_VALUE}" >> "${TMP_FILE}"
        fi
        TMP_COUNT=$(( TMP_COUNT + 1 ))
    done < "${TMP_RESPONSE}"
    printf '## END\n' >> "${TMP_FILE}"
    log "${TMP_DESCRIPTION}: wrote ${TMP_COUNT} parameters"
    return 0
}

# _aws_s3_storage_options <array name> <encryption> <storage class>: adds the aws s3 options to the array.
# Earlier versions took yes/no for both: yes encryption means AES256; a yes/no storage class (once
# "reduced redundancy") is ignored, because Reduced Redundancy now costs more than Standard.
function _aws_s3_storage_options() {
    case "${2}" in
        ""|n|no|N|NO|false|FALSE) ;;
        y|yes|Y|YES|true|TRUE) add_element_to_array "${1}" --sse AES256;;
        *) add_element_to_array "${1}" --sse "${2}";;
    esac
    case "${3}" in
        ""|y|yes|Y|YES|true|TRUE|n|no|N|NO|false|FALSE) ;;
        *) add_element_to_array "${1}" --storage-class "${3}";;
    esac
}
