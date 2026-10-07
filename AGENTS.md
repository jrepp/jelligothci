# Working on Jelligotchi

## Scope

This is a C11 foundation for a virtual pet on the Waveshare
ESP32-S3-Touch-AMOLED-1.75, SKU 31261. The current MVP draws three simple shapes;
one animates, and tap/Space toggles pause. Keep changes focused on the requested
increment. Do not introduce creature simulation, persistence, networking, audio,
or a larger UI unless the task calls for it.

Read `README.md` for setup, commands, and hardware bring-up details.

## Project memory

Use `docs-cms/` as durable project memory. Before changing architecture or the
development workflow, search and read the relevant ADRs, RFCs, PRDs, and memos.
Start with `docs-cms/README.md` and `docs-cms/docs-project.yaml`.

Record decisions as ADRs, proposals as RFCs, requirements as PRDs, and findings
as memos. Copy a template from `docs-cms/templates/`, use the next available ID,
set `project_id: jelligotchi`, and generate a unique UUID v4. Preserve that UUID
and the created date across edits. For new proposals use `Proposed` (ADR) or
`Draft` (RFC/PRD); memos need no status. Record approval only when the user has
actually approved the decision; do not present agent proposals as accepted.

Use `./scripts/docs` to run the pinned Docuchango through repository-local uvx.
After documentation changes, run `make docs-check`, then `make docs-fix` and
review any repairs before committing. Report unresolved findings. CI runs the
read-only check. `./scripts/docs bootstrap --guide agent` prints the full guide.

## Architecture

- `include/jelli/engine.h` defines the shared engine and host contracts.
- `core/` is platform-independent C. Do not include SDL, ESP-IDF, LVGL, or
  FreeRTOS APIs here.
- `ports/sdl/` owns the desktop window, input translation, and frame pacing.
- `ports/esp32/` owns the board integration and ESP-IDF project.
- `tests/` uses a fake host to test the same core without physical hardware.

Inject timing, drawable memory, input, and output through the existing
`JelliPlatform` and `JelliSurface` interfaces. Timing is a dependency: never
read a system clock or sleep inside the core. Animate from elapsed injected
time rather than frame count. Hosts own their run loops and pacing.

The surface uses native-endian RGB565 with stride measured in pixels. The
current renderer expects 466×466 and masks the round panel's corners. Honor
stride and buffer bounds. The host owns buffer allocation and lifetime; keep
the core free of heap allocation and hidden platform/global state.

Engine calls run on one thread. `present` must finish reading or copying the
surface before returning. On ESP32, retain the separate LVGL canvas buffer,
display mutex, and touch-event queue so the LVGL task cannot race with the core.
Keep board pin mappings, touch orientation, pixel transport, and SDK details in
the port/BSP.

## Development and validation

```sh
make run          # interactive SDL demo; click/Space pauses, Escape exits
make test         # core tests and headless SDL smoke test
make core-test    # verify the core builds without SDL
make sanitize     # address and undefined behavior sanitizers
make esp-build    # compile firmware with the repository-local SDK
```

Use deterministic fake time and input for behavior tests; do not introduce
wall-clock sleeps into core tests. Run checks appropriate to the change:

- Core/interface changes: `make test`, `make core-test`, and `make sanitize`.
- Rendering changes: inspect a snapshot or the interactive window as well.
- ESP32 or shared-interface changes: `make esp-build` when tools are available.
- Toolchain script changes: `bash -n scripts/esp` and
  `shellcheck -x scripts/esp` when ShellCheck is available; exercise affected
  commands.
- Documentation-only changes do not require rebuilding firmware.

For a deterministic screenshot:

```sh
./build/desktop/jelligotchi --headless --frames 64 --snapshot build/shapes.bmp
```

Report separately what was compiled, tested in SDL, and verified on hardware.
A successful firmware build or detected USB device does not verify the physical
display or touch. Keep build/test commands separate from flashing.

## Local ESP32 toolchain

Use `scripts/esp` or the Make targets; do not depend on a globally installed
ESP-IDF or change the developer's shell profile. Bootstrap with
`make esp-bootstrap`; reconcile the tracked SDK pin with `make esp-sync`.

- Track SDK release and commit in `toolchain.env`.
- Track uv and Docuchango versions there too; use `scripts/uv`, `scripts/uvx`,
  and `scripts/docs` to honor those pins without global installs.
- Track direct component versions in `ports/esp32/main/idf_component.yml` and
  resolved dependencies in `ports/esp32/dependencies.lock`.
- Track intentional board configuration in `ports/esp32/sdkconfig.defaults`.
- Never commit `.tools/`, `build/`, `managed_components/`, or generated
  `sdkconfig` files. Do not ignore source assets or dependency lock files.
- Make SDK upgrades explicit pin changes. Keep the manifest's IDF constraint
  consistent and rebuild after upgrades. Preserve local SDK edits.
- Resolve script paths relative to the repository, not the caller's working
  directory. Keep the workflow usable on macOS and Linux, with prerequisites
  documented. ESP-IDF paths must not contain whitespace.

Use `./scripts/esp ports` to discover the current device path; do not hard-code
a developer's serial port. When flashing is part of the task, use
`make esp-flash PORT=...`; monitor with `make esp-monitor PORT=...` (Ctrl+] exits).

## Style

Follow existing C11 conventions: four-space indentation, explicit ownership,
small functions, and `jelli_` prefixes for public functions. Keep dependencies
minimal and maintain warning-clean builds. Update `README.md` when changing
commands, prerequisites, host contracts, or the supported workflow.
