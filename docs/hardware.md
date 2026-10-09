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
| `ports/esp32/main/idf_component.yml` | IDF constraint and LVGL version pin |
| `ports/esp32/boards/<board>/jelli_board/idf_component.yml` | Each board's exact BSP pin |
| `ports/esp32/dependencies.lock` | Resolved dependency versions for the 1.75 |
| `ports/esp32/dependencies.1.75c.lock` | Resolved dependency versions for the 1.75C |
| `ports/esp32/sdkconfig.defaults` | ESP32-S3, 16 MB flash, octal PSRAM, USB serial, custom partition table, app rollback (both boards) |
| `ports/esp32/partitions.csv` | Flash layout A: `nvs` and `phy_init` unchanged, 7 MB pet, 4 MB agent |
| `ports/esp32-agent/sdkconfig.defaults`, `ports/esp32-agent/dependencies.<board>.lock` | The voice agent app's configuration and resolved versions |

SDK sources live in `.tools/esp-idf`; compilers and the Python environment live
in `.tools/espressif`. Caches also live under `.tools`. Build output is under
`build/esp32` (1.75) or `build/esp32-1.75c`, downloaded components under
`ports/esp32/managed_components`. The 1.75 generates `ports/esp32/sdkconfig`;
the 1.75C keeps its generated `sdkconfig` in its build directory. The agent keeps
its `sdkconfig` in `build/esp32-agent` or `build/esp32-agent-1.75c` and its
components under `ports/esp32-agent/managed_components`.
These directories and the generated `sdkconfig` files are ignored by Git. They can
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

To upgrade board components, edit their exact versions in the relevant
`idf_component.yml`, run `./scripts/esp idf update-dependencies` once per board
(with `JELLI_ESP_BOARD=1.75c` for the 1.75C), then build and commit each
resulting lock file. Switching boards removes the other board's BSP from
`managed_components`; the next build for that board downloads it again. Never commit `.tools` or `managed_components`.

For configuration changes use `./scripts/esp idf menuconfig`; put intentional
changes in `sdkconfig.defaults`. Defaults seed a fresh configuration and do not
override an existing generated `sdkconfig`. Remove the ignored generated config
and rebuild when intentionally resetting to defaults. Local virtual environments
and CMake caches contain absolute paths: bootstrap and rebuild after moving a
checkout; the workflow is portable, its downloaded binaries/caches are not.

### Partition table and regenerating sdkconfig

`ports/esp32/partitions.csv` replaces ESP-IDF's default single-app table, whose
1 MB app partition could not hold high-resolution artwork
([ADR-012](../docs-cms/adr/adr-012-high-resolution-asset-density-and-flash-partition.md)).
Both boards and both apps (the pet and the voice agent) use layout A
([ADR-013](../docs-cms/adr/adr-013-two-apps-on-one-board-with-flash-layout-a.md)):

| Name | Type | Offset | Size | Notes |
| --- | --- | --- | --- | --- |
| `nvs` | data/nvs | 0x9000 | 0x6000 (24 KB) | Saved game; identical to the old table |
| `phy_init` | data/phy | 0xf000 | 0x1000 (4 KB) | Identical to the old table |
| `factory` | app | 0x10000 | 0x700000 (7 MB) | The pet; was 8 MB before the agent |
| `ota_0` | app | 0x710000 | 0x400000 (4 MB) | The agent ([two apps](#two-apps-on-one-board)) |
| `otadata` | data/ota | 0xb10000 | 0x2000 (8 KB) | Which app starts |
| `agent_nvs` | data/nvs | 0xb12000 | 0xe000 (56 KB) | Agent settings, apart from the save |
| `model` | data/spiffs | 0xb20000 | 0x100000 (1 MB) | Reserved for speech models |
| `art` | data/0x40 | 0xc20000 | 0x3e0000 (3,968 KB) | Reserved for agent artwork |

Because `nvs` keeps its offset and size, flashing the new table and image keeps
the saved game; erasing the whole flash still clears it. Generated `sdkconfig`
files do not pick up new defaults such as `CONFIG_PARTITION_TABLE_CUSTOM` or the
bootloader's `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. In an existing checkout,
delete them once and rebuild each board:

```sh
rm -f ports/esp32/sdkconfig build/esp32-1.75c/sdkconfig
make esp-build
JELLI_ESP_BOARD=1.75c make esp-build
```

The regenerated configuration should differ from the old one only in its
partition and rollback settings. Check the generated table without a board attached:

```sh
.tools/espressif/python_env/idf5.5_py3.13_env/bin/python \
  .tools/esp-idf/components/partition_table/gen_esp32part.py \
  build/esp32/partition_table/partition-table.bin
```

It should list the eight partitions above. The build also checks the image against
each app partition. The pet's build warns that its image is too large for `ota_0`;
that is expected, because the pet only ever goes into `factory`.
Embedded art is flash rodata: the ESP32-S3 maps it through a 32 MB data window
(0x3C000000–0x3E000000) shared with PSRAM, so a 5 MB art set leaves room.
With the shipped high-resolution art the 1.75 pet image is 5,739,168 bytes and the
1.75C image 5,728,560 bytes, each about 22% of the 7 MB partition free. A build with
legacy art only was about 0.9 MB.

### Two apps on one board

The voice agent ([RFC-003](../docs-cms/rfcs/rfc-003-voice-agent-companion.md)) is a
separate ESP-IDF project, `ports/esp32-agent`, installed in `ota_0` beside the pet.
It is at phase 0: an identity screen (a lilac ring), an `@A1` console with
read-only hardware probes, and the switch back to the pet. Its findings are in
[memo-024](../docs-cms/memos/memo-024-agent-phase0-two-apps-and-hardware-probes.md).

```sh
JELLI_ESP_BOARD=1.75c make esp-flash PORT=<port>        # the pet, once, with layout A
JELLI_ESP_BOARD=1.75c make esp-agent-flash PORT=<port>  # builds the agent, writes ota_0
./scripts/jelli-debug --port <port> app agent           # pet checkpoints, restarts into the agent
./scripts/jelli-agent --port <port> hello
./scripts/jelli-agent --port <port> probe pmu           # also chip, heap, te MS, pwr MS
./scripts/jelli-agent --port <port> boot pet            # back to the pet
```

`JELLI_ESP_APP=agent` selects the agent for any `scripts/esp` command; its build
directories are `build/esp32-agent` and `build/esp32-agent-1.75c`, its locks
`ports/esp32-agent/dependencies.<board>.lock`. `scripts/esp flash` refuses the
agent, because `idf.py flash` would write it over the pet in `factory` and reset
the boot choice. `flash-agent` first resets the boot choice to the pet (otatool
`erase_otadata`), so a new agent image always starts on trial through `app agent`,
then writes `ota_0` by name with ESP-IDF's parttool, which reads the table from the
board; on a board without layout A it fails rather than overwrite the pet. A full
pet flash always makes the pet the app that starts. Both builds refuse a generated
`sdkconfig` that still has rollback off.

Each app starts only the other's project (`jelligotchi` or `jelli_agent`), since
`ota_0` can hold leftovers of the vendor firmware. The pet's `app agent` verifies
the whole agent image before selecting it. Rollback is enabled: a switch from the
pet starts the agent on trial, and the agent confirms itself once its start-up has
finished and before its console takes commands; if it resets before then, the
bootloader starts the pet again. From the host,
`./scripts/esp boot <port> pet|agent` switches with otatool for recovery. That path
skips the trial and resets without the pet's final checkpoint (up to a minute of
progress).

## Board variants

Two Waveshare boards are supported. They share the ESP32-S3R8, the CO5300
466×466 AMOLED, and the CST9217 touch controller, but some pins differ, so each
uses its own Waveshare BSP. `JELLI_ESP_BOARD` selects one; the default is 1.75.

| Board | `JELLI_ESP_BOARD` | Flash | BSP | Pins that differ |
| --- | --- | --- | --- | --- |
| ESP32-S3-Touch-AMOLED-1.75, SKU 31261 | `1.75` (default) | 16 MB | `waveshare/esp32_s3_touch_amoled_1_75` 3.0.1 | LCD reset 39, touch reset 40, audio MCLK 42; uSD on 1–3 |
| ESP32-S3-Touch-AMOLED-1.75C | `1.75c` | 32 MB | `waveshare/esp32_s3_touch_amoled_1_75c` 3.0.0 | LCD reset 1, touch reset 2, audio MCLK 16; no uSD |

```sh
JELLI_ESP_BOARD=1.75c make esp-build
JELLI_ESP_BOARD=1.75c make esp-flash PORT=<discovered-port>
JELLI_ESP_BOARD=1.75c make esp-monitor PORT=<discovered-port>
```

Identify the board from its printed label. The pinned esptool's `flash_id`
reads the chip without writing; a 32 MB flash report indicates a 1.75C. Both
builds use the shared 16 MB flash layout, which runs on the 1.75C's larger flash.
The firmware logs `Board: Waveshare …` at startup so serial output shows which
image is running. Flashing one board's image onto the other drives the reset
and audio-clock signals on the wrong pins; do not rely on it working.

## Hardware bring-up

The ESP32 port uses Waveshare's BSP for the selected board, including its touch
orientation and panel initialization.
The app sets 60% display brightness. SDL targets an 8 ms update interval; ESP32
targets 16 ms for its engine loop and LVGL refresh timer. After three minutes
without touch, [screen sleep](playing.md#screen-sleep) clears the LVGL canvas to
black and sets the AMOLED brightness to 0 through the BSP (under the display lock,
because brightness commands share the panel bus with LVGL flushes). The panel and
LVGL stay initialized, and the CST9217 touch controller is separate from the panel,
so LVGL keeps reading touch and the engine keeps polling it. While the screen is
dark the engine skips rendering and presentation and its loop waits 50 ms instead
of 16 ms, which still drains touch well inside the 500 ms double-tap window. Taps
are timestamped when the engine polls them, so the measured gap can exceed the
physical one by up to one loop interval. A double tap restores 60% brightness after
a full redraw. Power/PMU settings, light sleep, and radio settings are unchanged. The ESP32 loop includes
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
after an explicit debug clock sync. Unknown or backward time grants no offline
progress. Screen sleep only darkens the panel; the chip does not sleep. The
screen sleep power saving, the panel staying fully dark at brightness 0, and
double-tap wake on the touch panel are unverified on hardware.
Hardware sleep, battery charging, RTC battery retention, the
[battery status reading](#battery-status), and physical interaction remain unverified.

The current pet/debug firmware has been flashed successfully; serial startup
verified PSRAM, display/touch driver initialization, and both ready messages.
Physical appearance and touch response remain unverified. See the
[deployment record](../docs-cms/memos/memo-010-pet-and-debug-firmware-deployment.md). Earlier shapes firmware was flashed and
reached its ready message; that evidence does not validate the new pet UI. See
the [USB deployment record](../docs-cms/memos/memo-003-first-usb-deployment.md) and
[current MVP validation](../docs-cms/memos/memo-008-playable-pet-mvp-and-polish-backlog.md).

## Battery status

Both boards have an X-Powers AXP2101 PMU on the BSP I2C bus (7-bit address
0x34, shared with touch and the RTC). The firmware adds it as an I2C device and
reads it every ten seconds from the engine task, outside the display lock, with a
10 ms timeout. It is strictly read-only: it never writes charger, ADC, fuel-gauge,
or power-path registers, which would need separate authorization.

| Register | Use |
| --- | --- |
| 0x00 PMU status 1 | Bit 5 VBUS good, bit 3 battery present |
| 0x01 PMU status 2 | Bits 6:5 current direction (01 charging), bits 2:0 charger state (100 done) |
| 0x18 control | Bit 3 fuel gauge enabled (power-on default 1); if clear, percent is unknown |
| 0xA4 | Battery percentage 0–100 |

Addresses and bits follow the AXP2101 datasheet V1.0 (section 6.13.2) and
Waveshare's XPowersLib, as used by their ESP-IDF `01_AXP2101` example. No battery
with VBUS present shows the plug; charge done with VBUS present shows `FULL`;
charging shows the bolt; anything else shows the stored level. An I2C error reports an unknown
reading and logs once until reads recover. See
[memo-019](../docs-cms/memos/memo-019-battery-status-indicator.md).

Only the firmware build is verified. The on-device reading, the PMU's presence on
the 1.75 (SKU 31261), the gauge's default state, and physical legibility are
unverified until a board runs this firmware.

References:

- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [Board hardware reference and SKU mapping](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
- [Waveshare BSP 3.0.1](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1/readme)
- [Waveshare 1.75C BSP 3.0.0](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75c/versions/3.0.0/readme)
- [Waveshare 1.75C documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C)
- [Board variant selection](../docs-cms/adr/adr-011-selectable-esp32-board-variants.md)
