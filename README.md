# Jelligotchi

A C11 drawing MVP for a future virtual pet on the **Waveshare
ESP32-S3-Touch-AMOLED-1.75, SKU 31261**. It draws a square, triangle, and moving
circle. Tap/click the round screen or press Space to pause/resume; Escape quits
the desktop host. There is no creature simulation or persistence yet.

## Desktop

Prerequisites: a C compiler, CMake 3.21+, Ninja, SDL2 2.0.18+, and Make (optional).

```sh
# macOS
brew install cmake ninja sdl2
# Debian/Ubuntu
# sudo apt install build-essential cmake ninja-build libsdl2-dev

make run
make test
make sanitize     # address + undefined behavior sanitizers
make core-test    # no SDL dependency
```

Without Make: `cmake --preset desktop`, `cmake --build --preset desktop`, then
`./build/desktop/jelligotchi`. Resize the window freely; SDL maintains the aspect
ratio and maps mouse coordinates back to the native display surface.

For repeatable rendering without a window:

```sh
./build/desktop/jelligotchi --headless --frames 64 --snapshot build/shapes.bmp
```

Headless mode injects a simulated clock advancing 16 ms per frame. Interactive
mode injects SDL's monotonic clock. Tests inject their own time and input, so
they never wait for animation to advance.

## Boundary between engine and host

```text
                 core/engine.c + core/render.c
                         C11, no SDK APIs
                               |
                      include/jelli/engine.h
                               |
            +------------------+------------------+
            |                  |                  |
        SDL2 host          fake test host      ESP-IDF host
       SDL_GetTicks64      controlled time     esp_timer_get_time
       mouse/keyboard      queued input        CST9217 via BSP/LVGL
       SDL texture        memory assertions   CO5300 via BSP/LVGL
```

`JelliPlatform` injects timing, input polling, frame presentation, and optional
pause-state output. `JelliSurface` injects a host-owned RGB565 drawable buffer,
including its row stride. The core allocates no memory and includes no SDL,
LVGL, FreeRTOS, or ESP headers. Hosts own their run loops and frame pacing;
animation speed depends on elapsed injected time, not on frame count. All engine
calls are single-threaded.

The first renderer deliberately uses the board's 466×466 resolution and clips
the corners to emulate the round panel. Each framebuffer uses **434,312 bytes**.
The ESP32 host keeps two buffers in PSRAM: the core renders into one, and
`present` copies into an LVGL canvas under the BSP display mutex. LVGL never
reads the core's working buffer asynchronously. Touch events cross from the
LVGL task to the engine through a small FreeRTOS queue. DMA, byte order, and
display initialization remain the BSP's responsibility.

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
The app sets 60% display brightness and renders at approximately 30 FPS.

Flashing replaces the existing application and partition table. Preserve any
factory firmware you want to keep before the first flash. If automatic download
mode fails, use the board's documented BOOT/reset sequence, check the enumerated
port again, and retry. USB detection alone does not validate display or touch.

Bring-up checks: confirm the three shapes appear, the circle moves, a single tap
pauses/resumes it, colors are correct, and the display survives reset. Power,
battery charging, sleep/wake, audio, RTC, and creature behavior are outside this MVP.

Initial validation: desktop and SDL-free tests pass, including ASan/UBSan;
the interactive SDL host runs; repository-local bootstrap, repeat sync, and
ESP32-S3 firmware builds pass with the tracked pins. The USB serial device was
detected at `/dev/cu.usbmodem101` on the development Mac. Firmware has not yet
been flashed, so physical display/touch behavior remains unverified.

References:

- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [Board hardware reference and SKU mapping](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
- [Waveshare BSP 3.0.1](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1/readme)
