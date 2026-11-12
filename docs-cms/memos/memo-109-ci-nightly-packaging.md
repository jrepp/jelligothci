---
title: Nightly packaging and PR timing baseline
author: Codex
created: 2026-11-12T08:45:58Z
tags: [ci, memo, releases, validation]
id: memo-109
project_id: jelligotchi
doc_uuid: 53d84dc1-901d-4930-b04a-bd45a9b06979
---

# Context

The owner requested fast PR checks, engine screenshot evidence, and repeatable
nightly release packaging. [ADR-008](../adr/adr-008-semantic-versioning-and-release-validation.md)
records the updated release operation. The [release workflow](../../.github/workflows/release.yml)
resolves a ref once, validates and packages that commit on each platform, and stores
nightly artifacts without publishing a release. The manual full run also builds
all firmware profiles. Release Please candidate dispatches use validation-only mode;
tag publication still requires the matching tag and an existing release.

PRs and pushes to main compile the shipping firmware; nightly and manual full
release runs compile normal, power and factory profiles. PRs run Linux core tests
on GitHub-hosted Ubuntu, Linux SDL/sanitizers and Studio tests. Full nightly and
manual releases retain macOS and Windows core tests; tag releases retain the
three-platform matrix. The Linux core, desktop, docs, hooks and Studio checks
remain PR checks.
Platform packaging and Windows-specific SDL paths are verified nightly and at
release instead of by the PR packaging job. Pinned browser tests and the Studio
image remain separate PR checks. Browser files are distributed deterministically
across three isolated runners with exact-once file coverage; `Studio UI tests`
aggregates their results. Each shard retains its selected file list, test timings,
screenshots and accessibility reports for 14 days, including successful runs.
Image build and Studio tests run concurrently. Image
publication remains gated by the image smoke and both Studio test jobs. Branch
pushes no longer duplicate PR runs. On 2026-11-12 the maintainer removed the
macOS and Windows core contexts from the required-check ruleset while retaining
the other protections; the full platform matrix still runs in release validation.
The first local pinned-container collection on the sharded implementation found
149, 107 and 89 cases respectively, 345 total; file-level balancing uses source
size as an estimate, so actual timing needs measurement on the next CI run.
In the pinned Linux ARM container, selected game-view checks passed 4/4 on shard
0, persistent-form checks passed 2/2 on shard 1, and the airplane ambient-motion
check passed 1/1 on shard 2. These focused samples and collection do not establish
a complete green browser run; inspect all three final PR jobs and the aggregate.

# Baseline evidence

The following timings are a historical snapshot of the successful PR #97 head
`cfc8e6d` before the split, from GitHub Actions on 2026-11-12. Queue is job creation
to start; execution is start to completion. Dependencies mean a later job's queue
starts only after its prerequisites finish.

| Job | Queue | Execution | Run |
| --- | ---: | ---: | --- |
| Release validation | 2m 51s | 5m 37s | [43181689777](https://github.com/jrepp/jelligothci/actions/runs/43181689777) |
| Windows packaging | 16s | 10m 21s | same release run |
| macOS release package | 3s | 6m 37s | same release run |
| Studio UI tests | 14s | 12m 03s | [43181689785](https://github.com/jrepp/jelligothci/actions/runs/43181689785) |
| Normal firmware | 1m 58s | 4m 40s | [43181689892](https://github.com/jrepp/jelligothci/actions/runs/43181689892) |
| Desktop tests | 43s | 5m 31s | [43181689872](https://github.com/jrepp/jelligothci/actions/runs/43181689872) |

The release workflow took 25m 46s from job creation at 08:17:13 UTC to final
package completion at 08:42:59 UTC, with Windows followed by macOS on its critical
path. The Studio UI suite remains a potentially long parallel PR check; these
measurements do not predict future runner queues.

# Verification after landing

Record the commit, PR run links, and each job's queue and execution time for a
representative code change. Compare time to all required checks and to completion
of the Studio UI and normal firmware checks against this baseline; do not infer
improvement from fewer jobs alone. Run one manual full release validation and inspect
its source SHA, all firmware profiles, artifact names, archive checksums and
retention. Inspect the first scheduled run independently. A tag release must continue
to validate the original tag against the pinned commit before publication. Keep
host build, firmware build, and physical device evidence separate.

## Shard shell correction

On PR #101 revision `1830808`, [shards 1 and 2](https://github.com/jrepp/jelligothci/actions/runs/43189414232)
failed before collection because container job steps defaulted to `sh`, which
rejects `set -o pipefail`. The browser assertions did not run. The test step now
explicitly uses Bash so the captured test log retains the pipeline's failure
status; the next current-head CI run must establish full-suite results. The
pinned-container shard samples and exact-once collection checks above predate
this CI shell fix and do not establish the revised workflow's outcome.
