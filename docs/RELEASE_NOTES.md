# KALMIX ROS 2 RTK Bridge — Initial Public Release

## Compatibility

- Ubuntu: 24.04 (GitHub Actions CI)
- ROS 2 distribution: Jazzy
- Tested receiver interface and baud rate: Not recorded in this release note
- Tested NTRIP protocol/TLS mode: Not recorded in this release note

## Highlights

- Initial public release of the ROS 2 Jazzy RTK Bridge.

## Validation performed

- [x] `colcon build --symlink-install`
- [x] `colcon test`
- [ ] Serial NMEA input verified
- [ ] RTCM forwarding verified
- [ ] NTRIP reconnection behaviour checked
- [ ] Documentation and public-source scan reviewed

### CI evidence

- Commit: `5244de85c95eabeb43fddc2b470ca51d44b85849`
- Workflow: [ROS 2 Jazzy #1 — build-and-test](https://github.com/KalmixTech/kalmix-ros-rtk-bridge/actions/runs/42351336664)
- Trigger: push to `main`
- Result: passed

## Known limitations

- HTTP chunked NTRIP responses are rejected by this release.
- A receiver-reported RTK Fixed state is not by itself a verified accuracy measurement.
