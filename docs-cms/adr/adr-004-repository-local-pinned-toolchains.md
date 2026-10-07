---
id: adr-004
title: Manage pinned development tools locally within the repository
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [build, esp32, portability, tooling]
project_id: jelligotchi
doc_uuid: 8bb10c5c-9b7d-4d1a-bba4-dc1dfd9082f5
---

# Context

ESP32 development needs SDK sources, compilers, Python tools, and board
components. A developer should be able to provision them from this checkout
without committing tool binaries or relying on a globally configured SDK.

# Decision

Track version pins and setup scripts in Git. Download SDKs and tools into
ignored local directories. Provide commands to bootstrap, sync, build, flash,
and monitor. Resolve paths from the repository location and leave shell
profiles unchanged.

The current workflow stores tools and caches under `.tools/` and output under
`build/`. `toolchain.env` pins the ESP-IDF release and commit as well as uv and
Docuchango versions. The board manifest and component lock file are tracked;
downloaded components and generated `sdkconfig` files are ignored. Intentional
board settings live in `sdkconfig.defaults`.

Sync restores the tracked SDK pin. Upgrades require explicit pin changes,
dependency resolution when needed, and validation. Build and flash remain
separate commands. Discover serial ports instead of embedding one machine's
device path in the project.

# Approval basis

Jacob requested a self-hosting repository that does not commit ESP32 tools but
can sync, update, and use the toolchain through a portable local workflow. He
also requested pinned uv and documentation tooling in the 2026-10-07 session.

# Consequences

- A checkout records which SDK and direct tool versions it expects.
- Large downloads, generated files, and machine-specific paths stay out of Git.
- Initial setup still needs network access and documented host prerequisites.
- Local caches and virtual environments can contain absolute paths. Recreate
  them when moving the checkout; portability applies to the workflow.
- Version pins do not imply a fully hermetic build. Host utilities and all
  Python transitive dependencies are not frozen by this ADR.

# Alternatives

A global ESP-IDF installation would make the checkout depend on workstation
state. Committing the SDK and compiler would conflict with the explicit request
to keep those tools out of Git. Floating release selection on each sync would
make upgrades implicit.

# References

- [Toolchain pins](../../toolchain.env)
- [ESP32 workflow](../../scripts/esp)
- [Component manifest](../../ports/esp32/main/idf_component.yml)
- [Component lock](../../ports/esp32/dependencies.lock)
- [Ignored artifacts](../../.gitignore)
- [Documentation tooling](adr-005-docs-cms-with-pinned-docuchango.md)
