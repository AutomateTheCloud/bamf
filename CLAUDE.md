# BAMF: working rules

BAMF (BASH Application Management Framework) is a bash library: scripts source `bamf.inc` and get
logging, named exit codes, start and exit handling, temporary files, and everyday helpers. It is
David Singer's work of many years, published by Automate the Cloud Inc. as teaching
material as well as a tool: people read it to learn bash practices. Correctness, safety, and clear
explanations matter more than features.

## Ground rules

- Do not commit, push, tag, or change repository settings unless asked.
- Commit messages and pull requests describe the change only. Never mention an AI tool or add
  attribution trailers for one. Sign off every commit with `git commit -s`.
- Say what you verified and how. "Tests pass" means you ran them; otherwise say it is untested.
- `temp/` holds working copies of earlier versions (reference only). It is in `.gitignore` and must
  never be committed: it contains internal details from past deployments.

## Layout

- `bamf.inc`: the core, and the heart of the project. Only what any script could use.
- `helpers/bamf.<name>.inc`: helper files for one tool or service (AWS, Docker, ...), loaded by name
  through `REQUIRED_SOURCE_FILES` (looked up in `helpers/`, then beside `bamf.inc`). Anything
  tool-specific goes here, never in the core. `helpers/README.md` documents them.
- `examples/`: complete scripts that run as they are and teach one thing each.
- `brand_kit/`: the BAMF logo, icons, colors, and usage rules. Not under the Apache License 2.0
  (`brand_kit/NOTICE.md`); change the artwork only with David.
- `tests/`: bats-core tests; `tests/fixtures/script.sh` exercises the script lifecycle.
- `ci/`: `install_test_dependencies.sh` (pinned, checksum-verified ShellCheck and bats-core),
  `lint.sh`, `test.sh`, `function_reference.sh` (generates the references in `README.md` for
  `bamf.inc` and in `helpers/README.md` for the helper files).
- `tests/live/aws.sh`: checks the AWS helper against a real account (maintainers only; it creates
  and removes real resources, and refuses to run in any account but the one given with -a).

## Code rules

- Every function has a `## FUNCTION: name()` comment block (what it does, what it returns, each
  argument) and is listed in the function list at the top of its file. `tests/library.bats`
  checks both; the README reference is generated from them (`ci/function_reference.sh -u`).
- Values are returned through a variable name with `_bamf_return` (`printf -v`), never `eval`.
  A function that takes a result variable name uses locals with two leading underscores, unique to
  that function when it passes a result name on to another function (`__LA_VALUE`).
- Write for `set -euo pipefail` and bash 4.2: `${1:-}` for optional arguments,
  `"${ARRAY[@]+"${ARRAY[@]}"}"` for arrays that may be empty, and no `declare -gA NAME=()` inside
  functions (declare, then assign).
- `printf`, never `echo`; `command -v`, never `which`; quote every expansion.
- Silence a ShellCheck warning on the line it is about, with the reason
  (`# shellcheck disable=SC2034 # read by name, through option_enabled`), never for a whole library
  file: a file-wide disable also hides the next real mistake.
- Helper functions start with the helper's name (`aws_`, `docker_`, ...; `package_management_` for
  `bamf.pkg.inc`), return exit codes instead of ending the script, and never log or put secrets on
  a command line.
- Helper files declare variables with `declare -g` (they are sourced inside `source_include`), take
  exit codes from 81-120 or 151-200 with `define_exit_code`, and do nothing at source time beyond
  declarations.
- Exit code ranges: 0-80 and 251-255 core, 81-120 and 151-200 core and helpers, 121-150 bash and
  signals, 201-250 for users' scripts.

## Testing

```shell
ci/install_test_dependencies.sh
ci/lint.sh
ci/test.sh
ci/test.sh centos:7 debian:12-slim amazonlinux:2023 ubuntu:24.04   # needs Docker
ci/function_reference.sh
ci/readme_images.sh        # the README's terminal images (docs/images), made from real output
```

CentOS 7 is the bash 4.2 floor. Every bug fix gets a test that fails without the fix
(`tests/regressions.bats` for bugs from earlier versions).

## Licensing

Code only, under the Apache License 2.0: `LICENSE` (official text, byte for byte), `NOTICE`, and
this two-line header in every source file:

```bash
# Copyright 2026 Automate the Cloud Inc.
# SPDX-License-Identifier: Apache-2.0
```

In a file with a `# script:` / `# purpose:` / `# version:` header block (`bamf.inc`, the helpers,
the example scripts), the two lines go inside the block, below `# version:`, separated by `#` lines;
in other files, at the top (after the `#!` line). Keep the exact wording: it is what the licensing
playbook prescribes and what license scanners look for. `tests/library.bats` checks every file.
Files carry no `# shellcheck shell=bash` line: `.shellcheckrc` sets the shell for the repository.

The full policy is the organization's licensing playbook (`LICENSING-PLAYBOOK.md` in the
`atc-brand_kit` GitLab repository); fetch a fresh copy before any licensing change. David
confirmed on 2026-10-22 that all earlier BAMF code is his own.

## Writing

Plain and specific. Say what a thing does before why. Short sentences, no marketing words, expand
an acronym the first time it appears. Explain the reasoning behind a bash technique where the code
uses one: that is what makes BAMF worth reading. "Automate the Cloud" in prose, "Automate the
Cloud Inc." in legal text. American English everywhere.
