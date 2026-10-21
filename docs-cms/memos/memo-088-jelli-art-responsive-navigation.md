---
title: Jelli Art responsive navigation and interaction polish
author: Codex
created: 2026-10-21T03:24:00Z
tags: [jelli-art, memo, responsive, validation]
id: memo-088
project_id: jelligotchi
doc_uuid: 5283d01e-d349-4035-95df-cc196290053c
---

# Requested increment

The owner requested a Terra subagent to repair the mobile top navigation and
add purposeful interaction polish on desktop, iPad and phones. The working
changes are based on `644a21f`, preserving the existing Effects workspace.

The [shell](../../tools/jelli-art/shell.js) now gives phone modes priority,
wraps tools into visible rows, and constrains long save-state text to the
available width. Tablets use explicit header rows. Compact desktop chrome
suppresses the redundant studio suffix and live version badge to give the mode
rail more room. Active tabs have inset outlines and animated mint underlines;
buttons have hover and press feedback. Reduced-motion preferences suppress
motion, while selection and focus remain clear.

Mode arrows and edge highlights expose overflow. Selection scrolls only the
rail, preventing nested page scrolling, and resize keeps the selected tab
visible. Overflow measurement includes the space occupied by the arrows so
they disappear when every mode fits. Shared shell behavior still supports the
static review page. Reusable layout guidance lives in the
[studio README](../../tools/jelli-art/README.md#page-shell).

# Failure and remedy

The first local layout run passed 37 cases but exceeded the existing desktop
header-height budget by 0.39 px: the new rail inset made the Paint header
56.39 px high at 1440×900. Reducing the rail's vertical inset restored the
budget. The subsequent focused header and responsive-navigation run passed
all nine cases, including unchanged clean/dirty heights. This was a browser
layout regression discovered before publishing; no deployment occurred.

# Verification

- The [responsive tests](../../tools/jelli-art/ui_tests/test_navigation.py)
  exercise phone, tablet and desktop viewport sizes, keyboard End and wrap,
  active-tab visibility after resizing, visible phone tool targets, long
  repository/dirty labels and stable header height.
- `JELLI_UI_LOCAL=1 tools/jelli-art/ui_tests/run.sh -k 'test_navigation or test_header_height' -q`:
  nine passed after the inset fix; the final expanded run passed nine tests
  with 294 deselected in 17 seconds.
- The eight Python studio suites passed 132 tests; the paint-tools JavaScript
  suite passed 19 tests. JSDoc type checking reported no new baseline errors.
- `JELLI_UI_LOCAL=1 tools/jelli-art/ui_tests/run.sh -q`: 301 passed, two
  failed, no skips in 546 seconds. Desktop Paint's main ARIA tree includes a
  `Sprite details, scrolls sideways` group absent from its baseline. Tablet
  Paint icon reports axe `label-content-name-mismatch` on
  `div[data-chip="mint jelly:#43bca2"]`.
- Both failures reproduce with the original `644a21f` shell, using
  `-k 'test_view and (paint-creature or paint-icon)'`: six passed, two failed.
  The original shell also differs from the desktop baseline's fit zoom.
  The edited shell was backed up and restored byte-for-byte after that
  comparison; baseline artifacts are in `build/ui-tests-nav-baseline/`.
  These unresolved Paint findings were not hidden by updating baselines.
- The final focused nav/header run also covers arrow scrolling without mode
  activation and reduced-motion preferences. Normal and dirty-state screenshots
  were captured for every responsive test size.
- Screenshots in `build/ui-tests/navigation/` were inspected at phone,
  tablet and desktop sizes. These local artifacts are not committed.

# Remaining uncertainty

Chromium touch emulation establishes layout and keyboard behavior, not physical
iPad Safari gestures, Pencil behavior or touch feel. No physical iPad, firmware
build, device deployment or screenshot-baseline update was performed for this
header-only increment. The pinned Linux renderer remains the screenshot
comparison authority.

# PR image startup failure and authoring guidance

[PR #74 run 39539443992](https://github.com/jrepp/jelligothci/actions/runs/39539443992),
for head `d4d33ac` and merge revision `95ebdc7`, passed Studio tests but failed
its image smoke step. The image built successfully; its entrypoint exited with
`ModuleNotFoundError: No module named 'effect_data'`, causing the subsequent
health probes to fail. The inherited celebration server import existed in the
checkout but its shared module was missing from both the container COPY list
and build-context allowlist.

The remedy adds `tools/assets/effect_data.py` to both packaging lists. A local
Podman ARM64 image built with the corrected lists starts through the real
entrypoint against the read-only checkout mount. `/healthz` returned `ok: true`
with no problems, `/api/data?before=HEAD` returned 130 assets, and `/api/effects`
returned its data, base, editability and limits. The temporary container was
removed after verification. This is local container evidence, not a production
deployment or a replacement for the GitHub Linux image job.

The owner requested that these failures inform agent authoring guidance.
[AGENTS.md](../../AGENTS.md#scope-and-validation) now routes studio work to
[CONTRIBUTING.md](../../CONTRIBUTING.md#jelli-art-packaging-and-pr-validation),
which requires checking shared/transitive imports against packaging, testing
startup after image builds, inspecting current-revision job logs, and recording
inherited UI failures without treating their provenance as a passing check.
The UI job was still running when the packaging correction was prepared;
remote verification of the correction remains pending.

# Fast commit-time prevention

The owner requested JavaScript type checking and container checks without an
image build on every commit. The [hook configuration](../../.pre-commit-config.yaml)
now runs the existing pinned JSDoc check for studio JavaScript/type configuration
changes and a standard-library-only container layout checker for studio,
shared-asset and packaging changes. The type hook explicitly disables baseline
rewrite mode. File filters keep unrelated commits from invoking these checks.

The [packaging checker](../../scripts/check-studio-container.py) reads the actual
COPY list and build-context allowlist, checks known local Python dependencies
in the shipped layout, literal bundled browser files, and startup/health scripts.
Its [regression fixtures](../../scripts/test-studio-container.py) reproduce the
missing COPY entry and missing allowlist entry independently, plus transitive
imports and other packaging omissions. It rejects unsupported packaging syntax
rather than claiming to model Docker's full grammar. It neither imports the
application nor invokes a container engine.

Both hooks passed against the current checkout. The isolated packaging
regression fixtures passed and run as a separate hook when the checker changes.
A temporary JavaScript type error was rejected by the actual type hook even with
`JELLI_TS_UPDATE=1`; the baseline remained byte-for-byte unchanged. The probe was
removed afterward. The JSDoc check retains the existing error baseline;
these hooks do not establish browser behavior, third-party dependency presence,
OS compatibility or successful packaged startup. The full image smoke test
remains in CI. Authoring instructions are in
[CONTRIBUTING.md](../../CONTRIBUTING.md#jelli-art-packaging-and-pr-validation).

# Final UI gate remediation

The completed pinned CI UI job in run 39539443992 reported 292 passing cases
and 11 failures: ten expected screenshot changes and a tablet Paint palette
accessible-name mismatch. Local screenshot comparison had been disabled, so
local results did not establish the visual gate. The image job on the follow-up
[run 39540426054](https://github.com/jrepp/jelligothci/actions/runs/39540426054)
passed after packaging the missing dependency.

All ten reference screenshots were compared visually with the failed run's
Linux/AMD64 artifacts before copying the reviewed PNGs into the baselines.
They reflect the responsive header and inherited scene preview changes.
The palette fix includes visible usage counts in accessible names and adds an
actual text separator between the name and count spans; changing only the ARIA
label still failed because the visible text nodes were concatenated. The final
focused palette tests passed on desktop, tablet and phone. Accessibility trees
were regenerated in the pinned Playwright image, and the axe baseline only
removes resolved violations; no new exclusions were accepted.

The additional regression checks assert the visible count and accessible name
and run axe. The pinned-image verification run passed 37 cases covering all view checks,
responsive navigation and the palette regressions (snapshot update mode off).
Full current-revision GitHub validation remains the merge gate.

# PR 75 inherited failures and base synchronization

The documentation branch at `695b3c7` still used the celebration base before
PR 74. [Run 39545461731](https://github.com/jrepp/jelligothci/actions/runs/39545461731)
failed the image smoke test: container logs show `ModuleNotFoundError` for
`effect_data`. Its UI job passed 296 cases and failed the tablet Paint creature
screenshot, with 3099 changed pixels. These are the dependency and reviewed
visual changes already addressed by PR 74; the documentation edits do not modify
studio code or assets.

PR 74 merged into `feat/celebration-effects` as `a0cf712` with all checks green.
Merge `5d58239` brings that base into PR 75 without rewriting published history.
No additional screenshot replacement, accessibility exclusion or check bypass
was used. The static container checker and all 13 packaging fixtures pass on
the synchronized branch. The focused pinned Linux/ARM64 tablet Paint check
passed with screenshot comparison enabled (one passed, 305 deselected; update
mode off). Fresh GitHub checks remain pending; this local result does not
establish the full Linux/AMD64 UI suite or packaged startup for the new head.

When another branch contains a known CI remedy, check whether it has actually
landed in the affected PR's base/history before repeating the fix. A base update
must preserve the intended PR file scope and still receive current-head checks.
