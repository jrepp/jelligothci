---
title: Game stability and fun audit
author: Codex
created: 2026-10-10T09:10:28Z
tags: [gameplay, memo, stability, testing]
id: memo-039
project_id: jelligotchi
doc_uuid: c88350c7-abbc-4323-b12a-604f44ad8cf4
---

# Scope and release decision

Reviewed the remaining worktree (PR #16), then inspected game commands, live and
offline time, behavior requests, save decoding and storage, rewards, gestures,
render invalidation, SDL startup and ESP32 persistence boundaries. This is a
focused code and regression audit, not an exhaustive proof or a hardware playtest.
PR #16 passed desktop, core, sanitizer, static-analysis and firmware checks;
its debug integration test passed ten consecutive sanitizer runs after fixing
the startup race described in [memo-038](memo-038-debug-startup-review.md).
The merged worktree and redundant branches were archived and removed. Newly
created, locked Jelli Art worktrees belong to active work and were preserved.

The refreshed 0.5.0 / Jelli Art 0.3.0 release was held when the broader audit
reproduced a game-freezing command sequence. The fixes below must land before
release validation is dispatched against the final release candidate.

# Findings and remedies

| Priority | Reproduction and effect | Remedy and evidence |
| --- | --- | --- |
| P1 | Start a timed moment, then Care. The activity becomes CARING but `moment` remains nonzero, violating `jelli_game_valid`. Simulation and save encoding then refuse the game. | Starting a replacement activity clears its previous moment. A regression covers every timed moment, saves during Care, reloads, and verifies recovery completes. It failed before the fix. |
| P2 | Potty urge crosses its threshold while Reading or during offline catch-up. The one-shot event is discarded, leaving no later request. | Recheck outstanding urgency once per live behavior step, before incidental stimuli. Busy, sleep and cooldown rules still apply; offline steps only expire states. Regressions cover busy and offline thresholds, waking, cooldown, answering, and no repeated request after relief. The busy regression failed before the fix. |
| P2 | Tap the mess again while cleaning. The command returns BUSY but plays the success cue. | Queue confirmation and saving only after successful cleanup commands. The regression failed before the fix. |
| Coverage | The randomized command test selected only the original eleven commands. | Expand it to all public commands and 2,000 deterministic steps, mixing plausible values and rejected inputs. This exposed the Care/moment invalid state at step 978. |

No save schema, content balance, heap allocation or physical board configuration
changes are required. Urgency is already persisted in `potty`, so the request fix
needs no new pending-state field or queue capacity.

# Validation

Local validation passed: `make test` (52/52), `make core-test` (25/25),
`make sanitize` (52/52), `make esp-build`, and `make hooks-check`. Documentation
passed `make docs-check` followed by `make docs-fix`; the sole repair sorted the
new memo tags. CI must also test the actual updated PR and release candidate,
including Linux, macOS, Windows and packaged downloads.
Existing suites cover save corruption and migration, bounded elapsed-time
catch-up, render damage and stride, scene interaction, asset reload rollback,
and debug reconnect/capture. Compiled firmware does not verify physical
rendering, touch, power loss, RTC continuity or frame timing.

# Release verification

PRs #16 and #17 are merged. After the audit fixes and release-note deduplication,
[PR #15](https://github.com/jrepp/jelligothci/pull/15) passed all checks, including
Windows and macOS packages and the Jelli Art image. Both `v0.5.0` and
`jelli-art-v0.3.0` resolve to `c370fab31efdb5a8ade82a3cba2d8bd464560dcc`.
The [publication workflow](https://github.com/jrepp/jelligothci/actions/runs/38044516169)
completed successfully, including the image smoke test and semver tag pushes.

Downloaded all seven game release assets (six artifacts plus `SHA256SUMS`);
all six checksums matched. Extracted the published source archive outside the
checkout, configured a Release core-only build, compiled, and passed 25/25 tests.
The downloaded macOS application completed the 650-frame headless pet demo;
its snapshot matched the reviewed local demo output. Windows packaging and
execution tests passed in CI; no local Windows execution was performed.

Jelli Art `ghcr.io/jrepp/jelli-art:0.3.0` publication and container smoke testing
are evidenced by the successful CI image job. An independent local registry
manifest inspection did not complete and was interrupted; a fresh local image
pull was not verified. Physical ESP32 validation remains outstanding.

The removed worktree's bundles, regression failures, passing local logs,
release downloads, source-build log and image publication log are archived in
`/Users/jrepp/d/jelligotchi-cleanup-20261010-013938/`. Concurrent Jelli Art worktrees
were preserved, including a new studio integration worktree created during
this pass; they are not part of this release.

# Follow-up priorities

1. **Save/content compatibility before reordering authored states.** Behavior,
   cooldown and moment fields persist array positions (`core/save_tail.c`),
   while authored names resolve to positions during generation. A valid reorder
   can reinterpret an existing save. Add stable serialized IDs or an explicit
   content migration policy and fixtures before shipping such catalog changes.
2. **Hardware acceptance.** Exercise BUBBLE requests, Care during Reading,
   cleanup animation, saves across power cycles and RTC/offline recovery on the
   physical board. Measure display-copy and NVS latency with animation active.
3. **Player feedback and pacing.** Playtest a complete feed, read, request,
   potty and cleanup loop. Check whether the Health-ring potty action is easy
   to find and whether request captions survive competing reward feedback.
   Measure before changing durations or adding more requests/species behaviors.
4. **Desktop save latency.** Storage still performs synchronous flush/sync and
   verification on the host loop. Measure stalls on slow storage before choosing
   a bounded worker design; existing two-slot recovery tests do not measure frame
   latency or directory-entry durability after sudden power loss.

The historical backlog in [memo-008](memo-008-playable-pet-mvp-and-polish-backlog.md)
is context, not a current missing-feature list: ESP32 NVS/RTC support and several
content systems now exist. Revalidate each old item before scheduling it.

# References

- [Behavior proposal](../rfcs/rfc-005-stimulus-driven-creature-behaviour.md)
- [Maintainer recovery and release procedure](memo-005-contributor-and-maintainer-handoff.md)
- [Offline timeout regression](memo-037-behavior-offline-review.md)