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

These are scoped observations, not guarantees for every machine. Physical
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

For the complete macOS/Linux workflow, install a C compiler and SDK headers,
CMake 3.21+, Ninja, Make, Bash, curl, and Python 3.10-3.13 with venv support.
SDL2 2.0.18+ is needed for the desktop host, not the core. Examples:

```sh
# macOS: Xcode command-line tools must be installed as well.
brew install cmake ninja sdl2 python@3.13
# Ubuntu/Debian: choose an available Python 3.10-3.13 for ESP-IDF.
# sudo apt-get install build-essential cmake ninja-build libsdl2-dev git curl python3 python3-venv
```

Distribution packages may supply a newer unsupported Python. Check its version;
set `JELLI_PYTHON` to a compatible executable for ESP-IDF. Package-manager tools
and host compilers are prerequisites, not artifacts managed by this repository.
Pinned Python utilities and ESP-IDF tools are managed locally by the wrappers.

```sh
make hooks-install
make test          # expect engine and headless SDL tests to pass
make core-test     # verifies the core without SDL
make hooks-check   # C formatting, analysis, size limits, docs, version/action pins
make run           # three shapes; click/Space pauses, Escape quits
```

Hooks are installed per checkout. Stage newly added source files before checking
all tracked files; Git-based checks cannot discover untracked files. Tools
usually print their first-use download progress into the terminal.

## Core-only path, including Windows

With CMake and a C compiler installed, the following commands need neither Bash,
Make, Ninja, SDL, nor ESP-IDF. On Windows use a shell with the MSVC tools available.

```sh
cmake -S . -B build/core-native -DJELLI_BUILD_SDL=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-native --config Release --parallel 2
ctest --test-dir build/core-native --build-config Release --output-on-failure --no-tests=error
```

Expect the engine test to pass. Native Windows core CI does not establish that
the shell wrappers, Python lint wheels, SDL, or USB workflow work on Windows.
Use a supported macOS/Linux environment for those checks.

## Make and review a change

Create a branch and keep the change within the current shapes MVP. Read the
[architecture and ownership contracts](README.md#boundary-between-engine-and-host)
and [project instructions](AGENTS.md). The core receives memory, time, input,
and output from hosts; it must not gain SDK dependencies or heap allocation.

| Change | Validation before review |
| --- | --- |
| Core, public contract, rendering | `make test core-test sanitize`; inspect rendering changes |
| ESP32 adapter or shared interface | Above as relevant, plus `make esp-build`; physical checks when behavior changes |
| C size gate | `./scripts/c-lint self-test` |
| Release/build version behavior | `./scripts/release-validate` (includes incremental-version regression) |
| Workflows | `python3 scripts/check-workflow-pins.py` and actionlint when available |
| Documentation | `make docs-check`, `make docs-fix`, review repairs |

Run the relevant hooks and provide the exact revision, commands, outcomes, and
remaining gaps in the PR. Use Conventional Commit titles (`fix:`, `feat:`,
`docs:`, `ci:`); mark breaking changes explicitly. Squash merges use the PR title
as the release input. Do not hand-edit only one version file or bypass failing
checks to land a change. Automated enforcement and access settings are described
in the [maintainer runbook](docs-cms/memos/memo-005-contributor-and-maintainer-handoff.md).

Docs-cms holds decisions and evidence; copy an appropriate template and use the
next ID plus a new UUID. Preserve historical records. Reusable agent skills are
versioned under `.agents/skills`, but ordinary development does not require an
agent or installing personal skills.

## Optional firmware and hardware work

```sh
make esp-bootstrap
./scripts/esp doctor
make esp-build
./scripts/esp ports
```

Build success does not deploy anything. When deploying to your intended board,
use the discovered port and the separate flash/monitor commands in the
[hardware guide](README.md#hardware-bring-up). Flashing overwrites firmware and
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
