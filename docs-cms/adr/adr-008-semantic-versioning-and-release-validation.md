---
id: adr-008
title: Adopt Conventional Commits and Release Please
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [ci, releases, tooling]
project_id: jelligotchi
doc_uuid: 5ae32ffc-ee4e-47a9-9264-816729f6d785
---

# Context

The user requested the semantic-versioning pattern from the sibling auth
repository and end-to-end build and release health validation.

# Decision

Use Conventional Commits and Release Please's simple manifest strategy.
`fix:` increments patch, `feat:` increments minor, and `!` or a
`BREAKING CHANGE:` footer increments major, including before version 1.0.
A release PR maintains VERSION, CHANGELOG.md, and the manifest. Merging it
creates a vX.Y.Z tag and GitHub Release. Both CMake projects read VERSION and
track it as a configure dependency, so incremental builds refresh metadata.
Release validation tests version changes in an existing build directory.

Reuse GitHub's repository token. Explicitly dispatch validation for generated
release PRs and call the reusable release workflow after tag creation, since
bot-created PR and tag events do not trigger ordinary workflows. Validation
covers metadata, the core on Linux/macOS/Windows, SDL sanitizers, and quality
checks. Only after validation are source archives and checksums attached.
The GitHub Release record can exist before asset validation finishes; use the
workflow result and uploaded checksums as the publication health evidence.

The initial manifest baseline is 0.0.0; the first feature release is 0.1.0.
The initial releases contained source only. On 2026-10-08 the user requested
packaged native-game, editable-art, and preview downloads. The release workflow
now also builds a macOS ZIP with bundled SDL plus artwork and preview archives;
see [memo-017](../memos/memo-017-release-downloads-and-expressive-slice.md).
Firmware binaries and hardware flashing are not part of release publication.
Firmware compilation and physical display/touch verification remain separate.

# Approval basis

On 2026-10-07 Jacob requested auth's semantic-versioning pattern for Jelligotchi.
The workflow adaptation and source packaging are implementation choices.

# Consequences

Commits and PR titles must follow Conventional Commits. A failed validation
blocks asset publication and must be corrected or rerun before treating the
release as healthy. No host credentials or personal token enter the project.
A release never flashes the attached board automatically.

# References

- [Release configuration](../../release.yaml)
- [Validation script](../../scripts/release-validate)
- [Release Please token behavior](https://github.com/googleapis/release-please-action#other-actions-on-release-please-prs)
