# Contributing

Contributions are welcome. Every change is reviewed closely before it is merged, because people run BAMF scripts on real servers and in real pipelines, and because the code is meant to teach. Please expect questions and requests for changes.

## Before you start

Open an issue first and describe what you want to change and why. Agreeing on the approach before you write code saves you time. Small fixes, such as typos in the documentation, can go straight to a pull request.

To report a security problem, do not open an issue. Follow [SECURITY.md](SECURITY.md) instead.

## Making a change

You need bash 4.2 or later, GNU coreutils, and `curl`. Docker is optional; with it, you can run the tests on the oldest supported bash.

1. Fork the repository and create a branch.
2. Make the change. Keep to one change per pull request.
3. Add or update a test in `tests/` that shows the change works. A bug fix gets a test that fails without the fix.
4. Run the checks:

   ```shell
   ci/install_test_dependencies.sh       # pinned ShellCheck and bats-core, checked by SHA-256
   ci/lint.sh
   ci/test.sh
   ci/test.sh centos:7                   # bash 4.2, with Docker
   ci/function_reference.sh -u           # if you added or changed a function (README.md, helpers/README.md)
   ci/readme_images.sh -u                # if you changed what the log looks like (docs/images)
   ```

5. Describe the change in `CHANGELOG.md` under "Unreleased".
6. Commit with `git commit -s` (see [Sign your commits](#sign-your-commits)), open a pull request, and fill in the checklist.

CI runs the same checks on bash 4.2 and bash 5.2. A pull request is merged only when they pass and a maintainer has approved it.

## Sign your commits

Every commit in a pull request must be signed off. Signing off means you agree to the [Developer Certificate of Origin](https://developercertificate.org/) (DCO): a short statement that you wrote the change, or otherwise have the right to submit it under this project's license. Code written for an employer may belong to the employer, so check before contributing it.

Sign off by committing with `-s`, which adds a line with the name and email from your Git settings:

```shell
git commit -s -m "Add a function to read INI files"
```

```
Signed-off-by: Jane Developer <jane@example.com>
```

The email in the sign-off must match the commit's author email. A check on each pull request lists any commit that is not signed off. To sign off commits you have already made, run `git rebase --signoff main` and force-push the branch.

## Guidelines

- **Does it belong in `bamf.inc`?** The core holds what any script could use. Anything tied to one tool or service (AWS, Docker, a package manager) goes in a helper file, `helpers/bamf.<name>.inc`, with functions named after the helper (`aws_`, `docker_`).
- **Document every function** with a comment block in the same form as the others (`## FUNCTION: name()`, what it does, what it returns, and each argument), and add it to the function list at the top of the file. The README reference is generated from these comments, and a test checks the list.
- **Return values through a variable name**, with `_bamf_return`, never with `eval`. Name a function's local variables with two leading underscores when the function takes a result variable name, so they cannot hide the caller's variable.
- **Write for `set -euo pipefail`**: read optional arguments as `${1:-}`, and expand arrays that may be empty as `"${ARRAY[@]+"${ARRAY[@]}"}"`.
- **Print with `printf`, not `echo`.** Find commands with `command -v`, not `which`. Quote every expansion.
- **Helper files declare their variables with `declare -g`**, because `start_logic` sources them from inside a function.
- **Do not break existing scripts.** A change to what a function takes or returns needs a strong reason and a new major version.
- **New files carry the two-line license header**, `# Copyright 2026 Automate the Cloud Inc.` and `# SPDX-License-Identifier: Apache-2.0`: inside the `# script:` / `# purpose:` / `# version:` header block, below `# version:`, in files that have one (as `bamf.inc` does); otherwise at the top, after the `#!` line if there is one.
- Write in plain, American English, in the same style as the existing documentation.

## License

BAMF is licensed under the [Apache License 2.0](LICENSE). Under section 5 of that license, any contribution you submit for inclusion is licensed under the same terms. Your sign-off confirms you have the right to submit it.
