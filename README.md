# KALMIX ROS RTK Bridge

KALMIX ROS RTK Bridge is an independently implemented bridge for receiving an NTRIP RTCM 3 correction stream and forwarding it to a GNSS receiver over a Linux serial device. This release provides the ROS 2 Jazzy implementation only. It contains no Xsens source code, SDK, copied documentation, branding, or assets.

The current ROS 2 implementation consists of two packages:

| Package | Role |
| --- | --- |
| `kalmix_ntrip` | Receives an NTRIP stream, validates RTCM 3 CRC-24Q frames, publishes `/rtcm`, and sends validated GGA messages from `/nmea` to the caster. |
| `kalmix_gnss_serial` | Reads validated NMEA sentences from a GNSS serial device and publishes `/nmea` and optional `/fix`; writes `/rtcm` frames to that same full-duplex serial device. |

Both nodes write rotating operational logs by default. Validated NMEA and RTCM data are recorded separately; see [Logs and retention](#logs-and-retention).

## Current supported environment

- Ubuntu 24.04 LTS
- ROS 2 Jazzy (the only ROS distribution included in this release)
- A GNSS receiver that accepts RTCM 3 corrections over a Linux serial device
- An NTRIP account and mountpoint from a service provider authorized by the user

The package supports plaintext NTRIP and certificate-verified `ntrips` (TLS) connections. It accepts `ICY 200 OK` NTRIP v1 responses and HTTP 200 NTRIP v2 responses. HTTP chunked transfer encoding is explicitly rejected rather than parsed incorrectly; use a caster that delivers an unchunked NTRIP stream.

## Install and build

In a ROS 2 workspace whose `src` directory contains this repository:

```bash
sudo apt update
sudo apt install libboost-system-dev libssl-dev ros-jazzy-mavros-msgs ros-jazzy-nmea-msgs
source /opt/ros/jazzy/setup.bash
cd ~/ros2_ws
colcon build --symlink-install --packages-select kalmix_ntrip kalmix_gnss_serial
source install/setup.bash
```

If updating from an earlier checkout that installed a `*.local.yaml` file, or before creating a release archive, remove the workspace's generated `build`, `install`, and `log` directories and build again. This clears stale local configuration from prior build artifacts; it does not remove the source `*.local.yaml` files.

## Configure without exposing credentials

The two `*.local.yaml` files are the only configuration files used by the
repository-level launch file. They are deliberately ignored by Git and are
therefore absent from a fresh GitHub clone:

```text
~/ros2_ws/src/kalmix-ros-rtk-bridge/kalmix_ntrip/config/ntrip.local.yaml
~/ros2_ws/src/kalmix-ros-rtk-bridge/kalmix_gnss_serial/config/serial.local.yaml
```

Create the two local files once after cloning, then fill in the deployment values:

```bash
cd ~/ros2_ws/src/kalmix-ros-rtk-bridge
cp kalmix_ntrip/config/ntrip.example.yaml kalmix_ntrip/config/ntrip.local.yaml
cp kalmix_gnss_serial/config/serial.example.yaml kalmix_gnss_serial/config/serial.local.yaml
chmod 600 kalmix_ntrip/config/ntrip.local.yaml
```

Set the caster host, port, mountpoint, username, password, TLS option, serial device, and baud rate in the two `*.local.yaml` files. Do not commit or send `ntrip.local.yaml` because it contains credentials. The `*.example.yaml` files are reference templates only; they are never selected automatically. If either local file is absent, the launch stops immediately and names the missing file instead of starting with example values.

After entering the NTRIP credentials, keep its permissions restricted:

```bash
chmod 600 ~/ros2_ws/src/kalmix-ros-rtk-bridge/kalmix_ntrip/config/ntrip.local.yaml
```

For TLS, set `use_tls: true` and normally use the caster's TLS port, commonly `443`. The server certificate hostname must match `host`.

## Run

Use the repository-level launch file to start both nodes together. It directly reads the two YAML files in this checkout, so no separate `/root/kalmix-rtk-config` mount is required. The serial device must be accessible to the container user.

```bash
source ~/ros2_ws/install/setup.bash
ros2 launch ~/ros2_ws/src/kalmix-ros-rtk-bridge/launch/kalmix_rtk.launch.py
```

When using a disposable Docker container (`docker run --rm`), mount the log directory to the host before starting the container; otherwise its logs disappear when the container exits:

```bash
docker run -it --rm \
  --privileged \
  --device=/dev/ttyUSB0 \
  -v "$PWD:/root/ros2_ws/src/kalmix-ros-rtk-bridge" \
  -v "$HOME/kalmix-rtk-logs:/root/kalmix-rtk-logs" \
  REPLACE_WITH_KALMIX_ROS2_IMAGE bash
```

The repository-level launch file always reads these two `*.local.yaml` files. To change a device, account, logging path, or baud rate, edit the local YAML files directly; do not pass another parameter-file path to this launch command.

Inspect the data flow without printing potentially sensitive NMEA positions:

```bash
ros2 topic hz /rtcm
ros2 topic echo /diagnostics
```

## Connecting a physical receiver: complete procedure

This example assumes that a GNSS receiver is connected to an Ubuntu host through USB serial and uses the same full-duplex serial interface to output NMEA and receive RTCM 3 corrections. Enable GGA/NMEA output and RTCM input in the receiver's own configuration tool before starting. This software does not modify receiver firmware or receiver protocol settings.

### 1. Connect and identify the device

1. Stop the ROS nodes, connect the GNSS receiver, antenna, and power, then confirm that the receiver has started normally.
2. After connecting USB, run:

   ```bash
   ls -l /dev/serial/by-id/
   dmesg --follow | grep -E 'tty(USB|ACM)'
   ```

3. Prefer the stable path under `/dev/serial/by-id/` instead of `/dev/ttyUSB0`, which can change after reconnecting a device. For example:

   ```text
   /dev/serial/by-id/usb-Your_GNSS_Receiver-if00-port0
   ```

4. Confirm that the current user has read and write access:

   ```bash
   ls -l /dev/serial/by-id/usb-Your_GNSS_Receiver-if00-port0
   groups
   ```

### 2. Configure Linux serial permissions

Ubuntu normally uses the `dialout` group for serial-device access. Run this once:

```bash
sudo usermod -aG dialout "$USER"
```

Log out and back in, or restart, then run `groups` and confirm that it contains `dialout`. Do not use `sudo ros2 launch` as a workaround: doing so can place configuration, logs, and environment variables under the root user and weakens credential protection.

### 3. Configure the GNSS receiver

Use the receiver vendor's configuration tool or its documented protocol. Refer to the receiver manual for exact menu names and commands.

- Match the Linux baud rate and use 8 data bits, no parity, and 1 stop bit. Common baud rates are 115200, 460800, and 921600.
- Enable NMEA output on the selected serial interface, including at least GGA. A 1–10 Hz output rate is recommended.
- Enable RTCM 3.x input on that interface and confirm that the receiver supports the RTCM message types and constellations supplied by the selected mountpoint.
- In an open-sky environment, confirm that the receiver has a valid GNSS solution. VRS services commonly require a valid GGA before they return nearby reference-station corrections.

Do not run the vendor configuration tool, `screen`, `minicom`, or any other program that exclusively opens the same serial device at the same time.

### 4. Create and populate local configuration

The repository provides local configuration templates. Edit `kalmix_gnss_serial/config/serial.local.yaml` and set `serial_port` and `baud_rate` for the physical receiver:

```yaml
kalmix_gnss_serial_bridge:
  ros__parameters:
    serial_port: "/dev/serial/by-id/usb-Your_GNSS_Receiver-if00-port0"
    baud_rate: 460800
    nmea_topic: "/nmea"
    fix_topic: "/fix"
    rtcm_topic: "/rtcm"
    require_nmea_checksum: true
```

Edit `kalmix_ntrip/config/ntrip.local.yaml` and enter the provider's `host`, `port`, `mountpoint`, `username`, and `password`. Set `use_tls: true` when the provider supplies a TLS endpoint. This file is excluded by `.gitignore` and is not committed by Git by default.

### 5. Start both nodes with the combined launch file

```bash
source ~/ros2_ws/install/setup.bash
ros2 launch ~/ros2_ws/src/kalmix-ros-rtk-bridge/launch/kalmix_rtk.launch.py
```

### 6. Confirm a working data path

In a third terminal, run:

```bash
source ~/ros2_ws/install/setup.bash
ros2 topic hz /rtcm
ros2 topic echo /diagnostics
```

The minimum success conditions are:

- `/diagnostics` reports `connected`.
- `/rtcm` has a sustained, non-zero rate.
- The serial bridge does not report `serial write timed out` or `serial write error`.
- The receiver status page or NMEA GGA quality field reports differential, RTK float, or RTK fixed. Refer to the receiver manual for the meaning of its status values.

`/fix` only represents parsed latitude, longitude, and a basic position-validity state. Do not use it alone to determine RTK float, RTK fixed, or accuracy.

### Troubleshooting

| Symptom | Check first |
| --- | --- |
| No `/nmea` | Device path, `dialout` access, baud rate, GGA output, and whether another program has opened the serial device. |
| `/nmea` is present but `/rtcm` is 0 | Caster host, port, mountpoint, account entitlement, network, firewall, and the availability of valid GGA. |
| `/diagnostics` reports `disconnected` | Read `last_error`; verify the TLS setting and port. With TLS, the certificate hostname must match `host`. |
| `/rtcm` is present but the receiver does not reach RTK | Confirm RTCM input is enabled, baud rates match, RTCM messages and constellations are compatible, and antenna/environment conditions are suitable. |
| Repeated serial write timeouts | Check that the receiver opened the correct interface, no process owns it, cabling and power are stable, and the receiver input baud rate is sufficient. |

## Logs and retention

With the default YAML settings, logs are written to `/root/kalmix-rtk-logs` and rotate when each active file reaches 50 MiB. `log_max_files: 7` retains up to seven historical rotations in addition to the active file. Change `log_directory`, `log_max_file_size_mb`, `log_max_files`, or set `enable_file_logging: false` in both local YAML files to suit the deployment.

> **Important:** these data logs are **not** written to this repository's `logs/` directory by default. In a Docker deployment, `/root/kalmix-rtk-logs` exists inside the container. If the container is started with `--rm` and this directory is not mounted to the host, every collected data log is removed when the container exits.

### Native Linux (without Docker)

`/root/kalmix-rtk-logs` is the default only because the documented Docker command runs as `root`. When starting ROS 2 directly as a normal Linux user, that directory is normally not writable; file logging will report an error and disable itself. Do **not** solve this by running `sudo ros2 launch`.

Before launching on native Linux, create a user-owned directory and set the same absolute path in **both** `kalmix_ntrip/config/ntrip.local.yaml` and `kalmix_gnss_serial/config/serial.local.yaml`:

```bash
mkdir -p /home/your-linux-user/kalmix-rtk-logs
chmod 700 /home/your-linux-user/kalmix-rtk-logs
```

```yaml
enable_file_logging: true
log_directory: "/home/your-linux-user/kalmix-rtk-logs"
log_max_file_size_mb: 50
log_max_files: 7
```

Use the real Linux login name in the path. Do not write `~/kalmix-rtk-logs` or `$HOME/kalmix-rtk-logs` in YAML: ROS passes these parameter values literally, so neither form is expanded. ROS 2's own console and launch logs are separate and continue to use `~/.ros/log/` for the account that starts the node.

| File | Content | Format |
| --- | --- | --- |
| `kalmix_ntrip_operation.log` | NTRIP connection, reconnect, and transport-error events | UTF-8 text; UTC timestamps |
| `kalmix_serial_operation.log` | Serial open/close, read/write errors, and forwarded RTCM frame sizes | UTF-8 text; UTC timestamps |
| `kalmix_nmea.log` | Checksum-valid NMEA sentences received from the GNSS serial port | UTF-8 text, one sentence per line |
| `kalmix_rtcm3.bin` | CRC-24Q-validated RTCM 3 frames received from the caster | Binary; concatenated RTCM frames, not human-readable text |

After rolling, files use a numeric suffix: `.1` is the most recently rotated file, followed by `.2` through `.7`. For example: `kalmix_nmea.log`, `kalmix_nmea.log.1`, ..., `kalmix_nmea.log.7`.

ROS 2's own console and launch logs continue to be handled by ROS under `~/.ros/log/` (`/root/.ros/log/` when running as root inside the documented container). The NMEA log contains position information; protect the mounted directory, set an appropriate retention period, and do not upload it with support requests unless it has been reviewed and approved.

## Security and operations

- Keep credentials in the process environment, a secret manager, or a protected service definition. Do not commit them, paste them into issues, or record them in shell history.
- Prefer TLS. When TLS is unavailable, Basic authentication and GGA location data traverse the network without transport encryption.
- NMEA logs contain location data. Limit access to the configured log directory and apply a retention policy appropriate to the deployment.
- RTCM is recorded only after CRC-24Q validation; NMEA is recorded only after its configured checksum validation. Neither data log contains the NTRIP username or password.
- The node checks NMEA checksums by default and validates every published RTCM 3 frame with CRC-24Q.
- `/diagnostics` reports connection state, received bytes, published RTCM frame count, reconnect count, and the last transport error. It never includes credentials or raw GGA content.

## Accuracy and safety

This software transfers correction data; it does not compute or guarantee a positioning accuracy. Accuracy and availability depend on the receiver, antenna, correction service, network, sky visibility, multipath, vehicle integration, and receiver configuration. Validate the complete system against the requirements of the intended application before operational use.

## ROS distribution scope

This release contains the ROS 2 Jazzy implementation only. A future ROS 1 implementation may be published in this repository as a separate, tested adapter with its own `catkin` build metadata and documentation. ROS 1 and ROS 2 source, launch files, dependencies, and build instructions must remain separate; only protocol concepts and test vectors may be shared.

## License

Copyright 2026 KALMIX. Licensed under Apache-2.0. Before public release, replace the `.example` maintainer address in both `package.xml` files with KALMIX's approved public open-source contact address.

