# KALMIX ROS 2 RTK Bridge vX.Y.Z

## Compatibility

- Ubuntu:
- ROS 2 distribution:
- Tested receiver interface and baud rate:
- Tested NTRIP protocol/TLS mode:

## Highlights

- 

## Validation performed

- [ ] `colcon build --symlink-install`
- [ ] `colcon test`
- [ ] Serial NMEA input verified
- [ ] RTCM forwarding verified
- [ ] NTRIP reconnection behaviour checked
- [ ] Documentation and public-source scan reviewed

## Known limitations

- HTTP chunked NTRIP responses are rejected by this release.
- A receiver-reported RTK Fixed state is not by itself a verified accuracy measurement.

## Upgrade notes

- 

## Security and privacy

No credentials, NMEA records, RTCM data, device identifiers, or raw operation logs are included in this release.
