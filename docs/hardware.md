# ESP32 toolchain and hardware

Run commands from the repository root unless stated otherwise.

## Repository-local ESP32 toolchain

The supported bootstrap workflow runs on macOS and Linux (or Linux under WSL).
It needs Git, Bash, Python 3.10–3.13 with venv support, and standard platform
build prerequisites. On macOS install Xcode command-line tools; on Linux use
the prerequisites in [Espressif's setup guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/linux-macos-setup.html).
Python 3.14 is intentionally not selected by the wrapper.
ESP-IDF requires the checkout path to contain no whitespace.

```sh
make esp-bootstrap             # download SDK + compiler + isolated Python tools
./scripts/esp doctor           # versions and serial port detection
make esp-build
./scripts/esp ports
make esp-flash PORT=/dev/cu.usbmodem101
make esp-monitor PORT=/dev/cu.usbmodem101
# Linux ports usually look like /dev/ttyACM0. Exit monitor with Ctrl+].
```

`scripts/esp` resolves paths from its own location, so it works from other
directories and does not require sourcing an environment or changing shell
profiles. Set `JELLI_PYTHON=/path/to/python3.13` to select a bootstrap interpreter.
The wrapper verifies that the SDK tag resolves to the tracked commit before
using it. It refuses to sync over edits to tracked SDK files.

| Tracked configuration | Purpose |
| --- | --- |
| `toolchain.env` | Exact ESP-IDF release and commit |
| `ports/esp32/main/idf_component.yml` | Direct BSP and LVGL version pins |
| `ports/esp32/dependencies.lock` | Resolved component dependency versions |
| `ports/esp32/sdkconfig.defaults` | ESP32-S3, 16 MB flash, octal PSRAM, USB serial |

SDK sources live in `.tools/esp-idf`; compilers and the Python environment live
in `.tools/espressif`. Caches also live under `.tools`. Build output is under
`build/esp32`, downloaded components under `ports/esp32/managed_components`.
These directories and the generated `sdkconfig` are ignored by Git. They can
be recreated from tracked configuration. The first bootstrap downloads a
substantial SDK/toolchain and requires network access. Normal builds use the
local installation after dependencies have been fetched.

### Syncing and upgrading

`make esp-sync` restores the **tracked pin**, including submodules and tools;
it does not silently select the newest release. To upgrade ESP-IDF, change
both `JELLI_IDF_VERSION` and `JELLI_IDF_COMMIT` in `toolchain.env` to a matching
release tag and resolved commit, then run `make esp-sync` and `make esp-build`.
Keep the component manifest's IDF constraint consistent if changing release
series. Test the firmware before committing updated pins.

To upgrade board components, edit their exact versions in `idf_component.yml`,
run `./scripts/esp idf update-dependencies`, then build and commit the resulting
`dependencies.lock`. Never commit `.tools` or `managed_components`.

For configuration changes use `./scripts/esp idf menuconfig`; put intentional
changes in `sdkconfig.defaults`. Defaults seed a fresh configuration and do not
override an existing generated `sdkconfig`. Remove the ignored generated config
and rebuild when intentionally resetting to defaults. Local virtual environments
and CMake caches contain absolute paths: bootstrap and rebuild after moving a
checkout; the workflow is portable, its downloaded binaries/caches are not.


## Hardware bring-up

The ESP32 port uses Waveshare's BSP for this exact board, including its touch
orientation and panel initialization. It is not the similarly named 1.75C board.
The app sets 60% display brightness. SDL targets an 8 ms update interval; ESP32
targets 16 ms for its engine loop and LVGL refresh timer. The ESP32 loop includes
render/presentation work in that interval and yields on overruns. Actual update
rates are logged every five seconds and are not panel-refresh guarantees.
Firmware uses performance compiler optimization with assertions retained.
The earlier shapes firmware measured 61–62 engine updates/second on the attached
board during motion; this is not a panel FPS or touch-latency measurement. See
[memo-006](../docs-cms/memos/memo-006-input-animation-and-frame-performance.md).

Flashing replaces the existing application and partition table. Preserve any
factory firmware you want to keep before the first flash. If automatic download
mode fails, use the board's documented BOOT/reset sequence, check the enumerated
port again, and retry. USB detection alone does not validate display or touch.

Current firmware bring-up checks: confirm the pet and legible menu appear,
buttons match touch coordinates, care completes, Rest/Wake changes the pose,
and the display survives reset. The pet simulation's sleep state does not put
the board into hardware sleep. Firmware checkpoints to NVS and reads the board RTC
after a debug clock sync or a successful network time sync. Unknown or backward time grants no offline
progress. Hardware sleep, battery charging, RTC battery retention, and physical
interaction remain unverified.

The current pet/debug firmware has been flashed successfully; serial startup
verified PSRAM, display/touch driver initialization, and both ready messages.
Physical appearance and touch response remain unverified. See the
[deployment record](../docs-cms/memos/memo-010-pet-and-debug-firmware-deployment.md). Earlier shapes firmware was flashed and
reached its ready message; that evidence does not validate the new pet UI. See
the [USB deployment record](../docs-cms/memos/memo-003-first-usb-deployment.md) and
[current MVP validation](../docs-cms/memos/memo-008-playable-pet-mvp-and-polish-backlog.md).

References:

- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [Board hardware reference and SKU mapping](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
- [Waveshare BSP 3.0.1](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1/readme)


## Wi-Fi, time sync, and OTA

The new ESP32 network path is compiled but not yet verified on a device. Wi-Fi
starts disabled. Configure it over USB with device-scope tweakables; creature
and animation tweakables keep their existing session-only behavior. There is
no AP setup page or debug listener on Wi-Fi yet. The planned Settings button,
displayed random AP password, and optional authenticated network debug are in
[Draft RFC-003](../docs-cms/rfcs/rfc-003-wifi-provisioning-and-network-debug.md).

### First USB installation

OTA needs the new custom partition table and rollback-enabled bootloader. A
one-time USB flash is required from the older single-slot firmware. The table
keeps NVS at `0x9000`, size `0x6000`, and PHY data at `0xf000`. Two 4 MiB app
slots begin at `0x10000` and `0x410000`; OTA selection data is at `0x810000`.
The normal flash command does not erase NVS. Save preservation and migration
still need a device test; preserve a backup before deployment. Do not use a
full-chip erase. The first USB install has no previous OTA image to roll back to.

Existing generated SDK settings will not pick up new defaults. Preserve local
settings and create an isolated config from the tracked defaults:

```sh
./scripts/esp idf -D SDKCONFIG="$PWD/build/esp32-network.sdkconfig" reconfigure
make esp-build
./scripts/esp ports
# Deployment is a separate step, after checking the image and intended device:
# make esp-flash PORT=<discovered-port>
```

Inspect the generated config for `CONFIG_PARTITION_TABLE_CUSTOM=y` and
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` before flashing. Never send the new
larger image to the old 1 MiB partition layout.

### Configure through USB

Use the port reported by discovery. Close the viewer or any other serial client
before using the CLI. `wifi.password` prompts without echo when its value is
omitted, keeping it out of shell history and command-line process listings.

```sh
export JELLI_DEBUG_PORT='<discovered-port>'
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tunables --device
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tune --device wifi.ssid 'Home network'
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tune --device wifi.password
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tune --device time.timezone America/New_York
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tune --device wifi.enabled 1
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" network apply
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" network
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" clock
```

Changes remain staged in RAM until `network apply`. Apply validates the complete
set, saves one versioned NVS blob, then starts or restarts the connection. Its
reply means queued; inspect `network` until `pending` is false and `result` is
zero. Association happens after apply, so also check `connected`. A nonzero
result is an ESP-IDF error. A bad password remains saved; correct it over USB.
A later browser flow will test credentials before committing them.

`network` reports the current operation, OTA state, last received NTP time, and
internal heap/worker stack margins. `tunables --device` shows the staged values
and a password-present flag, never the password. Credentials reside in ordinary
NVS on this development build; flash encryption is not enabled. The USB protocol
hex-encodes printable strings to support spaces and quotes; this is framing,
not encryption. Request buffers are cleared after parsing.

| Device tweakable | Default | Accepted value |
| --- | --- | --- |
| `wifi.enabled` | `0` | `0` or `1` |
| `wifi.ssid` | Empty | 1–32 printable ASCII bytes when enabled |
| `wifi.password` | Empty | 8–63 printable ASCII bytes when enabled |
| `time.server` | `pool.ntp.org` | Hostname or IPv4 address, up to 63 bytes |
| `time.sync_seconds` | `3600` | 60–86400 seconds |
| `time.timezone` | `manual` | A supported region below |
| `ota.url` | Empty | Direct HTTPS binary URL, up to 191 bytes |

This first version targets WPA2 personal and WPA2/WPA3 mixed-mode networks. Open and enterprise networks, raw 64-digit PSKs, and non-ASCII settings are
not supported. To turn the radio off, stage `wifi.enabled 0`, then apply.

Supported regions: `manual`, `UTC`, `America/New_York`, `America/Chicago`,
`America/Denver`, `America/Phoenix`, `America/Los_Angeles`, `Europe/London`,
`Europe/Berlin`, `Asia/Tokyo`, `Asia/Kolkata`, and `Australia/Sydney`.
`manual` preserves the existing numeric offset. Other regions apply DST rules
automatically, including while offline with a valid RTC. These are built-in
rules, not a full timezone database; law changes require a firmware update.
Travel detection is not implemented. NTP supplies UTC, not a timezone.

Clock sync runs after Wi-Fi joins and repeats at the chosen interval. The
engine thread writes valid samples to the UTC RTC and checkpoints the save.
The pet continues with the RTC while disconnected, and retries Wi-Fi at
15-second intervals. Clock corrections do not advance monotonic simulation time.
`last_ntp_seconds` reports receipt; `clock` separately reports RTC completion.

### Stage an update

Host a compatible `build/esp32/jelligotchi.bin` at a direct HTTPS URL with a
certificate trusted by ESP-IDF's CA bundle. The device needs a valid UTC clock
for TLS. Redirects, HTTP URLs, and URL-embedded username/password credentials
are rejected. Private GitHub release URLs are not directly supported; no GitHub
access token is stored on the device. This increment does not publish firmware.

```sh
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tune --device ota.url 'https://example.com/jelligotchi.bin'
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" network apply
# Inspect network until apply completes successfully, then:
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" ota start
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" ota
# After ota reports staged:
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" ota reboot
```

The worker downloads to the inactive slot. It checks image validity, chip
compatibility, and project name before changing the next boot target. A failed
or interrupted download leaves the running slot selected. After staging, the
next reset boots the new image; `ota reboot` first saves the pet and refuses the
software restart if saving fails (`reboot_blocked: true`). A power loss can
still boot the staged image with the last periodic save. The first rendered
frame and readable save storage allow the new app to confirm itself. A crash
or reset before confirmation permits rollback to a previously valid OTA slot.
Wi-Fi availability is not required for confirmation. This boot check is not a
substitute for physical display/touch or long-running firmware tests.

OTA remains an explicit developer action. TLS authenticates the configured
server and the image digest detects corruption; this build does not enforce
release signatures, secure boot, or downgrade prevention. Use compatible save
formats across updates so rollback can still read the pet.

Hardware acceptance still needs: save-preserving USB migration, connection and
reconnect, RTC sync/offline retention, DST behavior, heap/stack margins during
TLS, valid update/reboot, corrupt/wrong-project image rejection, interrupted
transfer, rollback, and physical display/touch responsiveness during downloads.
See [network implementation evidence](../docs-cms/memos/memo-019-wifi-time-and-ota-foundation.md).
