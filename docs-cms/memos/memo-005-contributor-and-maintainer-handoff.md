---
id: memo-005
title: Contributor handoff and maintainer operations
author: Codex
created: 2026-10-07
tags: [ci, contributors, operations, portability]
project_id: jelligotchi
doc_uuid: 9a6cb479-c640-4a17-8660-e5d57afc2e28
---

# Purpose and ownership

The [contributor guide](../../CONTRIBUTING.md) is the human entry point. This
runbook captures operations and account settings that a clone cannot reproduce.
Commands below are for an authorized maintainer; documenting them does not
request or perform permission, policy, or host changes.

| Responsibility | Owner and source of truth | Access needed |
| --- | --- | --- |
| C core, SDL, ESP32 adapter, tool pins, docs | This repository | Repository read access to build; contribution access to propose changes |
| Workflow code and runner labels | This repository | Repository maintainer for workflow/release operations |
| GitHub account settings, variables, approvals | Repository owner/admin, currently jrepp | Appropriate repository administration rights |
| Host allowlists, App installation, capacity, signed router rollout | t1-hosting operators | Separate hosting repository and host access |
| Generic runner implementation | Runner project | Separate project access; environment policy stays in hosting |
| Physical display/touch confirmation | Person with the board | Physical access, no hosting credentials |

The local directory name and application are Jelligotchi. The upstream remote
is intentionally `jrepp/jelligothci`. References to the sibling auth project are
historical inspiration; cloning or accessing auth is not a setup requirement.

# GitHub settings inventory

Read-only API inspection on 2026-10-07 at 23:44 UTC returned:

| Setting | Observed state | Recreation or operational meaning |
| --- | --- | --- |
| Repository/default branch | Private, main | Grant contributors their own repository access |
| Actions | Enabled; all actions allowed | Workflow SHAs are enforced by project checks, not an account-wide restriction |
| GitHub SHA pin requirement | false | Preserve full commit refs and the workflow-pin check even without account enforcement |
| Default workflow token | read | Workflow files explicitly grant the release jobs needed write permissions |
| Actions PR creation/approval setting | enabled | Required for Release Please to create its PRs with the repository token |
| PRIVATE_LINUX_RUNNERS | true | Artemis onboarding and cleanup were verified before enabling |
| PRIVATE_MACOS_RUNNERS | unset | Defaults to hosted; prior Mac Studio inspection found it offline |
| Repository Actions secrets | zero | These workflows require no stored PAT or runner App key |
| Fork PR workflows for private repository | disabled | Do not promise automatic upstream CI for fork contributions |
| Fork write tokens / secrets and variables | disabled / disabled | Retain the boundary when planning any future fork workflow support |
| Merge methods | Squash, merge, and rebase enabled | Conventional Commit semantics must survive the chosen merge method |
| Delete branch after merge | false | Branch cleanup is not automatic |
| Rulesets and main protection | API returned 403 with a plan/visibility limitation | Required checks and merge protection were not inspectable; do not claim enforcement |

The fork-contributor approval endpoint also returned 422 because it does not
apply to this private repository. That is not proof that all workflow approvals
are disabled: bot-created release PR runs required approval during initial
validation. The explicit candidate dispatch handles the release validation path.

This is a dated snapshot. It excludes host-side credentials and does not imply
there are no account-level settings outside the inspected APIs. Reinspect with
your own authenticated GitHub identity when reproducing or changing operations:

```sh
gh api repos/jrepp/jelligothci/actions/permissions
gh api repos/jrepp/jelligothci/actions/permissions/workflow
gh api repos/jrepp/jelligothci/actions/permissions/fork-pr-workflows-private-repos
gh variable list --repo jrepp/jelligothci
gh secret list --repo jrepp/jelligothci
gh api repos/jrepp/jelligothci/rulesets
gh api repos/jrepp/jelligothci/branches/main/protection
```

To recreate the repository, use the workflow files as the token-permission
source of truth and enable Actions PR creation for Release Please. Start both
private-runner opt-ins unset/false; enable each only after separate host admission,
canary, attribution, cleanup, and health verification. Confirm settings through
the API or UI instead of assuming they traveled with a clone. Current manual
review practice is to check the relevant CI results before merging; server-side
required-check enforcement has not been established by this inspection.

# Contributor execution boundary

The core workflow has no event-level trust filter beyond the repository runner
variables. With private Linux enabled, collaborator pushes and PR jobs that run
can use Artemis. Restrict write access accordingly. Ordinary contributors need
neither host SSH nor the runner App credentials. Credentials remain on hosts.

Enabling external/fork PR execution is a separate maintainer policy change;
do not enable it as part of cloning or contributor setup. Review the private
runner execution boundary first. The present disabled fork workflow setting
is the observed account boundary, not an implemented workflow sandbox.

# Release operation and recovery

Use Conventional Commits and review Release Please's PR, including VERSION,
manifest, changelog, and validation for the candidate revision. Merge only after
relevant validation passes. The main-branch Release Please workflow creates
the tag/release and calls the reusable release workflow. A release record may
exist before its source archive and checksums are uploaded.

```sh
gh pr checks <pr-number> --repo jrepp/jelligothci
gh run list --repo jrepp/jelligothci
gh run view <run-id> --repo jrepp/jelligothci --log-failed
```

If a bot PR has blocked checks, inspect the revision before an authorized
maintainer approves execution. Candidate validation is also explicitly dispatched
by Release Please; check that run rather than assuming ordinary PR events fired.
Use the revision actually checked out by the job, not only the workflow's main
branch SHA, when assessing a manual dispatch with a ref input.

For a failure, identify whether it is source/test behavior, metadata, dependency
downloads, permission, or publication. Retry a diagnosed transient failure;
stop repeating the same action when it recurs and investigate the cause. Do not
weaken warnings or checks to repair infrastructure failures.

```sh
# Validate without publishing; replace the example with the intended ref.
gh workflow run release.yml --repo jrepp/jelligothci -f ref=main
# Retry publication only after diagnosing failure and checking this release/tag.
gh workflow run release.yml --repo jrepp/jelligothci -f ref=v0.1.1 -F publish=true
```

The publish input requires a matching vVERSION ref and an existing release.
The upload uses --clobber: a rerun replaces named assets. Verify the source ref
and expected release before using it. Download the archive and SHA256SUMS,
verify checksums, extract, and build/test outside the Git checkout. Release assets now also include macOS game, editable-art, and preview downloads;
see [memo-017](memo-017-release-downloads-and-expressive-slice.md). Preserve published tags; fixes land in a new version.

# Runner recovery

If private jobs remain queued, inspect the repository variables and workflow
labels, then consult the hosting operator for host/router health and admission.
Do not distribute App keys or change host profiles from this repository.

An authorized maintainer can restore hosted Linux for subsequent jobs:

```sh
gh variable set PRIVATE_LINUX_RUNNERS --repo jrepp/jelligothci --body false
```

An already queued job does not migrate. After changing routing, cancel the
obsolete queued run and dispatch a new run with the intended ref; verify its
actual runner. Apply the same procedure to the macOS variable if it was enabled.
Use hosting's signed installer and rollback runbook for policy changes. Check
cleanup as well as job success before enabling private routing again.

# Local toolchain and USB recovery

| Symptom | Diagnostic and bounded next step |
| --- | --- |
| Missing or mismatched SDK | Run `scripts/esp doctor`; preserve SDK edits, then use `make esp-sync` for the tracked pin |
| Unsupported Python selected | Check the interpreter version; select Python 3.10-3.13 using JELLI_PYTHON and rerun the wrapper |
| Cache errors after moving a checkout | Recreate generated local environments/build output at the new path; do not copy absolute-path caches |
| sdkconfig.defaults change has no effect | Preserve intentional local config; regenerate the ignored sdkconfig and build tree from tracked defaults |
| Wrong firmware version | Inspect VERSION, incremental rebuild metadata, and image app descriptor; do not clean first and hide a regression |
| USB port missing or busy | Rediscover ports, check the data cable/connection, and close other monitors; do not reuse a stale port path |
| Cannot enter download mode | Use the exact board's documented BOOT/reset sequence, rediscover its port, and retry once after correcting the cause |
| Repeated flash or boot failure | Retain the error/serial log and investigate; do not escalate to full erase or eFuse changes |
| Serial ready but display/touch unknown | Request physical observation; driver initialization alone is insufficient |

See the [hardware guide](../../README.md#hardware-bring-up) for exact board
references. Capture firmware version, reset cause, transfer verification,
startup milestones, runtime errors, and physical observations in a dated memo.
Keep raw logs in ignored build output and remove secrets before sharing.

# Fresh-environment acceptance evidence

A fresh local clone of commit `5f3d6ff0f5743e7e2827eeff0223b7a849c2412d`
was created with `git clone --no-local` into a new temporary directory. No
repository tool cache, build output, or managed SDK components were copied.
On macOS 26.4.1 arm64, using installed AppleClang 21.0.0 and CMake 4.4.3,
the following passed:

- `make test`: engine and headless SDL tests.
- `make core-test`: SDL-free engine build and test.
- `make hooks-check`: pinned tools bootstrapped into the new checkout; all hooks passed.
- `scripts/release-validate`: metadata, workflow pins, core tests, and incremental-version regression.
- `make hooks-install`: the clone's local hooksPath became `.githooks`.
- `make sanitize`: engine and SDL smoke tests passed with ASan/UBSan.

The machine already had compiler/SDK headers, SDL2, Ninja, curl, and Python
installed. Its default Python was 3.14.7; Python 3.13.15 was available separately
for the ESP wrapper. Firmware bootstrap was not repeated in the temporary clone.
This proves the tested checkout setup does not require copied project caches;
it does not prove clean operating-system provisioning or isolation from every
host environment setting. The new handoff prose is validated separately by
Docuchango; this acceptance run exercised the preceding committed code.

Independent fresh-machine firmware bootstrap on Linux, native Windows developer
tooling beyond core compilation, WSL USB forwarding, and physical display/touch
verification remain unproven. These are acceptance gaps, not completed support
claims. No live GitHub permission or host policy was changed for this handoff.

# References

- [Contributor guide](../../CONTRIBUTING.md)
- [Process lessons](memo-004-process-learnings-and-context-remediation.md)
- [Release and runner evidence](memo-002-build-release-and-runner-validation.md)
- [First USB deployment](memo-003-first-usb-deployment.md)
- [Hosting onboarding procedure](https://github.com/jrepp/t1-hosting/blob/main/runner-router/ONBOARDING.md)
- [Hosting operations](https://github.com/jrepp/t1-hosting/blob/main/runner-router/README.md)
