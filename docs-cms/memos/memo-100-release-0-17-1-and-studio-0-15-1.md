---
title: Release 0.17.1 and Jelli Art 0.15.1 verification
author: Codex
created: 2026-10-21T09:11:03Z
tags: [memo, release, validation]
id: memo-100
project_id: jelligotchi
doc_uuid: fb6bcc19-9138-41d0-b8a0-07e1ab03d8ed
---

# Overview

This memo records the release train that culminated in application 0.17.1 and
Jelli Art 0.15.1. The 0.17.0 release delivered the expanded synth palette,
encountered-tune library, and PCM measurement tool. Its optional Studio UI run
exposed three stale accessibility snapshots for tune playback. PR #87 updated
the snapshots, and 0.17.1 carries that fix. CI, downloaded artifacts, source
builds, and hosted Studio image evidence are recorded separately from physical
device validation.

# Scope and merged changes

The 0.17.0 release includes the expanded synth palette and original MOD
arrangements from [PR #83](https://github.com/jrepp/jelligothci/pull/83),
merged as [`637bbd1`](https://github.com/jrepp/jelligothci/commit/637bbd15dd50099db1d976e8d5a71869e33be8bc);
the encountered-tune library from [PR #84](https://github.com/jrepp/jelligothci/pull/84),
merged as [`306cecb`](https://github.com/jrepp/jelligothci/commit/306cecbf77e6e19693a18e032bf8e7c3bef8f294);
and the lossless PCM measurement tool and memo from [PR #85](https://github.com/jrepp/jelligothci/pull/85),
merged as [`f1b5b2e`](https://github.com/jrepp/jelligothci/commit/f1b5b2e26e7e53b7511fc01ba52400c54f85a48d).
The tune-library implementation and strict-GCC narrowing fix are documented in
[memo-098](memo-098-encountered-tune-library.md), and the PCM experiment is
recorded in [memo-101](memo-101-lossless-music-pcm-packing.md).

[PR #86](https://github.com/jrepp/jelligothci/pull/86) set app version 0.17.0
and Studio version 0.15.0. Its candidate head was
[`519b032`](https://github.com/jrepp/jelligothci/commit/519b032c0a281be69cf8a0a6da6864bc3adc33b3),
merged as [`8d25d7c`](https://github.com/jrepp/jelligothci/commit/8d25d7c70172b8f9dd9571a76769d50d09e91423).
The generated changelog listed the same feature twice because it counted both
original commits and later merge commits; the release candidate changelog was
edited to retain one entry per change.

[PR #87](https://github.com/jrepp/jelligothci/pull/87) added the `tune play`
option to the game accessibility snapshots for desktop, tablet, and phone. Its
head [`3953b77`](https://github.com/jrepp/jelligothci/commit/3953b773c552f44a6a1fb609b6fe64c3881f5dac)
was merged as [`3e9c128`](https://github.com/jrepp/jelligothci/commit/3e9c128017b5366eb3f9304e93503b733c03198d).
The implementation and the accessibility-test guidance are in the PR diff and
[memo-098](memo-098-encountered-tune-library.md).

[PR #88](https://github.com/jrepp/jelligothci/pull/88) set app version 0.17.1
and Studio version 0.15.1. Its deduplicated candidate head was
[`f0825d3`](https://github.com/jrepp/jelligothci/commit/f0825d303198599c24295a11cfd95a6e2c9d3202),
merged as [`28d3ca0`](https://github.com/jrepp/jelligothci/commit/28d3ca07565a9a4fc97f49b1da52cd7df5e5a078).

# Validation and findings

The corrected PR #84 head
[`cd8d2c0`](https://github.com/jrepp/jelligothci/commit/cd8d2c064c62ab831ae1e10ac8b1bf2fc6fc000f)
passed release validation, core, desktop, Studio, hooks, documentation, and image
checks before merge. An earlier head failed strict GCC compilation in
`core/pet_tune_library.c`: the compound elapsed-time addition was promoted to
`int`, so assigning the result to the `uint16_t` timer triggered
`-Werror=conversion`. The fix clamps elapsed time before narrowing the sum.
The reproduction and code-level finding are linked in memo-098. An outdated
Studio preview run also failed because the binary could not build; that same
compiler issue was corrected on the merged head.

PR #86 required checks passed at its exact candidate head, including release
validation run
[39579237927](https://github.com/jrepp/jelligothci/actions/runs/39579237927)
(6m52s), desktop tests, core checks on all three platforms, Studio tests, image,
hooks, and docs. The optional full Studio UI job in run
[39579237822](https://github.com/jrepp/jelligothci/actions/runs/39579237822)
found three stale ARIA snapshots: the actual game menu included `option "tune
play"` on desktop, tablet, and phone, while each baseline ended at
`collectible play`. The other 314 of 317 UI cases passed. This was a snapshot
omission, not a game-preview build failure.

PR #87 passed its exact-head full Studio UI suite in run
[39581471670](https://github.com/jrepp/jelligothci/actions/runs/39581471670)
(317 cases), along with release validation in 5m47s, desktop tests, all core
platforms, Studio tests, image, hooks, and docs. PR #88's exact-head required
checks passed, including desktop runs in 3m12s and 4m50s and release validation
in 4m02s. Its generated patch changelog also duplicated the original fix commit
and its merge commit; the candidate was edited to show the merged change once.

The final post-merge run
[39584485744](https://github.com/jrepp/jelligothci/actions/runs/39584485744)
passed release validation, all core platforms, Studio tests, the full Studio UI
suite, the image job, and Windows packaging. Native release packaging was still
in progress when last checked. The final publication workflow later completed
successfully, including native packaging and all release jobs. This final UI run
confirms the snapshot correction shipped with the patch release.

# Candidate and superseded-run handling

A release candidate's three-dot diff only reports what differs from its base;
it does not prove the candidate tree contains recent merges from the base.
During candidate review, the first 0.17.0 candidate head did not contain PR #85's
PCM tool or memo, even though its diff and mergeability looked plausible. The
refreshed candidate was checked directly for both files. The 0.17.1 candidate
was also checked at its exact ref: the PCM tool and memo-101, plus all three
`tune play` accessibility rows, were present; only the five release metadata
files differed from current main.

Obsolete validation and image dispatches were canceled after replacement runs
covered their heads. The old #84/#85 cancellation records are
[39576303625](https://github.com/jrepp/jelligothci/actions/runs/39576303625),
[39576815656](https://github.com/jrepp/jelligothci/actions/runs/39576815656),
and [39576815575](https://github.com/jrepp/jelligothci/actions/runs/39576815575).
Pre-release 0.17.0 candidate dispatches
[39579120359](https://github.com/jrepp/jelligothci/actions/runs/39579120359)
and [39579116075](https://github.com/jrepp/jelligothci/actions/runs/39579116075)
were also canceled; the image dispatch's GHCR publish step had been skipped.
After PR #87 merged, its standalone image and release-validation dispatches
[39583603929](https://github.com/jrepp/jelligothci/actions/runs/39583603929)
and [39583601546](https://github.com/jrepp/jelligothci/actions/runs/39583601546)
were canceled in favor of the final publication run. Those cancellations are
not counted as passes or failures of the final candidate.

GitHub CLI requests hit HTTP 403 rate limiting during publication monitoring. One poller then owned release polling and waited until the reset time from the actual failed response before retrying. A separate rate-limit response gave a different reading, so use the failing request’s response headers and avoid repeated polling from parallel agents.

Release-candidate changelog commits were normally signed through the configured
SSH signer backed by 1Password. On the 0.17.1 candidate, signing failed twice
with `1Password: agent returned an error`. Repository rules and documentation
did not require commit signatures; one changelog-only commit used the
per-command `git -c commit.gpgsign=false` override. No global or repository
signing settings were changed, and hooks remained enabled.

# Publication and artifact verification

The intermediate `v0.16.0` / `jelli-art-v0.14.0` release was published from
[run 39574650358](https://github.com/jrepp/jelligothci/actions/runs/39574650358),
which passed its release validation, packaging, Studio UI, and image jobs. Its
Studio image digest was
`sha256:bb7932d6ee055d06326966e76ae9e7d44fe96ebeb4ec57dae4e0bd6f4bb67fc3`.

The initial `v0.17.0` publication completed package and image jobs in run
[39580436412](https://github.com/jrepp/jelligothci/actions/runs/39580436412).
It attached seven assets, including macOS and Windows packages, browser preview,
source art, source archive, and `SHA256SUMS`. The optional Studio UI job failed
for the three stale snapshots described above; the patch release below passed
that suite.

The `v0.17.1` and `jelli-art-v0.15.1` tags were created at 10:25 UTC on
2026-10-21. GHCR package metadata identifies
`ghcr.io/jrepp/jelli-art:0.15.1` as
`sha256:04d624329acafde59c980619a0b168be41e86a0a90d4f06df8903129ab519229`.

The final publication workflow
[39584485744](https://github.com/jrepp/jelligothci/actions/runs/39584485744)
completed successfully with every job green. It uploaded all seven release
assets. The six payloads and their downloaded sizes were:

| Asset | Bytes |
| --- | ---: |
| `jelligotchi-0.17.1-macos-arm64.zip` | 2,413,235 |
| `jelligotchi-0.17.1-windows-x64.zip` | 2,180,587 |
| `jelligotchi-0.17.1-preview.zip` | 86,869 |
| `jelligotchi-0.17.1-preview.html` | 209,493 |
| `jelligotchi-0.17.1-source-art.zip` | 993,861 |
| `jelligotchi-0.17.1-source.tar.gz` | 3,921,321 |

The downloaded `SHA256SUMS` verified all six payloads. The source archive's
SHA-256 is
`3ab87dc0a6fba089ce4a86b2f880ec381c5c170c04b4e1ffac9662eabc9c9479`. It was
extracted outside the checkout and built from a fresh default desktop Release
CMake configuration; all 72 native tests passed in 14.44 seconds.

The released PCM catalog regression tests passed 4/4. The experimental pack
emitter used the fresh native verifier successfully and produced a 5,409-byte
JPAK; native verification compared all 17 blocks and 5,392 decoded bytes.
For the fifteen-track catalog, 71 references contain 16 unique PCM blobs
(3,104 bytes). Existing raw/LZ4 per-sample selection accounts for 2,545 PCM
payload bytes and a 5,409-byte complete pack. The experimental per-sample model
is 1,976 PCM payload bytes and 4,840 complete modeled bytes; it remains an
offline measurement, not a shipping codec or player format (see
[memo-101](memo-101-lossless-music-pcm-packing.md)).

The downloaded macOS archive was extracted with `ditto`. Its `Info.plist`
reports version 0.17.1, `codesign --verify --deep --strict` passed, and a dummy
SDL video/audio launch rendered 64 headless frames and exited successfully.
No save data was used for this smoke test.

# Validation limits

No new ESP-IDF firmware build, device flash, display, or touch validation was
performed for the final release. Host CI and desktop artifact smoke tests do
not establish physical device behavior.

# References

- [PR #83: synth palette and MOD arrangements](https://github.com/jrepp/jelligothci/pull/83)
- [PR #84: encountered tune library](https://github.com/jrepp/jelligothci/pull/84)
- [PR #85: PCM packing measurement](https://github.com/jrepp/jelligothci/pull/85)
- [PR #86: 0.17.0 / 0.15.0 release candidate](https://github.com/jrepp/jelligothci/pull/86)
- [PR #87: tune-play accessibility snapshots](https://github.com/jrepp/jelligothci/pull/87)
- [PR #88: 0.17.1 / 0.15.1 release candidate](https://github.com/jrepp/jelligothci/pull/88)
- [PR #80: Bubble Lab and background music](https://github.com/jrepp/jelligothci/pull/80)
- [PR #81: cartridge launch and activity-audio follow-up](https://github.com/jrepp/jelligothci/pull/81)
- [PR #82: intermediate 0.16.0 release candidate](https://github.com/jrepp/jelligothci/pull/82)
- [Bubble Lab implementation evidence](memo-097-bubble-lab-cartridge.md)
- [Encountered tune implementation evidence](memo-098-encountered-tune-library.md)
- [Expanded synth-bank measurements](memo-099-expanded-synth-bank-and-arrangements.md)
- [Jelli Art navigation and asset reload findings](memo-088-jelli-art-responsive-navigation.md)
- [PCM packing experiment](memo-101-lossless-music-pcm-packing.md)
