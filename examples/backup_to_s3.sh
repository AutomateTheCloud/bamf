#!/bin/bash
###------------------------------------------------------------------------------------------------
# script:  backup_to_s3.sh
# purpose: Archives a directory, encrypts it with a KMS key, and uploads it to S3
# version: 1.0.0
#
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
###------------------------------------------------------------------------------------------------
# What this example shows:
#   - three helper files working together: tar, aws (KMS and S3)
#   - envelope encryption: KMS creates a one-time data key, the archive is encrypted locally, and
#     only the KMS-encrypted copy of the key travels with it (restore with aws_kms_decrypt_file)
#   - the unencrypted archive exists only in a temporary directory, removed however the script ends
#   - aws_credentials_select: run it with any AWS profile type (-P), or the default credentials
#
# Needs: the AWS CLI, openssl 1.1.1 or newer, kms:GenerateDataKey on the key, s3:PutObject on the bucket
#
# Try it:
#   examples/backup_to_s3.sh -s /var/www -b my-backups -k alias/backups -r us-east-1

###------------------------------------------------------------------------------------------------
## Config
# shellcheck disable=SC2034 # read by bamf.inc
SELF_IDENTITY="backup_to_s3"
# shellcheck disable=SC2034 # read by usage
ARGUMENTS_DESCRIPTION=(
    '-s <directory> : Directory to back up'
    '-b <bucket> : S3 bucket'
    '-k <kms key> : KMS key ID, ARN or alias'
    '-p <prefix> : S3 prefix (optional, defaults to backups/<host name>)'
    '-r <region> : Region (optional, defaults to the AWS CLI setting)'
    '-P <profile> : AWS profile (optional)'
)

###------------------------------------------------------------------------------------------------
## Load BAMF
# shellcheck source=../bamf.inc
source "$(dirname "${BASH_SOURCE[0]}")/../bamf.inc" || exit 3
REQUIRED_SOURCE_FILES+=( aws tar )
REQUIRED_EXECUTABLES+=( 'openssl' )

###------------------------------------------------------------------------------------------------
## Variables
DIRECTORY_SOURCE=""
BUCKET=""
KMS_KEY=""
PREFIX="backups/$(return_hostname)"
REGION=""
PROFILE=""

###------------------------------------------------------------------------------------------------
## Main
while getopts "hVs:b:k:p:r:P:" OPTION; do
    case "${OPTION}" in
        s) DIRECTORY_SOURCE="${OPTARG%/}";;
        b) BUCKET="${OPTARG}";;
        k) KMS_KEY="${OPTARG}";;
        p) PREFIX="${OPTARG%/}";;
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
if [[ -z "${DIRECTORY_SOURCE}" || -z "${BUCKET}" || -z "${KMS_KEY}" ]]; then
    exit_logic "${E_BAD_ARGS}" "-s, -b and -k are required"
fi
if [[ ! -d "${DIRECTORY_SOURCE}" ]]; then
    exit_logic "${E_OBJECT_NOT_FOUND}" "directory not found [${DIRECTORY_SOURCE}]"
fi
aws_credentials_select "${PROFILE}" || exit_logic $?
printf -v STAMP '%(%Y%m%dT%H%M%S)T' -1
ARCHIVE_NAME="${DIRECTORY_SOURCE##*/}-${STAMP}.tar.gz"
generate_temp_directory DIRECTORY_WORK "backup work"
mkdir -p "${DIRECTORY_WORK}/encrypted"
log "- Source:      [${DIRECTORY_SOURCE}]"
log "- Destination: [s3://${BUCKET}/${PREFIX}/${ARCHIVE_NAME}.enc]"

line_break
log_highlight "Archive"
tar_create "${DIRECTORY_WORK}/${ARCHIVE_NAME}" "${DIRECTORY_SOURCE}" || exit_logic $?

line_break
log_highlight "Encrypt"
aws_kms_encrypt_file "${DIRECTORY_WORK}/${ARCHIVE_NAME}" "${DIRECTORY_WORK}/encrypted" "${KMS_KEY}" "${REGION}" || exit_logic $?

line_break
log_highlight "Upload"
aws_s3_cp_upload "${BUCKET}" "${PREFIX}/${ARCHIVE_NAME}.enc" "${DIRECTORY_WORK}/encrypted/${ARCHIVE_NAME}.enc" "" "" "${REGION}" || exit_logic $?

log_success "Backed up [${DIRECTORY_SOURCE}] to [s3://${BUCKET}/${PREFIX}/${ARCHIVE_NAME}.enc]"
exit_logic 0
