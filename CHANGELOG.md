# Changelog

All notable changes to BAMF are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and BAMF uses [semantic versioning](https://semver.org/): a new major version means scripts that use BAMF must change their code.

## [Unreleased]

### Added

- The BAMF logo, at the top of the README, and its brand kit in `brand_kit/`. The logos and the kit are not covered by the Apache License 2.0; see `brand_kit/NOTICE.md`.

## [1.0.0] - 2026-10-22

Initial release.

### Added

- `bamf.inc`, the core library: logging, named exit codes, start and exit handling with signal traps, temporary files that are always removed, dependency checks, and helpers for strings, arrays, base64, files, and configuration.
- `define_exit_code`, so scripts can name their own exit codes in the 201–250 range.
- Helper files (`helpers/bamf.<name>.inc`) loaded by name through `REQUIRED_SOURCE_FILES`, and `add_exit_function`, to run a script's own cleanup however it exits.
- `json_escape`, to build JSON safely from any value, and `run_silently`, to hide a command's output unless it fails.
- `helpers/bamf.aws.inc`: AWS CLI calls with retries (`aws_cli`), credential selection, EC2 instance metadata (IMDSv2), CloudFront, CodeDeploy (including `aws_codedeploy_wait_for_deployment`), EC2, ENI, KMS envelope encryption of files, Route 53, S3, and SSM Parameter Store.
- `bamf.curl.inc`, `bamf.docker.inc`, `bamf.git.inc`, `bamf.npm.inc`, `bamf.pkg.inc`, `bamf.tar.inc` and `bamf.terraform.inc` helper files.
- Tests (bats-core) that run on bash 4.2 and 5.2, ShellCheck linting, and a CI workflow.
- Examples: `hello.sh`, `config_check.sh`, `pipeline.sh`, `cleanup_and_signals.sh`, `download_verify.sh`, `backup_to_s3.sh`, and a set of AWS CodeDeploy hooks in `examples/codedeploy/`.

[Unreleased]: https://github.com/AutomateTheCloud/bamf/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/AutomateTheCloud/bamf/releases/tag/v1.0.0
