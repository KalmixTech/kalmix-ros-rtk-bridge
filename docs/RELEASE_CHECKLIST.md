# GitHub Release Checklist

Complete these items before publishing this directory as a public GitHub repository.

- [ ] Replace `opensource@kalmix.example` in both `package.xml` files with an approved public KALMIX maintainer address.
- [ ] Run the README build procedure and `colcon test` on Ubuntu 24.04 with ROS 2 Jazzy. Record results and dependency versions in the release notes.
- [ ] Complete TCP and TLS connection tests against at least one real caster using a non-production account. Do not commit an account, password, GGA track, or raw RTCM log.
- [ ] Complete an end-to-end test with the intended receiver, antenna, serial baud rate, and network conditions. Record test conditions and failure boundaries; accuracy claims must be supported by that test, not inferred from the software feature set.
- [ ] Confirm whether the target caster uses HTTP chunked transfer encoding. This release rejects chunked responses; verify compatibility or implement and test a chunk decoder first.
- [ ] Run secret, dependency-vulnerability, license/SBOM, and manual README reviews.
- [ ] Confirm that Apache-2.0 is KALMIX's final open-source decision for the independently authored code and that it has completed internal legal/IP review.
- [ ] Exclude third-party SDKs, driver source, vendor logos, service-provider test screenshots, real customer tracks, and documentation without publication permission.
- [ ] Enable GitHub Actions, branch protection, Dependabot where applicable, and a security-disclosure contact in the repository settings.
