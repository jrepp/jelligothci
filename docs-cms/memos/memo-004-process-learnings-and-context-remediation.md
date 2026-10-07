---
id: memo-004
title: Process learnings and context remediation
author: Codex
created: 2026-10-07
tags: [context, deployment, process, releases, tooling]
project_id: jelligotchi
doc_uuid: b4aa1d81-5ca5-4db2-bb04-ba68892430e7
---

# Purpose

Capture the lessons from repository bootstrap, private-runner onboarding,
release validation, and the first USB deployment. The user requested context
capture so later work benefits from observed failures and their fixes. This
record adds operational guidance; it does not approve new product scope.

# Observed failures and remedies

| Observation | Implemented remedy | Guidance for the next relevant task |
| --- | --- | --- |
| The requested docs bootstrap was a Python package, clarified as Docuchango, not the npm Docusaurus CLI. | Pinned Docuchango runs through local uvx; docs-cms is initialized without a website. | Resolve tool identity before substituting an ecosystem or adding a website. Reuse the established wrappers. |
| SDK activation picked an incompatible host Python during initial setup. | The ESP wrapper selects Python 3.10-3.13 explicitly and activates the pinned SDK through that interpreter. | Test bootstrap in a fresh environment as well as the developer's already-working shell. |
| The private runner image had a compiler and Make, but no CMake. | Core CI installs pinned CMake with local uv. | Confirm required tools in the actual execution image before enabling private routing. |
| An inherited UV_INSTALL_DIR redirected uv outside the checkout despite UV_UNMANAGED_INSTALL. | The wrapper sets both installer paths; a cold-bootstrap check with conflicting overrides passed. | Treat inherited environment variables as inputs. A warm local cache does not validate installer isolation. |
| VERSION changed but an existing ESP32 build retained old version metadata. | Both CMake projects track VERSION as a configure dependency; a regression test covers incremental desktop builds. Local ESP32 rebuilds and the final binary descriptor were checked too. | Test version changes without deleting the build directory before publishing. A clean build alone misses stale-metadata bugs. |
| Initial workflows used moving action tags and latest OS aliases. | Actions now use full commit SHAs with version comments; OS labels are explicit and a pin gate enforces them. | Resolve the exact release to its commit when pinning. Explicit OS labels do not freeze GitHub's underlying VM images. |
| Generated release PRs did not run ordinary CI without approval in this session. | Release Please explicitly dispatches candidate validation and directly calls release validation after tag creation. | Verify the actual generated-PR and tag path, not just a manually triggered main-branch build. |
| Hosted dependency downloads stalled on the Azure Ubuntu mirror. | Download retries/timeouts and a five-minute install-step limit bound the delay; the failed desktop job passed on retry. | Inspect the failing step and logs before changing application code. Retry an identified transient failure; investigate recurring failures instead of repeatedly rerunning the whole pipeline. |

The version bug was found after v0.1.0 was tagged. Its fix shipped as v0.1.1;
published tags were preserved. The process correction is to validate incremental
version propagation before release publication, not to normalize a patch release
as a substitute for that check.

# Release and CI evidence

Record the commit or tag, workflow run, and conclusion together. A green run
from an older revision is not evidence for newly changed code. A release page
can exist while validation or asset upload is still running. Publication health
requires the validation result and the expected assets; the downloaded archive
was checksum-verified and built outside the Git checkout for v0.1.1.

Use the checks appropriate to the changed layer. Documentation-only work needs
documentation validation; it does not call for another firmware build or flash.
Avoid creating duplicate validation runs while an equivalent check for the same
revision is active. Respect required checks, inspect the remaining job, and report
its actual state instead of repeatedly promising that completion is imminent.
When cancellation races with completion, inspect the final job result and logs
before classifying it as a failed test.

# Host ownership and rollout

Host allowlists, resource profiles, and deployment evidence belong in t1-hosting.
The project owns its workflow labels and opt-in variables. App credentials stay
on the hosts. Linux onboarding used the reviewed policy and signed installer,
then a canary, actual runner attribution, cleanup, and health checks before
PRIVATE_LINUX_RUNNERS was enabled. A passing job alone is not cleanup evidence.

The Mac Studio was offline during onboarding. Its committed policy is not a
live deployment. Rediscover host availability before enabling its opt-in; do
not queue work based only on a historical inventory. Keep environment-specific
policy out of the generic router and reusable C engine.

# Device evidence and limits

The first USB flash verified transfer hashes and reached the v0.1.1 app's ready
message. PSRAM passed its memory test; panel and touch initialization succeeded.
These observations establish successful deployment and startup, not the physical
appearance of shapes or touch behavior. The user has not yet supplied visual or
tap-test confirmation. Do not promote that pending check into a verified claim.

Rediscover the USB port for each deployment. Identify the intended device and
image, build, flash, and monitor startup as separate observations. Check the
reported reset cause: this session's USB_UART_CHIP_RESET followed the monitor's
reset and was not evidence of a crash. Driver warnings need their surrounding
successful or failed operations; their presence alone does not justify changing
board wiring, pins, or drivers. Close the monitor after the observation window
so the next tool can open the port.

For subsequent bring-up, retain a sanitized serial log under ignored build
output when useful, and summarize the firmware version, reset cause, startup
milestones, runtime errors, and user-observed behavior in the memo. Temporary
logs support diagnosis but are not a substitute for tracked findings.

# Where the context lives

- AGENTS.md routes build/release, runner, and hardware work to the relevant memo
  and requires preserving the boundary between recorded and verified state.
- The embedded skill contains the reusable device-verification steps.
- This memo owns the cross-cutting failure/remedy mapping; earlier memos retain
  their dated observations rather than being rewritten as if they knew later facts.
- Executable fixes and regression checks remain the enforcement mechanism.
  Context capture explains their purpose and covers decisions tests cannot make.

The remaining hardware check is user confirmation of shapes, motion, colors,
and tap pause/resume. Private macOS onboarding is a separate pending host task.
Neither is authorized by this context-capture request alone.

# References

- [Build, release, and runner evidence](memo-002-build-release-and-runner-validation.md)
- [USB deployment evidence](memo-003-first-usb-deployment.md)
- [Project instructions](../../AGENTS.md)
- [Embedded skill](../../.agents/skills/jelli-embedded-development/SKILL.md)
- [Incremental-version regression test](../../tests/test_build_version.py)
- [Workflow pin gate](../../scripts/check-workflow-pins.py)
