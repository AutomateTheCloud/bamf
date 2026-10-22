###------------------------------------------------------------------------------------------------
# script:  bamf.terraform.inc
# purpose: BAMF helper file for Terraform: read the outputs of a workspace kept in an S3 backend
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
#
# usage:
#          REQUIRED_SOURCE_FILES+=( terraform )
#          start_logic
#          terraform_s3_remote_workspace_to_file outputs.json prod my-state-bucket network/terraform.tfstate us-east-1 || exit_logic $?
#          terraform_output_value outputs.json vpc_id VPC_ID || exit_logic $?
#
# requirements:
#          - Terraform 0.14 or newer; jq (terraform_output_value only)
#
# function list:
#          - terraform_output_value
#          - terraform_s3_remote_workspace_to_file
###------------------------------------------------------------------------------------------------

###------------------------------------------------------------------------------------------------
## Dependencies
REQUIRED_EXECUTABLES+=('terraform')

###------------------------------------------------------------------------------------------------
## Exit Codes
define_exit_code E_TERRAFORM_INIT_FAILURE   170 "Terraform Init Failure"
define_exit_code E_TERRAFORM_PLAN_FAILURE   171 "Terraform Plan Failure"
define_exit_code E_TERRAFORM_APPLY_FAILURE  172 "Terraform Apply Failure"
define_exit_code E_TERRAFORM_OUTPUT_FAILURE 173 "Terraform Output Failure"

###------------------------------------------------------------------------------------------------
## FUNCTION: terraform_output_value()
## - Returns a value from a file of Terraform outputs (JSON, as terraform_s3_remote_workspace_to_file
##   writes it); a list or map is returned as JSON. Empty if the output does not exist.
## - Arguments
##   - $1: Outputs file (JSON)
##   - $2: Output name; use dots for nested values (example: vpc.id)
##   - $3: Variable name to pass info to (optional, if not specified the value is printed)
function terraform_output_value() {
    local FUNCTION_DESCRIPTION="Terraform (Output Value)"
    local __TO_FILE="${1:-}"
    local __TO_NAME="${2:-}"
    local __TO_VALUE=""
    if [[ -z "${__TO_FILE}" || -z "${__TO_NAME}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: outputs file and output name are required"
        return $E_BAD_ARGS
    fi
    if [[ ! -f "${__TO_FILE}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: outputs file not found [${__TO_FILE}]"
        return $E_OBJECT_NOT_FOUND
    fi
    if ! command -v jq >/dev/null 2>&1; then
        log_error "${FUNCTION_DESCRIPTION}: jq is not installed"
        return $E_MISSING_DEPENDENCIES
    fi
    # the name is passed to jq as data (--arg), never pasted into the jq program
    if ! __TO_VALUE="$(command jq -r --arg name "${__TO_NAME}" 'getpath($name | split(".")) // empty' "${__TO_FILE}" 2>&1)"; then
        log_error "${FUNCTION_DESCRIPTION}: could not read [${__TO_FILE}]: ${__TO_VALUE}"
        return $E_FILE_CONVERSION_FAILURE
    fi
    _bamf_return "${3:-}" "${__TO_VALUE}"
}

###------------------------------------------------------------------------------------------------
## FUNCTION: terraform_s3_remote_workspace_to_file()
## - Writes the outputs of a Terraform workspace whose state is in an S3 backend to a file (JSON)
## - Works in a temporary directory with a generated configuration (one terraform_remote_state data
##   source), so it needs no access to the configuration that owns the state; the AWS credentials
##   need read access to the state object
## - Arguments
##   - $1: Output file
##   - $2: Workspace (default for the default workspace)
##   - $3: S3 bucket
##   - $4: State key (optional, defaults to terraform.tfstate)
##   - $5: Region of the bucket
function terraform_s3_remote_workspace_to_file() {
    local FUNCTION_DESCRIPTION="Terraform (S3 Remote Workspace to File)"
    local TMP_FILE_OUTPUT="${1:-}"
    local TMP_WORKSPACE="${2:-}"
    local TMP_BUCKET="${3:-}"
    local TMP_KEY="${4:-terraform.tfstate}"
    local TMP_REGION="${5:-}"
    local TMP_DIRECTORY=""
    local TMP_FILE_LOG=""
    local TMP_FILE_JSON=""
    if [[ -z "${TMP_FILE_OUTPUT}" || -z "${TMP_WORKSPACE}" || -z "${TMP_BUCKET}" || -z "${TMP_REGION}" ]]; then
        log_error "${FUNCTION_DESCRIPTION}: output file, workspace, bucket and region are required"
        return $E_BAD_ARGS
    fi
    generate_temp_directory TMP_DIRECTORY "terraform working directory"
    generate_temp_file TMP_FILE_LOG "terraform output"
    generate_temp_file TMP_FILE_JSON "terraform outputs"
    log_notice "${FUNCTION_DESCRIPTION}: started"
    log "- Workspace:   [${TMP_WORKSPACE}]"
    log "- State:       [s3://${TMP_BUCKET}/${TMP_KEY}] (${TMP_REGION})"
    log "- Output File: [${TMP_FILE_OUTPUT}]"
    command cat > "${TMP_DIRECTORY}/main.tf" <<'TERRAFORM'
terraform {
  required_version = ">= 0.14"
}

variable "workspace" { type = string }
variable "region" { type = string }
variable "s3_bucket" { type = string }
variable "key" { type = string }

data "terraform_remote_state" "this" {
  backend   = "s3"
  workspace = var.workspace
  config = {
    bucket = var.s3_bucket
    key    = var.key
    region = var.region
  }
}

output "output" {
  value = data.terraform_remote_state.this.outputs
}
TERRAFORM
    # -chdir runs Terraform in the working directory without changing the calling script's directory
    if ! command terraform -chdir="${TMP_DIRECTORY}" init -input=false -no-color >"${TMP_FILE_LOG}" 2>&1; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: terraform init"
        return $E_TERRAFORM_INIT_FAILURE
    fi
    if ! command terraform -chdir="${TMP_DIRECTORY}" apply -input=false -no-color -auto-approve \
        -var "workspace=${TMP_WORKSPACE}" -var "region=${TMP_REGION}" -var "s3_bucket=${TMP_BUCKET}" -var "key=${TMP_KEY}" >"${TMP_FILE_LOG}" 2>&1; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: terraform apply"
        return $E_TERRAFORM_APPLY_FAILURE
    fi
    if ! command terraform -chdir="${TMP_DIRECTORY}" output -json output >"${TMP_FILE_JSON}" 2>"${TMP_FILE_LOG}"; then
        log_add_from_file "${TMP_FILE_LOG}" "${FUNCTION_DESCRIPTION}: terraform output"
        return $E_TERRAFORM_OUTPUT_FAILURE
    fi
    if ! command cp -- "${TMP_FILE_JSON}" "${TMP_FILE_OUTPUT}" 2>/dev/null; then
        log_error "${FUNCTION_DESCRIPTION}: could not write file [${TMP_FILE_OUTPUT}]"
        return $E_OBJECT_FAILED_TO_CREATE
    fi
    log "${FUNCTION_DESCRIPTION}: wrote the workspace outputs"
    return 0
}
