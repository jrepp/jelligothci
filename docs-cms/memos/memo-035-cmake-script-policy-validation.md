---
id: memo-035
title: CMake script policy validation across versions
author: Codex
created: 2026-10-10T07:46:39Z
tags: [build, ci, testing]
project_id: jelligotchi
doc_uuid: 17ad5baf-e8be-46de-9efe-6fa4dad7cde9
---

# Observation

During review of the creature-content PR, Linux desktop and release-validation
CI failed `collection_content` despite local CMake 4 tests passing. The test
writes a small driver and invokes `cmake -P`. That script had no minimum-version
declaration, so it did not inherit the main project's policy settings.
`JelliCollection.cmake` now uses `IN_LIST`, whose CMP0057 policy defaults differ
between these CMake versions. CI reported unknown arguments to `if()`.

# Remedy and verification

The generated test driver now starts with `cmake_minimum_required(VERSION 3.21)`,
matching the project baseline before including the collection generator. No
warning or catalog validation was disabled. The test remains responsible for
establishing its own standalone execution context.

Reproduced the original failure with repository-local uv and `cmake==3.28.4`.
After the fix, the same authored-catalog test passed on CMake 3.28.4 and local
CMake 4.4.3. The additional version is a compatibility probe; toolchain.env was
not changed. Full local desktop/core/sanitizer suites had already passed at the
feature head; CI must rerun on the repair and dependent branches.

Reusable guidance is beside the generated driver in the owning test. When a
CMake helper is tested via script mode, declare the same minimum version as its
normal project entry point. A passing newer compiler or build tool does not
prove compatibility with the supported minimum or CI's installed version.

# References

- [Standalone catalog test](../../tests/test_collection_content.py)
- [Collection generator](../../cmake/JelliCollection.cmake)
- [Project baseline](../../CMakeLists.txt)
