# Jelligotchi

A C11 virtual pet MVP for the **Waveshare ESP32-S3-Touch-AMOLED-1.75,
SKU 31261**, with a shared SDL desktop host. Feed, play, clean, give gifts, claim
a first-care reward, rest/wake, recover from illness, evolve, switch between two
pets, and visit two locations. Healthy clickers cover brushing teeth, medicine, shots,
washing, and stretching; each accepted tap changes pet state. The original shapes demo remains available.
Desktop supports saves and bounded offline progress; firmware currently starts
an explicitly unsaved session. This is a partial implementation of the draft
[RFC-001](docs-cms/rfcs/rfc-001-virtual-pet-systems-architecture.md).
See [the MVP record and polish backlog](docs-cms/memos/memo-008-playable-pet-mvp-and-polish-backlog.md).
See [the action audit and visual update](docs-cms/memos/memo-015-healthy-activities-and-action-audit.md)
for exact effects, blocked actions, cached sprite geometry, and ESP32 blending limits.

New contributors: start with [CONTRIBUTING.md](CONTRIBUTING.md). Maintainers can
use the [settings and recovery runbook](docs-cms/memos/memo-005-contributor-and-maintainer-handoff.md).

## Project documentation

[docs-cms](docs-cms/README.md) holds project decisions, proposals, requirements,
and findings. Begin with the [shapes MVP memo](docs-cms/memos/memo-001-shapes-mvp-foundation.md).

```sh
make docs-guide   # print the Docuchango bootstrap guide
make docs-check   # validate without changing files
make docs-fix     # apply available repairs; review the diff
./scripts/docs bootstrap --guide agent
```

`toolchain.env` pins **uv 0.12.23** and **Docuchango 1.19.0**, the latest stable
versions checked on 2026-10-07. `scripts/uv` downloads the pinned uv to `.tools/`
on first use; `scripts/uvx` runs tools with that uv; `scripts/docs` runs the pinned
Docuchango from the repository root. These commands need Bash, curl, and network
access on first use. Python, tool environments, and caches managed by uv stay in
`.tools/`; shell profiles and the system uv installation are unchanged.

The initial setup used `./scripts/uvx docuchango==1.19.0 bootstrap`, followed by
`./scripts/docs init --project-id jelligotchi --project-name Jelligotchi`.
`uvx` already means `uv tool run`, so no extra `run` argument is needed. The
`bootstrap` command prints instructions; `init` creates the docs tree. Existing
checkouts only need the validation commands above.

To upgrade, change the exact pins in `toolchain.env` and rerun validation. When
upgrading Docuchango, review its config/schema changes and update
`docs-cms/docs-project.yaml`'s `docuchango_version`. The repository currently
stores and validates Markdown documents; it has no Docusaurus website build.

## Desktop

Prerequisites: a C compiler, CMake 3.21+, Ninja, SDL2 2.0.18+, and Make (optional).
SDL and ESP32 builds embed the PNG assets through repository-local uv, Python
3.12, and Pillow 12.0.0; first use needs Bash, curl, and network access. The
SDL-free C core build still needs only CMake and a compiler.

```sh
# macOS
brew install cmake ninja sdl2
# Debian/Ubuntu
# sudo apt install build-essential cmake ninja-build libsdl2-dev

make run          # pet; saves to build/pet-save.0 and .1
make run-shapes   # original color/motion diagnostic
make test
make sanitize     # address + undefined behavior sanitizers
make core-test    # no SDL dependency
```

GitHub Actions builds and runs the core tests on Linux, macOS, and Windows,
with SDL disabled. This checks host portability independently of the SDL and
ESP32 ports. A separate Linux job runs the SDL smoke test and sanitizers.

## C quality checks and Git hooks

```sh
make hooks-install  # enable this checkout's pre-commit hook
make hooks-check    # run every hook across tracked files
make lint-c         # formatting, static analysis, function/file size checks
make format-c       # apply formatting, then review and stage the diff
```

The installed hook runs through the pinned pre-commit wrapper. It checks the
staged snapshot and does not auto-fix source or docs. Docuchango checks the full
docs tree on every commit so changes to link targets are also caught. Hook
environments and tools stay under `.tools/`. Repeat `make hooks-install` after
cloning; it sets this repository's `core.hooksPath` to `.githooks`. Hooks need
Bash (Git Bash on Windows), curl, and network access for initial tool downloads.
To remove this checkout's hook setting, run `git config --local --unset core.hooksPath`.

The checks are:

| Check | Coverage |
| --- | --- |
| clang-format | Tracked project C sources and headers |
| clang-tidy | Core and tests, with real C11 headers and the public interface |
| Cppcheck | All project C, including SDL and ESP32 adapters, without SDK setup |
| Lizard and file limits | 80 function NLOC, 400 physical file lines, complexity 20 |
| Docuchango | Structured documents, frontmatter, and links |

NLOC excludes blank lines and comments; file length includes them. Limits live
in `.c-size-limits.json`. Split code by responsibility when a limit is reached.
Cppcheck's analysis without vendor headers does not replace compiling the
adapters or testing hardware. Narrow, explained suppressions cover known
callback/borrowed-buffer modeling gaps; the clang-tidy config excludes the
heuristic requiring optional Annex K `_s` APIs, which are not portable here.

CMake enables `-Wall -Wextra -Wpedantic -Werror`, conversion/sign, shadow,
prototype, format, undefined-macro, alignment, VLA, and other strict warnings on
GCC/Clang. MSVC uses `/W4 /WX`. The [project skills](AGENTS.md#project-skills)
describe bounded embedded development and safe C11 practices. GitHub Actions
runs the same hooks and tests the size checker against oversized fixtures.

## Private runner routing

The GitHub repository is [jrepp/jelligothci](https://github.com/jrepp/jelligothci).
Private runner admission is owned by
[t1-hosting](https://github.com/jrepp/t1-hosting/tree/main/runner-router), which
holds host allowlists, GitHub App installation IDs, and pool policy. No runner
App key or registration token belongs in this project.

The `Private runner canary` workflow can be dispatched for `linux` or `macos`
after the corresponding host policy is deployed. It builds and tests the
SDL-free core using the approved `router-small` profile. Linux requests
`self-hosted/linux/x64`; macOS requests `self-hosted/macos/arm64`.

Regular core CI defaults to GitHub-hosted runners. Set the repository variable
`PRIVATE_LINUX_RUNNERS=true` or `PRIVATE_MACOS_RUNNERS=true` only after that
platform's private canary and cleanup checks pass. Windows remains hosted.
Unset a variable or set it to `false` to restore hosted CI for that platform.
Each variable controls routing at queue time; jobs do not automatically fall
back if a private host goes offline. The other workflows currently use hosted
Linux runners.

The current onboarding status is recorded in t1-hosting's operational memo.
Mac Studio was offline during the initial inspection, so macOS must remain
hosted until its separate bring-up succeeds.

Without Make: `cmake --preset desktop`, `cmake --build --preset desktop`, then
`./build/desktop/jelligotchi`. Resize the window freely; SDL maintains the aspect
ratio and maps mouse coordinates back to the native display surface.

For repeatable rendering without a window:

```sh
./build/desktop/jelligotchi --pet --headless --demo --frames 650 --snapshot build/pet.bmp
./build/desktop/jelligotchi --shapes --headless --frames 64 --snapshot build/shapes.bmp
```

Headless mode injects a simulated clock advancing 8 ms per frame (100 ms with
`--demo`, which scripts the care loop). Interactive
mode injects SDL's monotonic clock. Tests inject their own time and input, so
they never wait for animation to advance.

Click/tap the labeled buttons to navigate Home, Care, More, Collection, and
Settings. Space pauses live care on desktop; Escape exits. The baby evolves
after 60 seconds of active time for this accelerated slice. Stored pets freeze
until selected. Bedtime is a pet-relative routine, initially 22:00 for eight hours;
it is independent of your computer's local time zone.

Running the executable directly starts an unsaved session. Add `--save BASE` to
use two alternating files `BASE.0` and `BASE.1`. Saves occur on explicit requests,
selected progression actions, every 60 seconds, and exit. Resume advances at most
six hours before committing the new time anchor and enabling input. Backward or
unavailable clocks forgive elapsed time. `--wall-ms N` injects wall time in
headless tests. A valid older slot can recover from a damaged newer one. If no
valid slot remains, or a slot is incompatible, files are preserved and startup
fails; choose a different base path or omit `--save` for an unsaved session. Do not run
two processes against the same save path. Desktop file writes are synchronous;
background IO and stronger crash durability remain in the backlog.

## Boundary between engine and host

```text
                 core/game*.c + core/pet_*.c
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
including its row stride and a bounded damage rectangle. Preserve its pixels
between frames: the first render initializes the full surface; subsequent frames
report only the region that changed (zero width/height means no changes). The core allocates no memory and includes no SDL,
LVGL, FreeRTOS, or ESP headers. Hosts own their run loops and frame pacing;
animation speed depends on elapsed injected time, not on frame count. All engine
calls are single-threaded.

Both renderers use the board's 466×466 resolution and clip
the corners to emulate the round panel. Each framebuffer uses **434,312 bytes**.
The ESP32 host keeps two buffers in PSRAM: the core renders into one, and
`present` copies damaged rows into an LVGL canvas under the BSP display mutex
and invalidates that region. SDL similarly updates only the damaged texture region. LVGL never
reads the core's working buffer asynchronously. Touch events cross from the
LVGL task to the engine through a small FreeRTOS queue. DMA, byte order, and
display initialization remain the BSP's responsibility.

The pet engine uses fixed caller-owned state, a 100 ms simulation step, at most
eight live ticks per frame, and bounded resume segments. Its renderer uses the
embedded sprites and bitmap font. It restores bounded regions for mood changes and button particles, redraws the
round surface for broader scene changes, and reports zero damage when unchanged. The original shapes renderer retains its smaller damage rectangles.

## Input and animation

In `make run-shapes`, a press anywhere inside the round surface cycles three fixed color palettes.
Touches outside it are ignored. Each shape owns one linear RGB transition;
repeated presses retarget from its current color without jumping or queuing
animations. The transition reaches its target at 300 ms of injected elapsed
time (visible at the next presented frame) and stays there. A delayed frame
clamps to the endpoint; animation speed does not depend on update frequency.
Space pauses circle motion on desktop, while color feedback continues.

The reusable `JelliColorTween` in `include/jelli/animation.h` uses caller-owned
storage, integer channel interpolation, and no heap allocation. The engine uses
three slots, not a dynamic animation scheduler. Input draining remains bounded;
the ESP32 eight-event queue drops a new event when full. See
[ADR-009](docs-cms/adr/adr-009-bounded-input-color-animation.md) for the contract.

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
[memo-006](docs-cms/memos/memo-006-input-animation-and-frame-performance.md).

Flashing replaces the existing application and partition table. Preserve any
factory firmware you want to keep before the first flash. If automatic download
mode fails, use the board's documented BOOT/reset sequence, check the enumerated
port again, and retry. USB detection alone does not validate display or touch.

Current firmware bring-up checks: confirm the pet and legible menu appear,
buttons match touch coordinates, care completes, Rest/Wake changes the pose,
and the display survives reset. The pet simulation's sleep state does not put
the board into hardware sleep. Firmware currently has no retained wall clock or
save adapter: resetting starts fresh, with Save unavailable. Power management,
battery charging, audio, RTC retention, and physical interaction remain unverified.

The current pet/debug firmware has been flashed successfully; serial startup
verified PSRAM, display/touch driver initialization, and both ready messages.
Physical appearance and touch response remain unverified. See the
[deployment record](docs-cms/memos/memo-010-pet-and-debug-firmware-deployment.md). Earlier shapes firmware was flashed and
reached its ready message; that evidence does not validate the new pet UI. See
the [USB deployment record](docs-cms/memos/memo-003-first-usb-deployment.md) and
[current MVP validation](docs-cms/memos/memo-008-playable-pet-mvp-and-polish-backlog.md).

References:

- [Waveshare board documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [Board hardware reference and SKU mapping](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
- [Waveshare BSP 3.0.1](https://components.espressif.com/components/waveshare/esp32_s3_touch_amoled_1_75/versions/3.0.1/readme)

## Debug CLI

The desktop app and ESP32 share the same state, button, tap, and screenshot
protocol. On macOS/Linux, `make run` opens `build/jelli-debug.sock`; restart an
already-running app to enable the new interface. From a second terminal:

```sh
./scripts/jelli-debug state
./scripts/jelli-debug buttons
./scripts/jelli-debug press MENU
./scripts/jelli-debug press CARE
./scripts/jelli-debug press "BASIC CARE"
./scripts/jelli-debug screenshot build/local-screen.png
```

Without `--port`, the CLI defaults to this checkout's local socket. For another
instance, launch the executable with `--debug-socket /path/to/session.sock` and
pass `--socket /path/to/session.sock` to the CLI. Direct executable runs without
`--debug-socket` do not open an endpoint. With `--headless --debug-socket PATH`,
the app stays alive until stopped (or `--frames N` completes), pacing iterations
so external clients can interact; ordinary headless tests retain their fast mode.
The local endpoint is a user-only Unix socket, not a TCP listener.

Normal exit removes the socket. After a crash, confirm no game owns the endpoint
before removing its stale socket or choose a fresh path. Startup never replaces
an existing socket/file. Client disconnect releases an unfinished capture; closing
the window or pressing Escape still works during capture. Local state, input,
screenshots, and reconnect behavior are tested against the actual SDL app.

The ESP32 build includes a small debug interface on its existing USB Serial/JTAG
console. Build with `make esp-build`; when ready to deploy, discover the port
with `./scripts/esp ports` and use the separate `make esp-flash PORT=...` command.
Close `esp-monitor` and other serial clients before using the CLI. Firmware startup after flashing is verified; this feature has host test coverage,
but live debug commands and screenshot transfer have not yet been exercised.

```sh
./scripts/jelli-debug ports
# Set this to the port discovered above, rather than reusing an old device path.
export JELLI_DEBUG_PORT=/dev/cu.usbmodem...  # Linux typically /dev/ttyACM...
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" state
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" buttons
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" press MENU
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" press CARE
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" press "BASIC CARE"
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tap 114 332
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" screenshot build/pet-live.png
```

The CLI runs through repository-local uv, Python 3.12, and pinned pyserial 3.5.
`state` reports the last rendered view and visible buttons. `press` accepts a
current label or one-based index; it checks that the page has not changed.
Inputs use the normal care rules. A delivered input can still be rejected by the
game; inspect the subsequent state's result field. The CLI never automatically
repeats a timed-out input.

Screenshots contain exact renderer pixels plus matching `.png.json` state.
Capture temporarily freezes the game to reuse its existing framebuffer, then
releases it; physical taps during capture are ignored. Abandoned captures expire
after five idle seconds, with a thirty-second hard limit. Capture time is forgiven.
No extra framebuffer or task is added; adapter static state is about 4.2 KiB,
plus 4.25 KiB of startup USB rings and SDK overhead. Ordinary state reads do not
freeze the game. See [the protocol and validation record](docs-cms/memos/memo-009-wired-debug-interface-and-validation.md).

## Slice artwork preview

Artwork for the pet slice lives in [assets/slice](assets/slice/README.md).
It includes both creature forms, care icons, gifts/props, and a bitmap font.
The SDL and ESP32 pet builds embed these assets as immutable C data.

```sh
./scripts/uv run --python 3.12 tools/assets/build_slice.py
open build/assets/preview.html  # macOS; otherwise open the file in a browser
```

The command validates all 47 PNGs and creates a self-contained interactive HTML
sheet, contact sheet, and raw RGB565/mask exports under ignored `build/assets/`.
Python 3.12 and Pillow 12.0.0 are supplied through repository-local uv. See the
asset README for formats, provenance, reproduction, and remaining integration work.

## Versions and releases

This project follows the [auth project](https://github.com/jrepp/auth) pattern
(no local sibling checkout is required): Conventional Commits feed Release
Please, which opens a release PR updating `VERSION`, `CHANGELOG.md`, and
`.release-please-manifest.json`. `fix:` means patch, `feat:` means minor, and
`!` or a `BREAKING CHANGE:` footer means major. These rules also apply at 0.x.
Use the same convention for PR titles, since squash merges become commits.

Merging the release PR creates `vX.Y.Z` and a GitHub Release. The reusable
release workflow validates metadata, builds/tests the core on all three desktop
platforms, runs SDL sanitizers and C/docs checks, then packages macOS and Windows x64 game ZIPs with bundled SDL, editable source-art ZIP, standalone
preview HTML (also zipped), source archive, and `SHA256SUMS`. Game archives name
the actual build architecture. Builds are ad-hoc signed, not Apple notarized;
macOS may require approval in Privacy & Security. Releases never flash hardware.
Opening the app starts an unsaved session; use the bundled CLI with `--save` for persistence.

To reproduce packages on macOS after `make test`, build the preview with
`./scripts/uv run --python 3.12 tools/assets/build_slice.py`, then run
`python3 scripts/package-release.py`. Downloads appear in `build/release/`.
Manual publication also accepts an existing `dev-` tag for development releases,
without changing the Release Please-managed semantic version.
The release record is created before checks finish; verify the release workflow
is green and its assets are present before using a release.

```sh
./scripts/release-validate  # metadata plus core CMake build/test
# Revalidate a candidate; omit publish to avoid changing release assets.
gh workflow run release.yml -f ref=main
# Retry asset publication for an existing release after investigating failure.
gh workflow run release.yml -f ref=v0.1.0 -F publish=true
```

The default GitHub token cannot trigger CI through bot-created PR/tag events.
The automation explicitly dispatches release-PR validation and calls release
validation after tagging, without a stored personal access token. The repository
must allow GitHub Actions to create pull requests. CMake and ESP-IDF both read
`VERSION`; generated build metadata therefore follows the release version.

Private Linux core jobs install the pinned CMake package through repository-local
uv. The shared runner image provides the compiler and Make.

External workflow actions use full commit SHAs with release-version comments.
Hosted OS labels are explicit: Ubuntu 24.04, macOS 15, Windows 2025. GitHub still
updates those hosted images; these are OS selections, not immutable VM images.
`python3 scripts/check-workflow-pins.py` rejects floating actions and OS aliases.

## Large ring controls and moments

Tap MENU for care, moments, rest/wake, direct pet switching, gifts, and settings.
Only useful actions occupy ring positions. The bottom arrow BACK is the only
return action; X CLOSE dismisses the home ring. There is no separate confirm or
save button: successful actions request a save. ESP32 persistence remains unavailable.
The home tile shows one overall MOOD score, 1–100 (best), combining care needs,
fun, connection, and bond. Detailed needs remain in the CLI and companion.
Gentle touches improve mood; rapid repeated touches request space and then show
an overwhelmed reaction. Sensitivity recovers with time. Each pet has stable
moment preferences and time-of-day bonuses; current balance is a prototype.

Night fades over 12 seconds to darker gray with a crescent above the actor;
icons gain a subtle cool tint. Sleeping pets emit medium-blue Z particles from
half the visible sprite height above the cached centroid.

MOMENTS offers breakfast, tea, going out, and a movie. The prototype maps these
to existing feed, play, and travel actions. SDL uses its host's local clock for
suggestions; ESP32 currently falls back to the pet's relative day. Suggestions
never lock actions. Distinct ritual scenes and effects are tracked in
[PRD-001](docs-cms/prd/prd-001-moments-and-readable-care.md).

Button presses emit short native confetti bursts, including while gameplay is
paused. The fixed pool contains 24 eight-byte particles and uses no extra
framebuffer or runtime allocation. The rings/art/particles were flashed and externally tested; see
[memo-012](docs-cms/memos/memo-012-cli-acceptance-and-creature-tunables.md) for current deployment and tuning evidence.

## Runtime tunables and external acceptance

The same CLI supports SDL and the ESP32. Add `--port PORT` before the command for
USB; omit it for the default local socket. Get stable creature IDs from `state`.

```sh
./scripts/jelli-debug tunables
./scripts/jelli-debug tune idle_frame_ms 900
./scripts/jelli-debug tune animation_scale_pct 300
./scripts/jelli-debug tunables --creature 1
./scripts/jelli-debug tune idle_frame_ms 1200 --creature 1
./scripts/jelli-debug tune idle_frame_ms reset --creature 1
```

`animation_scale_pct` scales UI durations: larger means slower. 300 makes slides,
carousel holds, and confetti three times as long as the original defaults.
Creature idle timing is independent: 900 ms/frame is twice the original duration.
The listing includes ranges, units, defaults, effective values, and their source.
Other controls are `stat_hold_ms`, `stat_slide_ms`, `burst_count`, and
`particle_spread_pct`. Hold/slide values are before duration scaling. Tunables
are session-only; reset removes the selected override and restores inheritance.
Gameplay timing and rewards are unaffected.

The portable [registry](include/jelli/tunables.h) supports immutable form profiles
through `jelli_tunables_register(&engine.ui.tunables, profiles, count)`. Profiles
must outlive the engine. Resolution is default → form profile → global override
→ creature override. Both existing forms currently inherit common defaults.
This establishes a content extension point without inventing different balance
for the two creatures.

For a black-box acceptance run against an already-running game:

```sh
python3 tools/debug/acceptance.py --socket build/jelli-debug.sock --tunables --output build/acceptance-local
python3 tools/debug/acceptance.py --port "$JELLI_DEBUG_PORT" --tunables --output build/acceptance-esp32
```

Run local first, then discover the USB port and run against firmware with the
same protocol extension. The harness uses separate public CLI processes, writes
JSON evidence and a verified PNG, tests sleep/wake and menus, and restores sleep
state on success. Close other serial monitors. On the verified macOS native USB
Serial/JTAG connection the CLI keeps DTR/RTS asserted to avoid reset on open;
other serial adapters need separate validation.

### Healthy activity debug flow

With a running local debug socket (or add `--port` for the ESP32), start at the
closed home scene:

```sh
./scripts/jelli-debug press MENU
./scripts/jelli-debug press CARE
./scripts/jelli-debug press HEALTH
./scripts/jelli-debug press 'BRUSH TEETH'
./scripts/jelli-debug press 'BRUSH TEETH'  # one game-logic tap; inspect current stage and target
./scripts/jelli-debug state              # visual.clicker_hits, needs, bond, result
```

The activity icon sits above the actor; touching the actor itself does not count.
BACK walks activity → health → care → home. On the health ring use button `0` to
select the bottom BACK unambiguously (the ring also contains BACK).
The manual external acceptance script supports `--health` alongside `--tunables`;
it checks every clicker, internal bond changes, completion guards, and screenshots.

### Sound audition

Six tiny procedural cues are available through `jelli-debug sound`: `chirp`,
`happy`, `sparkle`, `hello`, `sleepy`, and `tap`. Hello and sleepy are vowel-like pet voices.
Accepted menu presses play a quiet 65 ms tap at volume 25, coalesced within 120 ms.
Pass `--volume 0..80` (default 35). Both native SDL and the ESP32 support this
command; sound output is asynchronous and does not block the engine thread.
Interactive SDL initializes audio at startup; headless SDL needs `--audio`.
`SDL_AUDIODRIVER=dummy` enables silent integration checks. No sound plays on boot.

```sh
./scripts/jelli-debug sound hello --volume 35
python3 tools/audio/preview.py
open build/audio/preview.html
```

See [sound sources and audition exports](assets/sound/README.md). ESP32 initialization
uses the pinned BSP's ES8311 codec path. Its codec/driver allocate at startup;
steady-state synthesis uses fixed buffers and a four-request queue.

### Readable event companion and activity routines

```sh
./scripts/jelli-viewer --socket build/jelli-debug.sock
# Or use the discovered USB port (close other serial clients first):
./scripts/jelli-viewer --port "$JELLI_DEBUG_PORT"
```

Open `http://127.0.0.1:8765`. The companion owns the debug wire, polls at 800 ms,
groups repeated taps, and shows accepted/blocked actions and actual need changes.
Pause reading or scroll back to hold the feed still. Care details and game buttons
are collapsible. There are no frame/animation/decay events. The device retains
32 fixed 64-byte records; the browser keeps 100 grouped rows and reports history gaps.
`jelli-debug events --after 0` also reads the bounded stream directly.

Brushing is a six-step routine: brush, floss, quick brush, mouthwash, spit, clean up.
The first brush and floss vary from one to three taps; later steps default to one.
Other healthy activities default to three taps. The icon, label, and dots update
at each step; every accepted tap affects care/bond, with a final celebration.
Tuning applies when a round starts, so an in-progress target stays fixed.

```sh
./scripts/jelli-debug tunables
./scripts/jelli-debug tune activity.taps 3
./scripts/jelli-debug tune activity.brush.min_taps 1
./scripts/jelli-debug tune activity.brush.max_taps 3
./scripts/jelli-debug tune activity.floss.min_taps 2 --creature 1
./scripts/jelli-debug tune activity.floss.max_taps 3 --creature 1
./scripts/jelli-debug tune activity.finish.taps 1
./scripts/jelli-debug tune scene.night_transition_ms 12000
```

All canonical names have a namespace: `pet.*`, `ui.*`, `fx.*`, `scene.*`, or
`activity.*`. Legacy flat input names remain aliases; listings return canonical
names. Overrides remain session-only, with creature overrides above global/form
profiles. Tap counts are bounded 1–6. Effective min/max endpoints are sorted,
including mixed-scope overrides. The old `ui.stat_hold_ms`/`ui.stat_slide_ms`
settings are retained for compatibility but the single mood tile does not slide.

The debug viewer includes an active-pet cheat panel: needs and bond accept 0–100,
food and gifts 0–20, and Heal restores care needs and clears illness/touch stress.
Changes appear as Debug events and obey active recovery/item reservations.
CLI examples: `./scripts/jelli-debug cheat energy 75` and
`./scripts/jelli-debug cheat heal` (add the usual socket or port option).
Quiet periodic coos use `audio.coo_enabled` and `audio.coo_interval_ms` (default
30000); menus, sleep, activity, and recent input suppress them.

Windows release builds compile pinned SDL2 with MSVC and bundle `SDL2.dll` beside
the executable. Extract the ZIP before running it. Builds are unsigned; the live
Unix-domain debug socket is currently macOS/Linux only. Windows still tests the
portable debug protocol, saves, rendering, and headless SDL game. For a local
Windows SDL build, install the pinned uv from `toolchain.env`, build/install SDL2,
and configure CMake with its install directory in `CMAKE_PREFIX_PATH`.

Settings now owns pet selection, sleep/wake, bedtime, and a large local/pet clock.
Back and close use icon-only controls partly below the round screen. Ring icons
travel out/in on menu changes; CLI presses wait up to three seconds for the ring
to settle. Shots take 1–3 taps, medicine one small dose; each has a per-pet hour
cooldown. Shot progress survives menu changes. The desktop checkpoint codec now
writes version 2, migrates version 1, and rejects future versions without replacing
them. Device checkpoints and portable pet-memory import are still planned.

Touch navigation: swipe up to open the menu; swipe down to go back one level
(or close the main ring). Swipe left/right on the main scene to select Mood,
Fullness, Energy, Hygiene, Play, or Social. The selected stat stays put. Desktop
mouse drags use the same recognizer: taps commit on release, swipes need 48 pixels
and a clear dominant axis, and ambiguous drags do nothing.
`./scripts/jelli-debug --socket build/pet.sock swipe left` exercises the same UI
navigation over the debug interface; `up`, `down`, and `right` are also supported.

Settings shows a smaller clock face with a gear button. Tap the gear for distinct
TZ −/+, HR −/+, and MIN −/+ controls; Back returns to Settings. TZ changes a
session offset relative to the host local clock (or the simulated pet clock on
ESP32) in 30-minute steps, bounded to −12/+14 hours. Hour and minute adjustments
wrap at midnight. These affect the display, atmosphere, and moment suggestions;
they do not set the hardware RTC or persist across restarts yet. The sleep button
has an ON/OFF badge showing actual sleep state. Menu movement uses a short
acceleration followed by a long integer ease-out, with no extra frame buffers.

Timed activity buttons carry a play badge and return to the pet scene when
started. Unavailable actions are dimmed using the same game rules as execution;
they produce no celebration or state change. The debug console also disables
those buttons. Settings shows the VERSION-derived build number in large text.
