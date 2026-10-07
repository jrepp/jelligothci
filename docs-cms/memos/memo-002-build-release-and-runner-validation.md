---
id: memo-002
title: Build release and private runner validation
author: Codex
created: 2026-10-07
tags: [build, ci, releases, runners]
project_id: jelligotchi
doc_uuid: 0daf606a-97f2-4bd1-97b7-a2d49186ab10
---

# Scope

The private repository is [jrepp/jelligothci](https://github.com/jrepp/jelligothci),
using the owner's requested GitHub spelling. The local application remains
Jelligotchi. This record covers build/release validation and private Linux
runner onboarding on 2026-10-07; it does not establish physical board behavior.

# Runner routing

Hosting PRs 155 and 157 merged the repository allowlist and rollout evidence.
The signed, pinned router installer deployed the Artemis policy without changing
its release, image, pool resources, or credentials. Linux canary 37693998919
passed on router-small (2 CPUs, 2 GiB). At 22:09 UTC, router state, GitHub runner
registration, the Podman container, generated Quadlet, and workspace confirmed
cleanup. Router errors and storage blocking were zero.

PRIVATE_LINUX_RUNNERS is enabled. Core portability run 37694326274 passed with
Linux on Artemis and macOS/Windows on hosted runners. The Mac Studio was offline;
its allowlist is committed in hosting, but not deployed. PRIVATE_MACOS_RUNNERS
remains unset. Do not queue private macOS jobs until onboarding is completed.

The canary exposed missing CMake in the shared image and an inherited
UV_INSTALL_DIR overriding the local bootstrap path. CI now installs pinned
CMake through local uv, and the wrapper explicitly fixes both uv installer paths.
A fresh-bootstrap regression check passed with conflicting inherited values.

# Release validation

Conventional Commits produced release PR 1 for 0.1.0, then PR 2 for 0.1.1 from a
fix commit. Release Please created both version tags through the normal flow.
External actions use full commit SHAs with exact version comments. Hosted OS
labels are explicit; GitHub still updates their underlying VM images. The
workflow-pin gate rejects floating action tags and moving OS aliases.

An existing ESP32 build initially retained stale version metadata after VERSION
changed. Version 0.1.1 adds VERSION as a configure dependency in both CMake
projects. An automated regression test checks the desktop build. Local testing
also changed VERSION in an existing ESP32 build tree, verified regeneration,
and restored it successfully without deleting build directories.

The validation covers Linux/macOS/Windows core compilation and tests, strict
warnings, SDL smoke tests with address/undefined-behavior sanitizers, static
analysis, C size/complexity limits, documentation, and release metadata. One
hosted desktop job timed out downloading packages from the Azure Ubuntu mirror;
its retry passed. Dependency downloads now have bounded retries and timeouts.

[Release workflow 37696540467](https://github.com/jrepp/jelligothci/actions/runs/37696540467)
completed successfully for
[v0.1.1](https://github.com/jrepp/jelligothci/releases/tag/v0.1.1).
Its published source archive and SHA256SUMS were downloaded, verified, extracted
outside the Git checkout, and used for a fresh Release-mode CMake build. Both
the engine tests and SDL headless smoke test passed from that archive.

The local ESP32 build also passed for v0.1.1. ESP-IDF project metadata and
esptool's application descriptor both reported app version 0.1.1 and ESP-IDF
v5.5.5. The 558080-byte application image had a valid checksum and validation
hash. This is compilation/image verification only; no flashing was performed.

# Limits and follow-up

Release assets contain source and SHA256 checksums, not prebuilt executables or
firmware. ESP32 firmware compilation is local; CI does not yet compile that port.
The attached board has not been flashed or used to verify display and touch.

# Recovery

Disable PRIVATE_LINUX_RUNNERS to return core jobs to hosted Linux. Host allowlist
rollback is documented in the hosting memo. Revalidate a source ref using the
release workflow's manual dispatch; publishing assets requires publish=true and
a matching existing version release. Preserve published tags and issue a patch
release for fixes. Releases never flash hardware automatically.

# References

- [Hosting onboarding record](https://github.com/jrepp/t1-hosting/blob/main/docs-cms/memos/memo-037-jelligothci-runner-onboarding.md)
- [Successful Linux canary](https://github.com/jrepp/jelligothci/actions/runs/37693998919)
- [Core matrix using private Linux](https://github.com/jrepp/jelligothci/actions/runs/37694326274)
- [Semantic-versioning decision](../adr/adr-008-semantic-versioning-and-release-validation.md)
