---
id: adr-006
title: Build and test the core across desktop platforms in CI
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [ci, portability, testing]
project_id: jelligotchi
doc_uuid: 8ffedf92-5e80-415d-9c7a-1ff6c6ce5376
---

# Context

The engine is intended to be portable C, but a successful build on one
developer's Mac does not establish portability across host compilers and
operating systems. The existing desktop CI covers Linux with SDL and sanitizers.

# Decision

Add GitHub Actions coverage that builds and tests the core independently of
SDL and ESP-IDF. The initial matrix covers Linux, macOS, and Windows, using the
default native compiler on each runner: GCC, AppleClang, and MSVC respectively.

Configure CMake with `JELLI_BUILD_SDL=OFF` and testing enabled. Build in Release
mode and run CTest. The tests use checks that remain active in Release builds.
Treat compiler warnings as errors and fail if no tests are discovered. Let all
matrix jobs finish so one platform's failure does not hide another's results.

Keep the Linux SDL/sanitizer job as separate coverage. This matrix does not
install SDL, bootstrap ESP-IDF, flash a board, or validate a graphical host.

# Approval basis

During the 2026-10-07 session, Jacob requested GitHub workflows to validate the
cross-platform build of the core engine. The three-OS matrix implements that
request; it does not extend the ESP32 bootstrap's supported host platforms.

# Consequences

- Compiler and platform assumptions in shared code become visible in CI.
- A core-only job can reveal accidental platform-library dependencies.
- Three runner jobs add CI time and depend on GitHub-hosted tool availability.
- Hosted runner images and default compilers can change. These are portability
  checks, not bit-for-bit reproducible release builds.
- Passing this matrix does not replace ESP32 compilation or hardware checks.

# Alternatives

Linux-only coverage would leave other compilers unchecked. Testing only the SDL
executable would mix core portability with graphical runtime dependencies. An
ESP32-only cross-compile would not exercise the core on multiple desktop hosts.

# References

- [Portable core boundary](adr-001-portable-c-core-and-host-adapters.md)
- [Core CI workflow](../../.github/workflows/core.yml)
- [Desktop and sanitizer CI](../../.github/workflows/desktop.yml)
- [CMake targets](../../CMakeLists.txt)
- [Core tests](../../tests/test_engine.c)
