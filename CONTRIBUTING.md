# Contributing to KALMIX ROS 2 RTK Bridge

Thank you for helping improve the project.

## Before opening an issue or pull request

- Use a supported environment: Ubuntu 24.04 and ROS 2 Jazzy.
- Reproduce with a redacted local configuration; never attach NTRIP credentials, raw NMEA, RTCM, exact coordinates, or full operation logs.
- Explain the receiver interface, baud rate, ROS 2 distribution, and the observed/expected behaviour without disclosing device-specific identifiers.
- Keep changes focused. Include tests for parser or connection behaviour when practical, and update the README when user-facing behaviour changes.

## Pull requests

1. Build and run `colcon test` before requesting review.
2. Confirm that `*.local.yaml`, `logs/`, build artifacts, and archives are not included in the change.
3. State any tested caster protocol behaviour, but do not name or expose an account unless KALMIX has written permission to publish it.

By submitting a contribution, you confirm that you have the right to submit it under the repository's published license.
