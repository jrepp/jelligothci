# Working on Jelligotchi

## Scope

This is a C11 virtual pet MVP on the Waveshare ESP32-S3-Touch-AMOLED-1.75,
SKU 31261, with an SDL host. See README.md for implemented care, gifts/rewards,
sleep, collection, and desktop persistence, and memo-008 for the remaining work.
The shapes diagnostic remains available. Keep changes focused on the requested
increment; the draft RFC does not authorize every proposed system.

Read `CONTRIBUTING.md` for the human setup and validation path, and `README.md`
for architecture, commands, and hardware bring-up details. For repository settings,
access boundaries, releases, or recovery, read
[memo-005: Maintainer handoff](docs-cms/memos/memo-005-contributor-and-maintainer-handoff.md).
Keep contributor instructions in those shared documents; agent guidance should
link to them instead of becoming the only source of operational knowledge.

## Project skills

Load the relevant versioned skill when the task calls for it:

- [jelli-safe-c](.agents/skills/jelli-safe-c/SKILL.md): C implementation,
  refactoring, ownership, bounds, and analysis. Keep the core and steady-state
  engine paths free of dynamic allocation; prefer fixed or caller-owned memory.
- [jelli-embedded-development](.agents/skills/jelli-embedded-development/SKILL.md):
  firmware, board integration, memory/latency budgets, tasks, and hardware tests.

These apply to code and embedded work, not ordinary documentation edits. Read
only the skill relevant to the current change. The current ESP32 adapter and
SDK allocate at startup; do not describe the entire firmware as heap-free.

## Project memory

Use `docs-cms/` as durable project memory. Before changing architecture or the
development workflow, search and read the relevant ADRs, RFCs, PRDs, and memos.
Start with `docs-cms/README.md` and `docs-cms/docs-project.yaml`.

For build/release, runner onboarding, or hardware bring-up, also read
[memo-004: Process learnings](docs-cms/memos/memo-004-process-learnings-and-context-remediation.md).
Use its linked evidence for the relevant task; do not load the whole history.
After a failure or deployment, capture the observation, remedy, verification,
and remaining uncertainty. Put reusable guidance in the owning instruction or
skill and link the memo, rather than duplicating the incident narrative.
Historical observations are not current state: rediscover ports/host availability,
and keep physical display/touch confirmation separate from serial startup.

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

For planned game systems, read
[RFC-001](docs-cms/rfcs/rfc-001-virtual-pet-systems-architecture.md).
It is a draft architecture, not authorization to implement every proposed system
or a claim that gameplay defaults have been approved.

- `include/jelli/engine.h` defines the shared engine and host contracts.
- `core/` is platform-independent C. Do not include SDL, ESP-IDF, LVGL, or
  FreeRTOS APIs here.
- `ports/sdl/` owns the desktop window, input translation, and frame pacing.
- `ports/esp32/` owns the board integration and ESP-IDF project.
- `tests/` uses a fake host to test the same core without physical hardware.

Inject timing, drawable memory, input, and output through the existing
`JelliPlatform` and `JelliSurface` interfaces. Timing is a dependency: never
read a system clock or sleep inside the core. Animate from elapsed injected
time rather than frame count. Color tweens are fixed-size caller-owned state;
retarget from the current value, clamp at completion, and keep feedback running
when circle motion is paused. Read ADR-009 for the input-animation contract. Hosts own their run loops and pacing.

The surface uses native-endian RGB565 with stride measured in pixels. The
current renderer expects 466×466 and masks the round panel's corners. Honor
stride and buffer bounds. Preserve pixels between frames and honor the damage
rectangle: first frame initializes the full surface, zero damage means unchanged.
Keep renderer history and buffer contents together; do not swap in a blank buffer. The host owns buffer allocation and lifetime; keep
the core free of heap allocation and hidden platform/global state.

Engine calls run on one thread. `present` must finish reading or copying the
surface before returning. On ESP32, retain the separate LVGL canvas buffer,
display mutex, and touch-event queue so the LVGL task cannot race with the core.
Keep board pin mappings, touch orientation, pixel transport, and SDK details in
the port/BSP.

## Development and validation

```sh
make run          # pet care UI with desktop saves; Space pauses, Escape exits
make run-shapes   # original input/color/motion diagnostic
make jelli-art   # local browser pixel editor (tools/jelli-art/jelli_art.py)
make test         # core tests and headless SDL smoke test
make core-test    # verify the core builds without SDL
make sanitize     # address and undefined behavior sanitizers
make esp-build    # compile firmware with the repository-local SDK
make hooks-install # activate this checkout's pre-commit hooks
make hooks-check  # formatting, analysis, size limits, and documentation
make lint-c       # all C checks without documentation validation
make format-c     # apply the pinned C formatter
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

Local builds use CMake with strict GCC/Clang diagnostics or MSVC `/W4`, and
warnings as errors. Do not relax them to make a change pass. Core CI builds and
tests Linux, macOS, and Windows without SDL. Cppcheck covers all project C;
clang-tidy covers the portable core and tests. SDK-dependent runtime behavior
still needs a port build and, where relevant, hardware verification.

Respect `.c-size-limits.json`: 80 nonblank/noncomment lines per function, 400
physical lines per C/header file, and complexity 20. Refactor responsibilities
instead of compressing code or raising thresholds. Keep any diagnostic
suppression local and explain the specific false positive. Size-gate changes
also require `./scripts/c-lint self-test`.

For a deterministic screenshot:

```sh
./build/desktop/jelligotchi --pet --headless --demo --frames 650 --snapshot build/pet.bmp
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

## Versioning and releases

Use Conventional Commit titles for commits and PRs, following the sibling auth
project: `fix:` bumps patch, `feat:` minor, and `!` or `BREAKING CHANGE:` major.
Release Please maintains VERSION, its manifest, and CHANGELOG.md through a PR.
Both CMake builds consume VERSION. Do not update only one version source.
Run `scripts/release-validate` after release metadata or workflow changes and
validate workflow syntax with actionlint. See `release.yaml` and ADR-008.
Generated release PRs receive explicitly dispatched validation. Tag creation
calls the reusable release workflow directly; do not rely on GITHUB_TOKEN
writes triggering another workflow. Source assets are published after checks.
Report hardware verification separately; releases never flash automatically.

Pin external workflow actions to full 40-character commit SHAs with exact release
version comments. Use explicit hosted OS labels, never `*-latest`. Local reusable
workflows resolve with the calling revision. Run `scripts/check-workflow-pins.py`
when changing workflows; pre-commit and release validation enforce these rules.
