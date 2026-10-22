# Security

## Reporting a vulnerability

Report security problems privately, not in a public issue. On GitHub, open the repository's **Security** tab and choose **Report a vulnerability**. Only the maintainers can see the report.

Include what you found, how to reproduce it, and what an attacker could do with it.

## What counts

A security problem in BAMF is anything that lets a script do more than it was written to do, or show more than it should. For example:

- a value read from a file, an argument, or a command's output that is run as code;
- a secret that reaches the log or a command line despite `SENSITIVE_ARGUMENTS` or `generate_temp_file_with_content`;
- `cleanup` removing anything that BAMF did not create, or anything outside the temporary directory;
- a temporary file created with permissions that let other users read it.

## Supported versions

Fixes are made to the latest release.
