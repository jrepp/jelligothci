# Development and architecture

Run commands from the repository root unless stated otherwise.

## Project documentation

[docs-cms](../docs-cms/README.md) holds project decisions, proposals, requirements,
and findings. Begin with the [shapes MVP memo](../docs-cms/memos/memo-001-shapes-mvp-foundation.md).

```sh
make docs-guide   # print the Docuchango bootstrap guide
make docs-check   # validate without changing files
make docs-fix     # apply available repairs; review the diff
./scripts/docs bootstrap --guide agent
```

Repository tool versions are pinned in [`toolchain.env`](../toolchain.env).
Use `scripts/uv`, `scripts/uvx`, and `scripts/docs` for the pinned local tools;
the [docs-cms guide](../docs-cms/README.md) owns setup and authoring instructions.
This repository validates Markdown documents; it has no Docusaurus website build.


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
| clang-tidy | Core, portable drivers and tests, with real C11 headers and the public interface |
| Cppcheck | All project C, including SDL and ESP32 adapters, without SDK setup |
| Lizard and file limits | See [`.c-size-limits.json`](../.c-size-limits.json) |
| Docuchango | Structured documents, frontmatter, and links |

NLOC excludes blank lines and comments; file length includes them. Split code by
responsibility when a configured limit is reached.
Cppcheck's analysis without vendor headers does not replace compiling the
adapters or testing hardware. Narrow, explained suppressions cover known
callback/borrowed-buffer modeling gaps; the clang-tidy config excludes the
heuristic requiring optional Annex K `_s` APIs, which are not portable here.

CMake enables `-Wall -Wextra -Wpedantic -Werror`, conversion/sign, shadow,
prototype, format, undefined-macro, alignment, VLA, and other strict warnings on
GCC/Clang. MSVC uses `/W4 /WX`. The [project skills](../AGENTS.md#project-skills)
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

Headless mode injects a simulated clock; `--demo` scripts the care loop.
Interactive mode injects SDL's monotonic clock. Tests inject their own time and
input, so they never wait for animation to advance.

Click/tap the labeled buttons to navigate Home, Care, More, Collection, and
Settings. Space pauses live care on desktop; Escape exits. The baby evolves
according to the game-time rules in [core/game_time.c](../core/game_time.c).
Stored pets freeze until selected. Bedtime is a pet-relative routine, independent
of your computer's local time zone.

Running the executable directly starts an unsaved session. Add `--save BASE` to
use two alternating files `BASE.0` and `BASE.1`. Saves occur on explicit requests,
selected progression actions, periodically according to the
[desktop session policy](../ports/sdl/session.c), and exit. Resume applies
bounded catch-up before committing the new time anchor and enabling input. Backward or
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
report only the region that changed (zero width/height means no changes). The core performs no heap allocation and includes no SDL,
LVGL, FreeRTOS, or ESP headers. Hosts own their run loops and frame pacing;
animation speed depends on elapsed injected time, not on frame count. All engine
calls are single-threaded.

Both renderers use the board's 466×466 native resolution and clip the corners to
emulate the round panel. The dimensions are defined by
[`JELLI_WIDTH` and `JELLI_HEIGHT`](../include/jelli/engine.h).
The ESP32 host keeps two buffers in PSRAM: the core renders into one, and
`present` copies damaged rows into an LVGL canvas under the BSP display mutex
and invalidates that region. SDL similarly updates only the damaged texture region. LVGL never
reads the core's working buffer asynchronously. Touch events cross from the
LVGL task to the engine through a small FreeRTOS queue. DMA, byte order, and
display initialization remain the BSP's responsibility.

The pet engine uses fixed caller-owned state and the simulation and resume bounds
defined in [`game.h`](../include/jelli/game.h). Its renderer uses embedded sprites
and the bitmap font. It restores bounded regions for mood changes and button
particles, redraws the round surface for broader scene changes, and reports zero
damage when unchanged. The original shapes renderer retains its smaller damage
rectangles.

## Bounded working storage

The portable [Jellipak reader](../include/jelli/jellipak.h) mounts a provisional
indexed block container through an exact-read source callback and decodes raw
or LZ4 payloads into caller-owned buffers. Source bytes remain immutable for the
mounted lifetime; failed decode output must not be published. The native
`jelli_jellipak_verify` target sweeps every block through this shared code.
[Implementation and validation evidence](../docs-cms/memos/memo-073-jellipak-block-reader.md)
records the wire boundary and reproduction commands. C image expansion,
scene residency, and threaded loading remain planned; current renderers retain
their existing asset paths.

`make jellipak` builds the desktop native verifier and writes a provisional
artwork pack plus a separate measurement report under `build/desktop/generated`.
The compiler validates shared palettes and image reconstruction, then verifies
every compressed block with the C reader before publishing the pack. Its image
metadata is currently validated in Python. See
[compiler and local desktop evidence](../docs-cms/memos/memo-074-jellipak-artwork-compiler.md)
for reproduction, measured size, and the remaining runtime boundary.

Pack builds also compare every native-decoded byte against captured
precompression buffers before publication. The
[byte-comparison evidence](../docs-cms/memos/memo-075-jellipak-native-byte-comparison.md)
distinguishes this build gate from the standalone verifier's CRC sweep.

The portable [memory API](../include/jelli/memory.h) provides checked size
arithmetic, bounded byte views, and an aligned arena over caller-owned storage.
It never allocates or frees backing memory. The host supplies an aligned region
and remains responsible for its lifetime and memory capabilities.

Arena allocation and failed operations follow the header's output/state
contracts. Rewind/reset invalidate marks and require every reclaimed view to be
unused. Checkout/checkin guards exclusive use of already allocated views:
while checked out, the owner cannot allocate, mark, rewind, or reset. These
operations require external synchronization; the token is not a mutex or a
worker executor. Initialize arena state outside its backing region and keep its
address stable while marks or leases exist.

[Memory tests](../tests/test_memory.c) and
[arena tests](../tests/test_memory_arena.c) exercise the portable contracts
without SDL, artwork tools, or an SDK. The
[adoption plan](../docs-cms/rfcs/rfc-009-bounded-memory-systems-and-adoption.md)
describes subsequent consumers. The current renderer, reload banks, and firmware
allocations do not yet use these foundations.


## Input and animation

In `make run-shapes`, a press anywhere inside the round surface cycles the
renderer palettes defined in `core/engine.c`.
Touches outside it are ignored. Each shape owns one linear RGB transition;
repeated presses retarget from its current color without jumping or queuing
animations. The transition reaches its target after
[`JELLI_COLOR_DURATION_MS`](../include/jelli/engine.h)
(visible at the next presented frame) and stays there. A delayed frame
clamps to the endpoint; animation speed does not depend on update frequency.
Space pauses circle motion on desktop, while color feedback continues.

The reusable `JelliColorTween` in `include/jelli/animation.h` uses caller-owned
storage, integer channel interpolation, and no heap allocation. Input draining remains bounded;
the ESP32 queue drops a new event when full. See
[ADR-009](../docs-cms/adr/adr-009-bounded-input-color-animation.md) for the contract.


## Device services

`include/jelli/device.h` defines optional clock, motion and display providers.
Each provider has an independent borrowed context and explicit status. Missing
callbacks report unavailable. Bind motion/display with `jelli_pet_bind_devices`
after engine init; rebinding clears the motion sample. Hosts serialize access
and keep context alive until unbound or the engine stops. The original
`JelliPlatform` interface is unchanged.

Motion polling runs once per pet frame and must not block. The host acquires
sensor observations separately, then publishes a coalesced event through
`JelliMotionMailbox` or its own synchronized queue. The sample's time uses the
engine's monotonic epoch. Failed polls clear the sample; timestamps beyond the
current frame time are rejected. SDL M injects a simulated event into this same
boundary. It has no visual or gameplay reaction yet. Normal ESP32 firmware reports
motion unavailable because pickup/wake qualification is incomplete.

Clock reads return UTC milliseconds and source validity. Host sessions retain
responsibility for durable trust, timezone and save anchors. The ESP32 session
receives a `JelliClockDriver`; it never infers trust from a moving RTC alone.
Display brightness requests accept 0..100 and return the actual adapter result.
A failed request may have reached hardware and is not automatically retried.
Sleep/wake policy is outside this interface.

The `drivers/` C11 library contains QMI8658 and PCF85063 protocols. Provide a
`JelliRegisterIo` with a bounded read/write implementation and, for QMI setup,
injected time/delay callbacks. Transfers use at most 16 bytes. Board addresses,
GPIO routing, interrupt evidence and bus locking stay in ports. QMI setup and
cleanup are synchronous host operations, never engine/ISR callbacks. The power
profile uses the same tested driver; normal and factory images exclude it.
Its explicit INT2 opt-in does not establish wake readiness on another board.

`JelliTouchGuard` applies fault cancellation before host gesture translation and
requires a valid release before a new contact. Its guard and gesture share one
owner or host lock. ESP32 uses the LVGL lock; SDL runs them on its event thread.

Use `make core-test` for fake transport and service tests, `make test` for the
SDL integration, and `make sanitize` for memory/undefined-behavior checks.
The existing C analysis and size gates include portable drivers. Firmware
profile isolation is checked with `tools/debug/check_firmware_profile.py`.
