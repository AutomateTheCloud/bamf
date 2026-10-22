# BAMF helper files

[`bamf.inc`](../bamf.inc) is the core of BAMF. The files in this directory add functions for one tool or service each. A script loads only the ones it uses, by name:

```bash
source "/opt/bamf/bamf.inc" || exit 3
REQUIRED_SOURCE_FILES+=( aws docker )   # bamf.aws.inc, then bamf.docker.inc
start_logic
```

`start_logic` loads them in the order listed, once each, before it checks `REQUIRED_EXECUTABLES`, so a helper's own requirements (the `aws` command, for example) are checked too. A name is looked up in this `helpers` directory, then beside `bamf.inc`, so a flat copy with every file in one folder works as well. A path loads that file instead. `source_include <name>` loads a helper at any other point.

| Helper file | What it covers |
|---|---|
| [`bamf.aws.inc`](bamf.aws.inc) | AWS CLI calls with retries, credential selection, EC2 instance metadata (IMDSv2), CloudFront, CodeDeploy, EC2, ENI, KMS, Route 53, S3, and SSM Parameter Store |
| [`bamf.curl.inc`](bamf.curl.inc) | HTTP requests with retries for network errors and 408, 429 and 5xx responses; waiting for a service to answer |
| [`bamf.docker.inc`](bamf.docker.inc) | Installing Docker and a pinned, checksum-verified Docker Compose; Compose up and down; Amazon ECR login; cleanup |
| [`bamf.git.inc`](bamf.git.inc) | Tagging and pushing a release, checking a clone is up to date, listing a GitHub organization's repositories |
| [`bamf.npm.inc`](bamf.npm.inc) | Reading `package.json`, checking whether a version is already published, writing `.npmrc` |
| [`bamf.pkg.inc`](bamf.pkg.inc) | Installing and updating packages with apt-get, dnf or yum, without prompts and without losing the dpkg lock race at boot |
| [`bamf.tar.inc`](bamf.tar.inc) | Creating and extracting archives, with the compression taken from the file name |
| [`bamf.terraform.inc`](bamf.terraform.inc) | Reading the outputs of a Terraform workspace kept in an S3 backend |


A helper file can load another: `bamf.docker.inc` loads `bamf.pkg.inc` for `docker_install`.

## Conventions

- **Every function starts with the helper's name** (`aws_`, `curl_`, `docker_`, `git_`, `npm_`, `tar_`, `terraform_`; `package_management_` for `bamf.pkg.inc`), so it is clear where a function comes from.
- **Functions return an exit code** and never end the script themselves; the script decides: `docker_compose_up compose.yaml || exit_logic $?`.
- **Exit codes** come from the 81-120 and 151-200 ranges, defined with `define_exit_code` (for example `E_AWS_FAILURE` 81, `E_DOCKER_FAILURE` 101).
- **Nothing runs when a helper is loaded**: it only defines functions, variables and exit codes, and adds to `REQUIRED_EXECUTABLES`.
- **Secrets never reach a command line or the log.** Passwords, tokens and secret values go to commands through 0600 temporary files or standard input.

## Writing your own

A helper file for your team's own tools follows the same pattern. Save it as `bamf.<name>.inc` in this directory (or anywhere, and load it by path):

```bash
REQUIRED_EXECUTABLES+=( 'mytool' )
define_exit_code E_MYTOOL_FAILURE 201 "mytool failed"
declare -g MYTOOL_TIMEOUT="${MYTOOL_TIMEOUT:-30}"

## FUNCTION: mytool_sync()
## - Runs mytool sync for a project
## - Arguments
##   - $1: Project name
function mytool_sync() {
    command mytool sync --timeout "${MYTOOL_TIMEOUT}" "${1:-}" || return $E_MYTOOL_FAILURE
}
```

`start_logic` sources helper files from inside a function, so declare variables with `declare -g`; a plain `declare` would be local to that function and disappear.

## Notes on each helper

### AWS: `bamf.aws.inc`

```bash
REQUIRED_SOURCE_FILES+=( aws )
start_logic
aws_credentials_select "${AWS_PROFILE_TARGET:-}"     # a profile, or the AWS CLI's default credentials
aws_ssm_parameter_get DB_PASSWORD /app/prod/db_password || exit_logic $?
aws_codedeploy_create_deployment DEPLOYMENT_ID app web CodeDeployDefault.OneAtATime releases app-1.4.0.zip || exit_logic $?
aws_codedeploy_wait_for_deployment "${DEPLOYMENT_ID}" || exit_logic $?
```

- **Every call goes through `aws_cli`**, which retries what can succeed on another try (network errors, throttling, a service that is briefly unavailable) and fails at once on what cannot (access denied, a missing resource, a bad argument). It logs the AWS CLI's error output, so a failed script says why it failed. Use it for any AWS CLI call of your own: `aws_cli RESPONSE_FILE "My step" ec2 describe-vpcs`.
- **Secrets stay secret.** Parameter values and KMS plaintext are never logged, and are handed to the AWS CLI and `openssl` through 0600 temporary files instead of the command line.
- **The Region is the last argument** of most functions. Leave it empty to use the AWS CLI's own setting.
- **Responses are read with `--query` and `--output text`**, not by searching JSON text with `grep`. `aws_route53_delete_record` goes further: its query turns the matching record sets into the change batch that deletes them, so alias, weighted, and multi-value records are deleted exactly as Route 53 holds them.

### Packages: `bamf.pkg.inc`

apt-get waits up to `PACKAGE_MANAGEMENT_LOCK_TIMEOUT` seconds (600) for the dpkg lock. Ubuntu runs `unattended-upgrades` while a machine starts, and it holds that lock; without the wait, the first `apt-get` in a boot script fails at once. Every command runs without prompts.

`package_management_update_all` exists, but think twice before calling it from a deployment: it upgrades what the deployment depends on (Docker, systemd) at the moment you need them most, and makes every deployment depend on the package mirrors.

### Docker: `bamf.docker.inc`

Docker Compose v2 (the `docker compose` plugin) is used when it is installed, otherwise the older `docker-compose`. `docker_compose_install` installs a pinned version and checks its SHA-256 before installing it; it never installs "latest", because a deployment should not change when someone else publishes a release.

### Web requests: `bamf.curl.inc`

`curl_request` retries network errors and the HTTP status codes that mean "try again" (408, 429, 5xx), and stops at once on the rest (a 404 will still be a 404 in ten seconds). `curl_wait_for_http_status` waits for a service to answer, which makes a good deployment health check.

## Function reference

Every function in the helper files, generated from the comments in the code (`ci/function_reference.sh -u` updates it).

<!-- BEGIN FUNCTION REFERENCE -->

<!-- Generated by ci/function_reference.sh from the FUNCTION comments in the code. Do not edit by hand. -->

### `bamf.aws.inc`

BAMF helper file for AWS: credentials, EC2 instance metadata, CloudFront, CodeDeploy, EC2, ENI, KMS, Route 53, S3 and SSM Parameter Store

| Function | What it does | Arguments |
|---|---|---|
| `aws_cli` | Runs an AWS CLI command, retrying failures that can succeed on another try<br>Retries network errors and AWS errors that mean "try again later" (throttling, service unavailable, internal errors). Errors that will not change, such as access denied, a missing resource or an invalid argument, fail at once. The AWS CLI also retries throttling itself.<br>On failure, logs the AWS CLI's error output, stores it in AWS_CLI_LAST_ERROR, and returns E_AWS_FAILURE | `$1` File to write the response (standard output) to (optional; empty discards it)<br>`$2` Description for the log (example: "SSM (Get Parameter)")<br>`$3+` AWS CLI arguments (example: ssm get-parameter --name /app/db) |
| `aws_cloudfront_invalidate_cache` | Invalidates paths in a CloudFront distribution's cache | `$1` CloudFront distribution ID<br>`$2` Path to invalidate, or several separated by ";" (example: /* or /index.html;/css/*)<br>`$3` Region (optional; CloudFront is global, so this is rarely needed) |
| `aws_codedeploy_create_bootstrap_deployment_group` | Creates a deployment group that targets EC2 instances tagged BootstrapCodeDeployDeploymentGroup=<name>, using the service role of an existing deployment group<br>A bootstrap group deploys to one new instance (tag it first) before it joins the real group | `$1` CodeDeploy application name<br>`$2` Existing deployment group name (its service role is reused)<br>`$3` Name for the bootstrap deployment group<br>`$4` Region (optional) |
| `aws_codedeploy_create_deployment` | Starts a CodeDeploy deployment of a revision stored in S3, and returns its deployment ID<br>The bundle type comes from the key's extension: .zip, .tgz or .tar.gz, .tar, .yaml or .yml, .json<br>Returns E_CODEDEPLOY_CREATE_DEPLOYMENT_FAILURE if the deployment cannot be created | `$1` Variable name to pass the deployment ID to<br>`$2` CodeDeploy application name<br>`$3` Deployment group name<br>`$4` Deployment configuration (example: CodeDeployDefault.OneAtATime)<br>`$5` S3 bucket<br>`$6` S3 key of the revision<br>`$7` Description (optional)<br>`$8` Region (optional) |
| `aws_codedeploy_delete_deployment_group` | Deletes a CodeDeploy deployment group | `$1` CodeDeploy application name<br>`$2` Deployment group name<br>`$3` Region (optional) |
| `aws_codedeploy_get_current_revision_json` | Writes the target revision of a deployment group (JSON) to a file<br>Returns E_AWS_RESOURCE_NOT_FOUND if the group has never been deployed | `$1` File to write the JSON to<br>`$2` CodeDeploy application name<br>`$3` Deployment group name<br>`$4` Region (optional) |
| `aws_codedeploy_get_deployed_revisions` | Writes the file names (S3 keys without their path) of an application's deployed revisions to a file, one per line | `$1` CodeDeploy application name<br>`$2` File to write the list to<br>`$3` Region (optional) |
| `aws_codedeploy_get_deployment` | Returns the status of a CodeDeploy deployment (Created, Queued, InProgress, Baking, Succeeded, Failed, Stopped or Ready), and logs its instance counts and any error | `$1` Variable name to pass the status to<br>`$2` Deployment ID<br>`$3` Region (optional) |
| `aws_codedeploy_get_deployment_revision_s3_bucket` | Returns the S3 bucket of a deployment group's target revision | `$1` Variable name to pass info to<br>`$2` CodeDeploy application name<br>`$3` Deployment group name<br>`$4` Region (optional) |
| `aws_codedeploy_get_deployment_revision_s3_key` | Returns the S3 key of a deployment group's target revision | `$1` Variable name to pass info to<br>`$2` CodeDeploy application name<br>`$3` Deployment group name<br>`$4` Region (optional) |
| `aws_codedeploy_get_service_role_arn` | Returns the service role ARN of a deployment group | `$1` Variable name to pass info to<br>`$2` CodeDeploy application name<br>`$3` Deployment group name<br>`$4` Region (optional) |
| `aws_codedeploy_register_revision` | Registers a revision stored in S3 with a CodeDeploy application<br>The bundle type comes from the key's extension, as in aws_codedeploy_create_deployment | `$1` CodeDeploy application name<br>`$2` S3 bucket<br>`$3` S3 key of the revision<br>`$4` Region (optional) |
| `aws_codedeploy_wait_for_deployment` | Checks a CodeDeploy deployment until it finishes, logging its progress each time<br>Returns 0 when it succeeds, E_CODEDEPLOY_DEPLOYMENT_FAILURE when it fails or is stopped, and E_OPERATION_TIMEOUT if it is still running after the last check | `$1` Deployment ID<br>`$2` Region (optional)<br>`$3` Number of checks (optional, defaults to AWS_CODEDEPLOY_WAIT_MAX_ATTEMPTS, 120)<br>`$4` Seconds between checks (optional, defaults to AWS_CODEDEPLOY_WAIT_SLEEP, 15) |
| `aws_credentials_select` | Chooses the AWS credentials for the rest of the script, and logs which ones are used<br>With a profile name: uses that profile, of any type (static keys, assumed role, IAM Identity Center, credential_process). Credentials exported in the environment are cleared, because the AWS CLI would otherwise use them instead of the profile.<br>Without one: leaves the AWS CLI's default credential chain unchanged (environment variables, AWS_PROFILE, web identity, container and instance roles)<br>Returns E_BAD_ARGS if the profile does not exist | `$1` Profile name (optional) |
| `aws_credentials_verify` | Checks that the AWS credentials work (sts get-caller-identity), and logs the account and identity | `$1` Variable name to pass the account ID to (optional) |
| `aws_ec2_get_availability_zone` | Returns the Availability Zone of an EC2 instance | `$1` Variable name to pass info to<br>`$2` Instance ID<br>`$3` Region (optional) |
| `aws_ec2_get_ipaddress_private` | Returns the private IPv4 address of an EC2 instance | `$1` Variable name to pass info to<br>`$2` Instance ID<br>`$3` Region (optional) |
| `aws_ec2_source_destination_check_disable` | Turns off the source/destination check of an EC2 instance, so it can route traffic for other addresses (a NAT instance, VPN gateway or router) | `$1` Instance ID<br>`$2` Region (optional) |
| `aws_ec2_tag_resource` | Adds or replaces a tag on an EC2 resource (instance, volume, network interface, ...) | `$1` Resource ID<br>`$2` Tag key<br>`$3` Tag value<br>`$4` Region (optional) |
| `aws_ec2_tag_resource_from_file` | Adds or replaces tags on an EC2 resource from a JSON file: [{"Key": "Name", "Value": "web"}, ...] | `$1` Resource ID<br>`$2` Tags file (JSON)<br>`$3` Region (optional) |
| `aws_ec2_vpc_route_update` | Points a route table's default route (0.0.0.0/0) at an EC2 instance, replacing the route if it exists and creating it if not (a NAT instance taking over a subnet's internet traffic) | `$1` Route table ID<br>`$2` Instance ID<br>`$3` Region (optional) |
| `aws_eni_attach` | Attaches an elastic network interface (ENI) to an EC2 instance, detaching it from any other instance first (a floating ENI that moves to whichever instance is active) | `$1` Network interface ID<br>`$2` Instance ID<br>`$3` Device index (optional, defaults to 3)<br>`$4` Region (optional) |
| `aws_eni_detach` | Detaches an elastic network interface (forced) | `$1` Attachment ID<br>`$2` Region (optional) |
| `aws_eni_return_attachment_id` | Returns the attachment ID of an elastic network interface (empty when it is not attached) | `$1` Variable name to pass info to<br>`$2` Network interface ID<br>`$3` Region (optional) |
| `aws_eni_return_status` | Returns the status of an elastic network interface (available, in-use, attaching, detaching, ...) | `$1` Variable name to pass info to<br>`$2` Network interface ID<br>`$3` Region (optional) |
| `aws_kms_decrypt_file` | Decrypts a file made by aws_kms_encrypt_file into a directory, under its original file name<br>Also reads files from earlier BAMF versions (format 1) | `$1` Encrypted file<br>`$2` Output directory<br>`$3` Region (optional) |
| `aws_kms_decrypt_string` | Decrypts a string encrypted with KMS (base64 ciphertext, as aws_kms_encrypt_string returns)<br>The decrypted value is never logged | `$1` Variable name to pass the decrypted string to<br>`$2` Encrypted string (base64)<br>`$3` Not used (earlier versions masked the value in the log on request; it is now never logged)<br>`$4` Region (optional) |
| `aws_kms_encrypt_file` | Encrypts a file with a KMS key (envelope encryption) into <output directory>/<file name>.enc<br>KMS creates a one-time data key; the file is encrypted with it locally (openssl, AES-256-CBC, PBKDF2), and only the KMS-encrypted copy of the data key is stored with it. KMS is called once, however large the file. Decrypt with aws_kms_decrypt_file.<br>The data key reaches openssl through a 0600 file, never the command line<br>Requires OpenSSL 1.1.1 or newer (for PBKDF2) | `$1` File to encrypt<br>`$2` Output directory<br>`$3` KMS key (ID, ARN or alias, example: alias/backups)<br>`$4` Region (optional) |
| `aws_kms_encrypt_string` | Encrypts a string with a KMS key (up to 4 KB), and returns the ciphertext as base64<br>The plaintext is passed to the AWS CLI through a 0600 file, never the command line, and never logged | `$1` Variable name to pass the encrypted string to<br>`$2` String to encrypt<br>`$3` KMS key (ID, ARN or alias)<br>`$4` Region (optional) |
| `aws_kms_generate_envelope_key` | Creates a 256-bit data key with a KMS key, and returns only its encrypted form (base64); decrypt it with KMS when it is needed | `$1` Variable name to pass the encrypted data key to<br>`$2` KMS key (ID, ARN or alias)<br>`$3` Region (optional) |
| `aws_metadata_account_id` | Returns the AWS account ID of this EC2 instance (instance metadata)<br>Returns E_AWS_FAILURE if instance metadata cannot be reached (not on EC2) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_account_id_from_cli` | Returns the AWS account ID of the current credentials (sts get-caller-identity) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_auto_scaling_group_name` | Returns the name of the Auto Scaling group this EC2 instance belongs to (empty if none)<br>Needs ec2:DescribeTags | `$1` Variable name to pass info to (optional, if not specified the value is printed)<br>`$2` Region (optional, defaults to this instance's Region)<br>`$3` Instance ID (optional, defaults to this instance) |
| `aws_metadata_get_tag` | Returns the value of a tag on an EC2 instance (empty if the tag is not set)<br>Needs ec2:DescribeTags | `$1` Variable name to pass info to (optional, if not specified the value is printed)<br>`$2` Tag key<br>`$3` Region (optional, defaults to this instance's Region)<br>`$4` Instance ID (optional, defaults to this instance) |
| `aws_metadata_hostname` | Returns this EC2 instance's host name: the public one if it has one, otherwise the private one | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_hostname_private` | Returns this EC2 instance's private host name (instance metadata) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_hostname_public` | Returns this EC2 instance's public host name (instance metadata); empty, with E_OBJECT_NOT_FOUND, if it has none | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_instance_id` | Returns this EC2 instance's ID (instance metadata) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_ipaddress_private` | Returns this EC2 instance's private IPv4 address (instance metadata) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_ipaddress_public` | Returns this EC2 instance's public IPv4 address (instance metadata); empty, with E_OBJECT_NOT_FOUND, if it has none | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_mac` | Returns the MAC address of this EC2 instance's primary network interface (instance metadata) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_region` | Returns this EC2 instance's Region (instance metadata) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_route_id_private` | Returns the IDs of the route tables in a VPC tagged Network=private (space separated)<br>Needs ec2:DescribeRouteTables. The Network tag is a naming convention: tag route tables Network=private and Network=public to use this and aws_metadata_route_id_public | `$1` Variable name to pass info to (optional, if not specified the value is printed)<br>`$2` Region (optional, defaults to this instance's Region)<br>`$3` VPC ID (optional, defaults to this instance's VPC) |
| `aws_metadata_route_id_public` | Returns the IDs of the route tables in a VPC tagged Network=public (space separated)<br>Needs ec2:DescribeRouteTables; see aws_metadata_route_id_private | `$1` Variable name to pass info to (optional, if not specified the value is printed)<br>`$2` Region (optional, defaults to this instance's Region)<br>`$3` VPC ID (optional, defaults to this instance's VPC) |
| `aws_metadata_token` | Gets an instance metadata (IMDSv2) session token into AWS_METADATA_TOKEN<br>The token is reused until it is five minutes from expiring (tokens last six hours)<br>Returns E_AWS_FAILURE if instance metadata cannot be reached (not on EC2) | none |
| `aws_metadata_vpc_cidr` | Returns the IPv4 CIDR block of this EC2 instance's VPC (instance metadata) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_vpc_dns` | Returns the IP address of this EC2 instance's VPC DNS server (the VPC's base address plus two) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_metadata_vpc_id` | Returns the ID of this EC2 instance's VPC (instance metadata) | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `aws_route53_delete_record` | Deletes the record sets with a name (all types, or one type) from a Route 53 hosted zone<br>Deletes them exactly as Route 53 holds them, so alias, weighted and multi-value records work too<br>Succeeds without a change when there is nothing to delete | `$1` Hosted zone ID<br>`$2` Record name (example: www.example.com)<br>`$3` Region (optional; Route 53 is global, so this is rarely needed)<br>`$4` Record type (optional, example: A; empty deletes every type) |
| `aws_route53_get_record` | Loads the record sets with a name from a Route 53 hosted zone into an array, one element per record set: NAME\|TYPE\|TTL\|VALUES\|WEIGHT\|SET_IDENTIFIER, with multiple values separated by ";" (an alias record shows ALIAS:<target> as its value) | `$1` Array name to pass info to<br>`$2` Hosted zone ID<br>`$3` Record name (example: www.example.com)<br>`$4` Region (optional; Route 53 is global, so this is rarely needed) |
| `aws_route53_upsert_record` | Creates or replaces a record set in a Route 53 hosted zone<br>Values are written as given: a TXT value needs its own double quotes ("v=spf1 -all"), which are escaped for JSON here | `$1` Hosted zone ID<br>`$2` Record name (example: www.example.com)<br>`$3` Record type (example: A, CNAME, TXT)<br>`$4` TTL in seconds (optional, defaults to AWS_ROUTE53_DEFAULT_TTL, 600)<br>`$5` Values (separated by ";", example: 192.0.2.10;192.0.2.11)<br>`$6` Weight (optional, for weighted records)<br>`$7` Set identifier (optional, required with a weight)<br>`$8` Region (optional; Route 53 is global, so this is rarely needed) |
| `aws_s3_cp_download` | Downloads an object from S3 to a local file | `$1` S3 bucket<br>`$2` S3 key<br>`$3` Local file<br>`$4` Region (optional) |
| `aws_s3_cp_upload` | Uploads a local file to S3 | `$1` S3 bucket<br>`$2` S3 key<br>`$3` Local file<br>`$4` Server-side encryption, AES256 or aws:kms (optional; empty uses the bucket's default encryption; yes means AES256, as in earlier versions)<br>`$5` Storage class (optional, example: STANDARD_IA; empty uses STANDARD)<br>`$6` Region (optional) |
| `aws_s3_delete` | Deletes an object from S3 | `$1` S3 bucket<br>`$2` S3 key<br>`$3` Region (optional) |
| `aws_s3_move_in_bucket` | Moves (renames) an object within an S3 bucket | `$1` S3 bucket<br>`$2` Source key<br>`$3` Target key<br>`$4` Server-side encryption, AES256 or aws:kms (optional; empty uses the bucket's default encryption; yes means AES256, as in earlier versions)<br>`$5` Storage class (optional; empty uses STANDARD)<br>`$6` Region (optional) |
| `aws_s3_sync_download` | Copies new and changed files from an S3 prefix to a local directory (aws s3 sync) | `$1` S3 bucket<br>`$2` S3 prefix (directory)<br>`$3` Local directory<br>`$4` Delete local files that are not in S3, yes or no (optional, defaults to no)<br>`$5` Exact timestamps, yes or no: also copy same-size files whose times differ (optional, defaults to no)<br>`$6` Exclude patterns, separated by ";" (optional, example: *.tmp;logs/*)<br>`$7` Include patterns, separated by ";" (optional, applied after the excludes)<br>`$8` Region (optional) |
| `aws_s3_sync_upload` | Copies new and changed files from a local directory to an S3 prefix (aws s3 sync) | `$1` S3 bucket<br>`$2` S3 prefix (directory)<br>`$3` Local directory<br>`$4` Delete S3 objects that are not in the local directory, yes or no (optional, defaults to no)<br>`$5` Exact timestamps, yes or no (optional, defaults to no)<br>`$6` Server-side encryption, AES256 or aws:kms (optional; empty uses the bucket's default encryption)<br>`$7` Storage class (optional; empty uses STANDARD)<br>`$8` Exclude patterns, separated by ";" (optional)<br>`$9` Include patterns, separated by ";" (optional, applied after the excludes)<br>`$10` Region (optional) |
| `aws_s3_verify` | Checks that an S3 bucket (or a prefix in it) can be listed with the current credentials | `$1` S3 bucket<br>`$2` S3 prefix (optional, defaults to the top of the bucket)<br>`$3` Log the listing, yes or no (optional, defaults to no)<br>`$4` Region (optional) |
| `aws_ssm_parameter_delete` | Deletes an SSM parameter; a parameter that does not exist is not an error | `$1` Parameter name<br>`$2` Skip the check that it exists, yes or no (optional, defaults to no; yes saves a call when you know it exists, and then a missing parameter is an error)<br>`$3` Region (optional) |
| `aws_ssm_parameter_exists` | Returns true if an SSM parameter exists (E_AWS_RESOURCE_NOT_FOUND if it does not; E_AWS_FAILURE if AWS could not be asked, for example without permission) | `$1` Parameter name<br>`$2` Region (optional) |
| `aws_ssm_parameter_get` | Returns the value of an SSM parameter; a SecureString is decrypted unless told otherwise<br>The value is never logged | `$1` Variable name to pass the value to<br>`$2` Parameter name<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |
| `aws_ssm_parameter_get_file` | Writes a file stored in an SSM parameter (by aws_ssm_parameter_put_file) back to disk, mode 0400 | `$1` File to write<br>`$2` Parameter name<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |
| `aws_ssm_parameter_get_file_multi_part` | Writes a file stored in several SSM parameters (by aws_ssm_parameter_put_file_multi_part) back to disk, mode 0400 | `$1` File to write<br>`$2` Parameter path holding the parts<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |
| `aws_ssm_parameter_get_file_silent` | Same as aws_ssm_parameter_get_file, but logs only if it fails | `$1` File to write<br>`$2` Parameter name<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |
| `aws_ssm_parameter_get_silent` | Same as aws_ssm_parameter_get, but logs only if it fails | `$1` Variable name to pass the value to<br>`$2` Parameter name<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |
| `aws_ssm_parameter_path_delete` | Deletes every SSM parameter under a path (recursively) | `$1` Parameter path (example: /app/staging)<br>`$2` Region (optional) |
| `aws_ssm_parameter_put` | Creates or updates an SSM SecureString parameter<br>The value is passed to the AWS CLI through a 0600 file, never the command line, and never logged | `$1` Parameter name<br>`$2` Value<br>`$3` KMS key (optional, defaults to the account's aws/ssm key)<br>`$4` Description (optional)<br>`$5` Overwrite an existing parameter, yes or no (optional, defaults to yes)<br>`$6` Region (optional) |
| `aws_ssm_parameter_put_file` | Stores a file in an SSM SecureString parameter (base64 encoded); up to about 3 KB with the Standard tier, use aws_ssm_parameter_put_file_multi_part for larger files | `$1` Parameter name<br>`$2` File<br>`$3` KMS key (optional, defaults to the account's aws/ssm key)<br>`$4` Description (optional)<br>`$5` Overwrite an existing parameter, yes or no (optional, defaults to yes)<br>`$6` Region (optional) |
| `aws_ssm_parameter_put_file_multi_part` | Stores a file of any size in SSM SecureString parameters <path>/part_0001, part_0002, ... (base64 encoded, AWS_SSM_MULTI_PART_SIZE characters each)<br>With replace (the default), parameters already under the path are deleted first, so no stale parts are left behind; without it, existing parameters under the path are an error | `$1` Parameter path<br>`$2` File<br>`$3` KMS key (optional, defaults to the account's aws/ssm key)<br>`$4` Description (optional)<br>`$5` Replace what is under the path, yes or no (optional, defaults to yes)<br>`$6` Region (optional) |
| `aws_ssm_parameter_tag` | Adds or replaces tags on an SSM parameter | `$1` Parameter name<br>`$2` Tags, as Key:Value pairs separated by ";" (example: team:web;environment:staging); the value is everything after the first ":". AWS accepts letters, numbers, spaces and _ . : / = + - @<br>`$3` Region (optional) |
| `aws_ssm_parameter_tag_path` | Adds or replaces tags on every SSM parameter under a path (recursively) | `$1` Parameter path<br>`$2` Tags, as Key:Value pairs separated by ";"<br>`$3` Region (optional) |
| `aws_ssm_parameters_to_key_value_file` | Writes the parameters under a path to a file as [name]=value lines, named by the last part of each parameter name (read them with load_key_value_from_file); the file is created with mode 0600<br>Parameters under a "files" path element (files stored by the put_file functions) are skipped. Values must be single lines. | `$1` Parameter path<br>`$2` Output file<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |
| `aws_ssm_parameters_to_key_value_file_multipart` | Same as aws_ssm_parameters_to_key_value_file, but keeps the parameters under a "files" path element | `$1` Parameter path<br>`$2` Output file<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |
| `aws_ssm_parameters_to_properties_file` | Writes the parameters under a path to a file as name=value lines, named by the last part of each parameter name (read them with load_property_from_file); the file is created with mode 0600<br>Parameters under a "files" path element are skipped. Values must be single lines. | `$1` Parameter path<br>`$2` Output file<br>`$3` Decrypt, yes or no (optional, defaults to yes)<br>`$4` Region (optional) |

### `bamf.curl.inc`

BAMF helper file for curl: HTTP requests with retries, and waiting for a service to answer

| Function | What it does | Arguments |
|---|---|---|
| `curl_interpret_return` | Logs what a curl exit code means, and whether trying again could help<br>Network problems (cannot resolve, cannot connect, timeouts, interrupted transfers) can be retried; errors in the request itself (bad URL, unknown option, local file problems) cannot | `$1` curl exit code<br>`$2` Variable name to pass yes or no to: is a retry worthwhile (optional) |
| `curl_request` | Makes an HTTP request (GET, or POST when data is given) and saves the response body to a file, retrying network errors and HTTP status codes 408, 429 and 5xx<br>Succeeds for HTTP status codes 200-299; otherwise logs the response body and returns E_CURL_FAILURE. The status code of the last attempt is left in CURL_LAST_HTTP_CODE. | `$1` URL<br>`$2` Output file (optional, a temporary file if empty)<br>`$3` Log the response body, yes or no (optional, defaults to no)<br>`$4` Data to POST (optional)<br>`$5` User name (optional)<br>`$6` Password (optional)<br>`$7` Client certificate file, PEM (optional)<br>`$8` Resume a partial download into the output file, yes or no (optional, defaults to yes)<br>`$9` Rate limit (optional, example: 1m; defaults to CURL_DEFAULT_LIMIT_RATE, unlimited)<br>`$10` Seconds below 1 byte per second before giving up (optional, defaults to CURL_DEFAULT_SPEED_TIMEOUT)<br>`$11` Maximum seconds for the whole transfer (optional, defaults to CURL_DEFAULT_MAX_TIMEOUT) |
| `curl_wait_for_http_status` | Requests a URL until it answers with the expected HTTP status code, for example to wait for a service to become healthy after it starts<br>Returns 0 when it does, E_OPERATION_TIMEOUT if it never does | `$1` URL<br>`$2` Expected HTTP status code (optional, defaults to 200)<br>`$3` Number of attempts (optional, defaults to 12)<br>`$4` Seconds between attempts (optional, defaults to 10) |

### `bamf.docker.inc`

BAMF helper file for Docker: install Docker and Docker Compose, run Compose projects, log in to Amazon ECR, and clean up

| Function | What it does | Arguments |
|---|---|---|
| `docker_compose_down` | Stops and removes the containers of a Docker Compose project | `$1` Compose file<br>`$2` Environment file (optional) |
| `docker_compose_install` | Installs a pinned version of the Docker Compose v2 plugin from its GitHub release, for the machine's architecture, after checking the download against its SHA-256 checksum<br>Pass the expected checksum to check against a value you trust; without it, the checksum file published with the release is used (which catches a damaged download, not a tampered release)<br>Does nothing if "docker compose" already works | `$1` Version (example: v2.29.7)<br>`$2` Expected SHA-256 of the binary (optional) |
| `docker_compose_up` | Starts the containers of a Docker Compose project in the background (up -d)<br>Logs the Compose file and the output; the environment file is not logged, as it often holds secrets | `$1` Compose file<br>`$2` Environment file (optional) |
| `docker_ecr_login` | Logs Docker in to an Amazon ECR registry with the current AWS credentials<br>The password goes from the AWS CLI to docker through a pipe (--password-stdin), never the command line | `$1` Registry (example: 123456789012.dkr.ecr.us-east-1.amazonaws.com)<br>`$2` Region (optional, defaults to the Region in the registry name) |
| `docker_install` | Installs Docker with the system package manager (if it is not installed), and starts it and enables it at boot with systemctl<br>The default package is docker.io on apt-get systems (Debian, Ubuntu) and docker on dnf and yum systems (Amazon Linux). On other distributions, pass the package names, after adding Docker's own package repository. | `$1+` Package names (optional) |
| `docker_prune` | Removes every stopped container, unused network, unused image and unused volume<br>Volumes hold data: this deletes the data of every volume no container is using | none |
| `docker_reset` | Stops and removes every container on the machine (not only one project's) | `$1` Seconds each container gets to stop before it is killed (optional, Docker's default is 10) |
| `docker_reset_images` | Removes every image that no container uses | none |

### `bamf.git.inc`

BAMF helper file for Git and GitHub: tag and push a commit, check a clone is up to date, list an organization's repositories

| Function | What it does | Arguments |
|---|---|---|
| `git_github_repository_list` | Writes the full names (owner/name) of every repository in a GitHub organization to a file, one per line, sorted<br>The token is read from an environment variable and sent to curl in a 0600 header file, never on the command line | `$1` Organization name<br>`$2` Output file<br>`$3` Name of the environment variable holding the GitHub token (optional, defaults to GITHUB_TOKEN) |
| `git_is_repo_up_to_date` | Fetches from the remote and returns true if the current branch matches its upstream branch<br>Returns 1 if it does not, E_GIT_FAILURE if it cannot tell (no upstream, fetch failed) | `$1` Repository directory (optional, defaults to the current directory) |
| `git_tag_and_push` | Creates an annotated tag on a commit and pushes it to a remote<br>An existing tag is not moved unless asked: moving a published tag changes what the version means for everyone who has already fetched it | `$1` Repository directory<br>`$2` Tag (example: v1.2.3)<br>`$3` Commit (optional, defaults to HEAD)<br>`$4` Message (optional, defaults to the tag)<br>`$5` Remote (optional, defaults to origin)<br>`$6` Move the tag if it exists, yes or no (optional, defaults to no)<br>`$7` Tagger name (optional, defaults to Git's configuration)<br>`$8` Tagger email (optional, defaults to Git's configuration) |

### `bamf.npm.inc`

BAMF helper file for npm: read package.json, check a registry for a version, write .npmrc

| Function | What it does | Arguments |
|---|---|---|
| `npm_does_version_exist` | Returns true if a version of a package is published in the registry (npm view), and E_NPM_PACKAGE_DOES_NOT_EXIST if it is not, for example to stop a release that would publish a version twice | `$1` Package name<br>`$2` Version<br>`$3` Directory to run npm in, for its .npmrc (optional, defaults to the current directory) |
| `npm_generate_npmrc` | Writes an .npmrc file that points npm at a registry<br>With a token variable name, adds the registry's auth token as a reference, ${NAME}, which npm reads from the environment when it runs, so the token itself is never written to the file | `$1` Registry URL (example: https://npm.pkg.github.com/)<br>`$2` Directory to write the file in<br>`$3` File name (optional, defaults to .npmrc)<br>`$4` Name of the environment variable holding the auth token (optional, example: NPM_TOKEN) |
| `npm_package_value` | Returns a field from a package.json (npm pkg get), such as name or version; nested fields use dots (example: repository.url). A list or object is returned as JSON. | `$1` Variable name to pass info to (optional, if not specified the value is printed)<br>`$2` Field<br>`$3` Directory holding package.json (optional, defaults to the current directory) |

### `bamf.pkg.inc`

BAMF helper file for package management: install and update packages with apt-get, dnf or yum

| Function | What it does | Arguments |
|---|---|---|
| `package_management_detect` | Returns the package manager to use: PACKAGE_MANAGER if set, otherwise the first of apt-get, dnf and yum that is installed<br>Returns E_PACKAGE_MANAGEMENT_FAILURE if there is none | `$1` Variable name to pass info to (optional, if not specified the value is printed) |
| `package_management_install_packages` | Installs packages (refreshing the package lists first with apt-get) | `$1+` Package names, or one array passed by name (pass this way: "PACKAGES[@]") |
| `package_management_is_installed` | Returns true if a package is installed (dpkg on apt-get systems, rpm on dnf and yum systems) | `$1` Package name |
| `package_management_update_all` | Upgrades every installed package (apt-get update and upgrade, or dnf/yum update)<br>Changed configuration files keep the local version (apt-get), so an upgrade never stops to ask<br>Think twice before calling this from a deployment: upgrading the machine under an application also upgrades what the deployment relies on (docker, systemd, ...), and makes every deployment depend on the package mirrors. Patching belongs to whoever builds the machine image. | none |

### `bamf.tar.inc`

BAMF helper file for tar: create and extract archives, compressed or not

| Function | What it does | Arguments |
|---|---|---|
| `tar_create` | Creates an archive of a directory or a file<br>Relative (the default): a directory's contents are stored at the top of the archive, and a file is stored under its own name, so the archive can be extracted anywhere<br>Absolute: entries keep their full path (without the leading /), to be restored to the same place with tar_extract <archive> /<br>A failed archive is removed, so a partial file is never left behind | `$1` Archive to create (the extension picks the compression)<br>`$2` Directory or file to archive<br>`$3` Paths, relative or absolute (optional, defaults to relative) |
| `tar_extract` | Extracts an archive into a directory (created if it does not exist)<br>GNU tar refuses entries that would land outside the directory (absolute paths and "..") | `$1` Archive<br>`$2` Directory to extract into (use / to restore an archive made with absolute paths) |

### `bamf.terraform.inc`

BAMF helper file for Terraform: read the outputs of a workspace kept in an S3 backend

| Function | What it does | Arguments |
|---|---|---|
| `terraform_output_value` | Returns a value from a file of Terraform outputs (JSON, as terraform_s3_remote_workspace_to_file writes it); a list or map is returned as JSON. Empty if the output does not exist. | `$1` Outputs file (JSON)<br>`$2` Output name; use dots for nested values (example: vpc.id)<br>`$3` Variable name to pass info to (optional, if not specified the value is printed) |
| `terraform_s3_remote_workspace_to_file` | Writes the outputs of a Terraform workspace whose state is in an S3 backend to a file (JSON)<br>Works in a temporary directory with a generated configuration (one terraform_remote_state data source), so it needs no access to the configuration that owns the state; the AWS credentials need read access to the state object | `$1` Output file<br>`$2` Workspace (default for the default workspace)<br>`$3` S3 bucket<br>`$4` State key (optional, defaults to terraform.tfstate)<br>`$5` Region of the bucket |

<!-- END FUNCTION REFERENCE -->
