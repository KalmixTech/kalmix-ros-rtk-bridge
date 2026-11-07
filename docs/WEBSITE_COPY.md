# KALMIX Website Copy Draft

## Title

ROS 2 NTRIP RTK Data Bridge

## Body copy

KALMIX ROS 2 NTRIP RTK Data Bridge is designed for positioning integrations that use ROS 2. It brings RTCM 3 correction data from a user-authorized NTRIP service into a ROS 2 data flow and forwards it to a compatible GNSS receiver over Linux serial. It also publishes NMEA, RTCM, and connection-diagnostics topics for system integration and operational troubleshooting.

## Key capabilities

- Supports ROS 2 Jazzy and Ubuntu 24.04 LTS
- Supports NTRIP v1 `ICY 200 OK` and NTRIP v2 HTTP 200 streams
- Optional TLS transport; authentication data is not written to example configuration or operation logs
- RTCM 3 CRC-24Q validation, GGA checksum validation, and reconnection handling
- Standard ROS 2 `/nmea`, `/rtcm`, and `/diagnostics` interfaces
- Designed for compatible GNSS receivers with RTCM input over Linux serial

## Required statement

Positioning performance depends on the receiver, antenna, correction service, network, satellite visibility, multipath environment, and system-integration conditions. KALMIX does not guarantee a fixed accuracy or availability solely through this software. Users are responsible for obtaining the required NTRIP account and service rights.

## Pre-publication review

- Support numerical claims such as centimetre-level accuracy or RTK-fix time with KALMIX test reports.
- Do not use third-party service-provider, receiver-vendor, or GNSS-brand logos, case studies, test images, or claims of official compatibility or certification without written permission.
- When a third-party product must be mentioned, use factual compatibility language only and add a statement that relevant trademarks belong to their respective owners and do not imply partnership, endorsement, or certification.
