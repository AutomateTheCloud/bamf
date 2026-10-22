## What this changes

<!-- What does this pull request do, and why? Link the issue it addresses. -->

## Checklist

- [ ] An issue describing the change was discussed first.
- [ ] Every commit is signed off (`git commit -s`), agreeing to the [Developer Certificate of Origin](https://developercertificate.org/).
- [ ] `ci/lint.sh` reports no findings.
- [ ] `ci/test.sh` passes, and so does `ci/test.sh centos:7` (bash 4.2) if you have Docker.
- [ ] New or changed behavior has a test in `tests/`.
- [ ] New or changed functions are documented, listed at the top of their file, and `ci/function_reference.sh -u` was run.
- [ ] `CHANGELOG.md` has an entry under "Unreleased".
- [ ] New files carry the two-line license header, `# Copyright 2026 Automate the Cloud Inc.` and `# SPDX-License-Identifier: Apache-2.0`, placed as CONTRIBUTING.md describes.
