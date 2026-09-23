# Build and flash

## Prerequisites

- Python 3
- PlatformIO Core
- USB access to the M5Stack CoreS3 device

Install PlatformIO:

```bash
python3 -m pip install -U platformio
```

## Build

From repository root:

```bash
pio run -e m5stack-cores3
```

Successful output produces:

```text
.pio/build/m5stack-cores3/firmware.bin
```

The `.pio/` directory and firmware artifacts are ignored by git.

## Flash

Connect the robot by USB and identify the serial port. On many Linux systems it is `/dev/ttyACM0`.

```bash
pio run -e m5stack-cores3 -t upload --upload-port /dev/ttyACM0
```

If upload fails with permissions, add your user to the appropriate serial group or fix udev rules. A temporary local workaround is to adjust device-node permissions, but a persistent group/udev setup is better.

## First setup

If Wi-Fi credentials are missing or the configured network cannot be reached, the firmware starts a WPA2 setup access point named `<robot_id>-setup-XXXX` and serves the Web UI at:

```text
http://192.168.4.1/
```

The `XXXX` suffix is the last four hex digits shown in the setup SSID; the AP password is `stackchanXXXX`.

Use the Web UI to set Wi-Fi SSID/password, Gemini API key, and Web password. Reboot after changing network settings.

## Serial monitor

```bash
pio device monitor -b 115200
```

Useful boot messages:

- `SD status: ok`
- `ConfigManager init: ok`
- `WiFi: connected ip=...`
- `Gemini: ready for on-demand connect`

## Fresh checkout build check

For release verification, build from a clean copy so local build cache does not hide missing dependencies:

```bash
rm -rf /tmp/stackchan-firmware-fresh
mkdir -p /tmp/stackchan-firmware-fresh
rsync -a --exclude='.git' --exclude='.pio' ./ /tmp/stackchan-firmware-fresh/
cd /tmp/stackchan-firmware-fresh
pio run -e m5stack-cores3
```
