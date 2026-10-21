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
