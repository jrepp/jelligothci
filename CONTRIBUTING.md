# Contributing to Jelligotchi

Start here for a new checkout. You do not need the original chat, an agent,
private runner access, or the maintainer's sibling repositories to develop the
core, SDL host, docs, or firmware.

The private GitHub repository is named `jrepp/jelligothci`; the application and
usual local directory are named `jelligotchi`. Ask a repository maintainer for
GitHub access. Host SSH access is a separate operator responsibility.

## Supported paths and evidence

| Work | macOS | Linux | Native Windows |
| --- | --- | --- | --- |
| C11 core with CMake | Tested locally and in CI | Tested in CI, hosted and private | Tested in CI with MSVC |
| SDL desktop and sanitizers | Tested locally | Tested in hosted CI | Not validated |
| Bash wrappers, docs, C checks, hooks | Tested locally | Tested in hosted CI | Git Bash alone is not a validated full toolchain |
| ESP-IDF bootstrap and firmware build | Tested locally | Supported wrapper path; fresh-machine setup not yet verified | Use the Linux path under WSL; USB forwarding is not verified |
| USB flash and serial startup | Verified on the development Mac | Not verified | Not verified |

These are historical toolchain observations, not guarantees for every machine or
validation of the latest pet implementation on every platform. Physical
shape appearance and touch behavior still need confirmation; see
[hardware evidence](docs-cms/memos/memo-003-first-usb-deployment.md).

## Clone and prepare

Install Git and authenticate using your own GitHub identity, then clone:

```sh
git clone https://github.com/jrepp/jelligothci.git jelligotchi
cd jelligotchi
```

Use a path without whitespace if you will build firmware. Do not copy another
checkout's `.tools`, Python environments, CMake caches, or generated SDK config;
some contain absolute paths. Downloads require network access on first use.

For macOS/Linux, install a C compiler and SDK headers, CMake 3.21+, Ninja, Make,
Bash, curl, and SDL2 2.0.18+ for the desktop host. Repository Python tooling uses
pinned uv and its managed Python 3.15 interpreter. ESP-IDF bootstrap separately
requires system Python 3.10–3.13 with venv support. Examples:

```sh
# macOS: Xcode command-line tools must be installed as well.
brew install cmake ninja sdl2 python@3.13
# Ubuntu/Debian: choose an available Python 3.10-3.13 for ESP-IDF bootstrap.
# sudo apt-get install build-essential cmake ninja-build libsdl2-dev git curl python3 python3-venv
```

Distribution packages may supply a newer unsupported Python for ESP-IDF. Check its
version and set `JELLI_PYTHON` to a compatible executable when needed. Package
manager tools and host compilers are prerequisites, not repository artifacts. The
`./scripts/uv` and `./scripts/esp` wrappers keep their tools and environments under
`.tools/`.
Rendered builds fetch the Python and Pillow versions declared in the
[asset generator](tools/assets/build_slice.py) through uv to embed the tracked
PNGs. Core-only builds do not run the asset generator.

```sh
make hooks-install
make test          # expect engine and headless SDL tests to pass
make core-test     # verifies the core without SDL
make hooks-check   # run every configured hook
make run           # pet care UI; desktop saves under build/
make run-shapes    # original color/motion diagnostic
```

Hooks are installed per checkout. Stage newly added source files before checking
all tracked files; Git-based checks cannot discover untracked files. Tools
usually print their first-use download progress into the terminal.

Studio commits also run the JavaScript/JSDoc type check and a static container
layout check; their file triggers live in [.pre-commit-config.yaml](.pre-commit-config.yaml).
The type check needs Node.js with `npx`, caches the pinned TypeScript package
under `.tools/`, and rejects errors beyond the existing baseline. The hook
forces check mode even if `JELLI_TS_UPDATE` is set; baseline updates remain an
explicit authoring action. Changes to the packaging checker also run its isolated regression fixtures.
Unrelated commits skip these studio hooks.

## Core-only path, including Windows

With CMake and a C compiler installed, the following commands need neither Bash,
Make, Ninja, SDL, nor ESP-IDF. On Windows use a shell with the MSVC tools available.

```sh
cmake -S . -B build/core-native -DJELLI_BUILD_SDL=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-native --config Release --parallel 2
ctest --test-dir build/core-native --build-config Release --output-on-failure --no-tests=error
```

Expect all core/storage tests to pass. Native Windows core CI does not establish that
the shell wrappers, Python lint wheels, SDL, or USB workflow work on Windows.
Use a supported macOS/Linux environment for those checks.

## Make and review a change

Create a branch and keep the change within the requested increment. Read the
[architecture and ownership contracts](docs/development.md#boundary-between-engine-and-host)
and [project instructions](AGENTS.md). The core receives memory, time, input,
and output from hosts; it must not gain SDK dependencies or heap allocation.

| Change | Validation before review |
| --- | --- |
| Core, public contract, rendering | `make test core-test sanitize`; inspect rendering changes |
| ESP32 adapter or shared interface | Above as relevant, plus `make esp-build`; physical checks when behavior changes |
| C size gate | `./scripts/c-lint self-test` |
| Release/build version behavior | `./scripts/release-validate` (includes incremental-version regression) |
| Workflows | `python3 scripts/check-workflow-pins.py` and actionlint when available |
| Jelli Art studio page | The studio suites and `tools/jelli-art/ui_tests/run.sh` ([Tests](tools/jelli-art/README.md#tests)) |
| Documentation | `make docs-check`, `make docs-fix`, review repairs |

Run the relevant hooks and provide the exact revision, commands, outcomes, and
remaining gaps in the PR. Use Conventional Commit titles (`fix:`, `feat:`,
`docs:`, `ci:`); mark breaking changes explicitly. Squash merges use the PR title
as the release input. Do not hand-edit only one version file or bypass failing
checks to land a change. Automated enforcement and access settings are described
in the [maintainer runbook](docs-cms/memos/memo-005-contributor-and-maintainer-handoff.md).

When adding a timed UI transition, audit every entry path, including crafting,
collection selection and resume. Drive the injected clock through the owning
update function and check just before and at completion: no early gameplay or
statistics mutation, one completion, and safe cancellation. Run the surrounding
integration tests as well as the new focused tests; a launcher test alone does
not cover gallery crafting. See the
[PR 81 coverage finding](docs-cms/memos/memo-097-bubble-lab-cartridge.md#pr-81-timed-launch-integration-coverage).

When extending the save codec, update the centralized legacy-fixture offsets in
[tests/save_layout.h](tests/save_layout.h) alongside the versioned tail. Run old
migration and corruption cases as well as new-version round trips; malformed test
fixtures can otherwise hide whether a migration still works. See the
[tune-library finding](docs-cms/memos/memo-098-encountered-tune-library.md#validation-findings).

Keep generated game include paths inside the game firmware branch; the factory
profile deliberately skips those generators. SDL test executables that own their
entry point must follow the desktop host's `SDL_MAIN_HANDLED` convention and call
`SDL_SetMainReady()` before initializing SDL. In Studio browser tests, wait for
published UI state after reload: a newer live poll can supersede the awaited
request while images decode. These failures and their verification are recorded
in [memo-095](docs-cms/memos/memo-095-collectible-desktop-playtest.md#pr-77-ci-repairs).

Docs-cms holds decisions and evidence; copy an appropriate template and use the
next ID plus a new UUID. Preserve historical records. Reusable agent skills are
versioned under `.agents/skills`, but ordinary development does not require an
agent or installing personal skills.

## Jelli Art packaging and PR validation

When adding a shared Python dependency to the studio, update both the
[container copy list](tools/jelli-art/Containerfile) and the
[build-context allowlist](.dockerignore). Follow its transitive imports too:
checkout tests can import files that are absent from the shipped image.
Build the image and exercise its real entrypoint, health endpoint and data API
using the [image job's smoke procedure](.github/workflows/jelli-art.yml).
A successful image build alone does not establish successful startup.

The commit-time [container checker](scripts/check-studio-container.py) uses
Python's standard library without building an image or importing the app. It
checks COPY/context inclusion, known local Python imports (including transitive
imports), literal bundled browser files, and entrypoint/health-script presence.
It supports this repository's explicit COPY paths and literal-path allowlist;
unsupported syntax fails with a request to extend the checker. It does not
verify installed third-party packages, dynamic imports, operating-system
libraries or runtime startup. Keep the image smoke test in CI.

Run either hook independently:

```sh
./scripts/pre-commit run studio-types --all-files
./scripts/pre-commit run studio-container --all-files
python3 scripts/test-studio-container.py  # packaging-check regression fixtures
```

Before declaring a PR ready, inspect checks for its current head and merge
revision. Read the failed step and application logs before editing or retrying;
connection errors from a smoke probe can be a consequence of an earlier import
failure. If the workflow is still running, completed job logs are available via
`gh api repos/{owner}/{repo}/actions/jobs/{job_id}/logs`. Report pending checks
as pending and keep application fixes separate from infrastructure retries.
If a known fix has landed in the target branch, fetch it and verify whether the
PR includes that commit before duplicating the fix or refreshing baselines.
Synchronize the base when appropriate, inspect the resulting PR diff, and rerun
current-head checks; an earlier green PR does not establish this head is green.

For UI changes, inspect the pinned renderer's accessibility and screenshot
results as well as local browser tests. Reproduce suspected inherited failures
against the base in isolation, preserve the comparison evidence, and report
unresolved findings. A base reproduction explains provenance; it does not
waive a failing check. Update visual baselines only after reviewing the actual
rendered change using the [studio UI baseline procedure](tools/jelli-art/README.md#ui-tests).
Do not broaden accessibility exclusions or rewrite baselines merely to obtain
a passing run.

Changes to the C UI page enum also change Studio's embedded-game page picker,
whose catalog is read from [the engine header](include/jelli/pet_ui.h). Run the
focused game-view UI cases in the pinned renderer with screenshots enabled even
when no JavaScript changes. Review all three viewport ARIA snapshots; update
only the intentional options and retain unchanged screenshot baselines. See the
[tune-library coverage finding](docs-cms/memos/memo-098-encountered-tune-library.md#studio-game-page-snapshot-coverage).

After fixing a PR failure, record the failing run/revision, cause, remedy,
verification and remaining uncertainty in the owning memo. Recheck the pushed
revision and distinguish local verification from GitHub results. The container
startup failure and local Paint baseline findings for PR #74 are recorded in
[memo-088](docs-cms/memos/memo-088-jelli-art-responsive-navigation.md).

## Keep documentation grounded in source

Explain behavior, intent, ownership, and how to use or validate a feature.
Link to the owning code, content, or configuration for current limits, tuning,
catalogs, dependency pins, and test inventories instead of copying their values
or enumerating their entries. Prefer descriptive symbols and validation in the
implementation so a routine content change does not require a prose update.

Keep numbers when they define a requirement, a hardware or protocol contract,
an executable example, or a measured result. Record measurements with their
revision and context; preserve dated findings and accepted decisions as history.
Use generated output when readers need a complete current inventory.

## Optional firmware and hardware work

```sh
make esp-bootstrap
./scripts/esp doctor
make esp-build
./scripts/esp ports
```

Build success does not deploy anything. When deploying to your intended board,
use the discovered port and the separate flash/monitor commands in the
[hardware guide](docs/hardware.md#hardware-bring-up). Flashing overwrites firmware and
the partition table. Monitor startup, close the monitor with Ctrl+], and record
physical observations separately from serial logs. This project targets SKU
31261, not the similarly named 1.75C board.

## CI without private infrastructure

Local checks require no hosting credentials. Core workflows fall back to their
hosted configuration when private-runner variables are unset or false; there
is no automatic fallback for an already queued private job. A separate copy or
fork should leave both private-runner opt-ins disabled unless its own host policy
has been deployed and verified. Never copy App keys into the project.

On the current upstream private repository, fork PR workflows are disabled.
Coordinate contribution access with a maintainer; do not assume a fork will run
CI. A repository collaborator's branch can trigger private Linux jobs when the
upstream opt-in is enabled. See the runbook for the ownership/trust boundary.

## Fresh-environment acceptance

Use a new clone without copied `.tools`, build directories, or SDK components.
Follow the prerequisite and setup commands above, then run:

```sh
make test core-test hooks-check
./scripts/release-validate
make sanitize
```

For a firmware-capable environment, also run bootstrap, doctor, and `make esp-build`.
Do not flash a board merely to test contributor setup. Record OS/architecture,
compiler/CMake/Python versions, commit, whether host prerequisites were already
installed, results, and any missing checks. A fresh checkout on the maintainer's
Mac is useful evidence, but is not a fresh machine or proof of Linux/Windows
firmware support. The [handoff record](docs-cms/memos/memo-005-contributor-and-maintainer-handoff.md)
tracks the current acceptance boundary and recovery instructions.

## Board validation profiles

When changing ESP32 board support or experiment interfaces, build
`make esp-build`, `./scripts/esp validate`, and `./scripts/esp factory`. CI compiles all three against the pinned
SDK. These use separate generated SDK configurations and output directories.
Run `python3 tools/debug/check_firmware_profile.py normal build/esp32` and
`python3 tools/debug/check_firmware_profile.py power build/esp32-power` to verify
isolation; also run `python3 tools/debug/check_firmware_profile.py factory build/esp32-factory`.
Run `python3 tests/test_factory_capture.py` for result validation. Shared debug
protocol changes also require `make test`, `make core-test`, and `make sanitize`;
the parser test runs without a game engine or SDL. Source formatting and C analysis include experiments as normal code.
See [hardware experiments](docs/hardware.md#power-experiments-over-usb) for flashing
and serial evidence. CI compilation does not establish hardware test results.
