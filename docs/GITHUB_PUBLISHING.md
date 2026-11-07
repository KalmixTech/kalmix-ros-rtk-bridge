# Publishing to GitHub

This document explains how to publish this directory as the `kalmix-ros-rtk-bridge` public GitHub repository. Do not upload the parent `Product/ROS_NTRIP` directory: it contains internal test materials, historical archives, and logs that may contain credentials.

## Pre-publication checks

1. This repository is published under Apache-2.0. That license permits commercial use, modification, and redistribution of the code in this repository; public website pages must not impose a commercial-authorization restriction on that code. KALMIX trademarks, paid support, custom services, unpublished firmware/SDKs, and third-party NTRIP services remain governed by their own terms.
2. Both `package.xml` files still use the placeholder address `opensource@kalmix.example`. Replace it with an approved public KALMIX maintainer address.
3. The working directory may contain deployment configuration and historical build logs. They are excluded by `.gitignore`, but GitHub's browser upload does not automatically apply ignore rules.

## Files to include

Create the repository from this directory and retain:

```text
.github/
docs/
kalmix_gnss_serial/
kalmix_ntrip/
launch/
.gitignore
CONTRIBUTING.md
LICENSE
NOTICE
README.md
SECURITY.md
```

Do not upload:

```text
logs/
**/config/*.local.yaml
build/
install/
log/
*.log
*.bin
*.bz2
*.bzip
```

`ntrip.local.yaml` may contain NTRIP credentials, and ROS build logs may capture environment variables. If any of these files have been committed, uploaded, attached to a support request, or included in a screenshot, rotate the affected credentials with the service provider immediately.

## Recommended publishing method

Create the first commit from the repository root with Git rather than uploading an entire folder in a browser:

```bash
git init
git add .
git status --short
python3 docs/public_release_check.py
```

Confirm that neither `git status --short` nor the check script reports local configuration, logs, archives, or account information before creating the first commit and pushing to GitHub. Complete the build, test, dependency, and license review in [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md) before publication.

## Recommended first-release attachments

- A source tag, such as `v0.1.0`;
- Release notes completed from `RELEASE_NOTES_TEMPLATE.md`;
- The automatically generated source-code archive only;
- An optional SHA-256 checksum file.

Do not attach a Docker image, production configuration, raw RTCM, NMEA tracks, or test logs to a Release.
