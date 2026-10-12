---
title: Jelli Art touch and Apple Pencil delivery
author: Jacob Repp
created: 2026-10-12T00:20:00Z
tags: [deployment, jelli-art, memo, tooling, touch]
id: memo-066
project_id: jelligotchi
doc_uuid: b721af21-57a3-48e2-a23a-0e5bac7ff341
---

# Overview

This memo records how [RFC-007](../rfcs/rfc-007-jelli-art-touch-and-pen.md)
shipped as Jelli Art Studio 0.9.0 on 2026-10-11. It covers one desktop
regression found on `main` and the verification evidence. The evidence is
emulated touch and Pencil input plus a live version check. No physical iPad
or Apple Pencil run has been recorded.

# Delivery

| Stage | Pull request | Merged (UTC) |
| --- | --- | --- |
| RFC accepted | [#51](https://github.com/jrepp/jelligothci/pull/51) | 2026-10-11 17:28 |
| T0 foundation (`touch.js`, shell) | [#52](https://github.com/jrepp/jelligothci/pull/52) | 20:26 |
| T0 hardening after review | [#55](https://github.com/jrepp/jelligothci/pull/55) | 22:14 |
| T3 design views | [#57](https://github.com/jrepp/jelligothci/pull/57) | 22:22 |
| T2 timeline and flip-book | [#58](https://github.com/jrepp/jelligothci/pull/58) | 22:24 |
| T1 Paint | [#56](https://github.com/jrepp/jelligothci/pull/56) | 22:30 |
| Desktop Behaviour layout fix | [#59](https://github.com/jrepp/jelligothci/pull/59) | 23:52 |
| Release `jelli-art-v0.9.0`, `v0.11.0` | [#54](https://github.com/jrepp/jelligothci/pull/54) | 2026-10-12 00:07 |

Every stage was implemented and reviewed by a subagent, then auto-merged. The
release PR was admin-merged after `Release validation` passed on its base
commit `cc07fce`. #52 merged before its review fixes were committed, because
commit signing was locked. The fixes therefore shipped separately as #55.

# Regression: desktop Behaviour layout

- **Observation:** #57 merged while its `Studio UI tests` job was failing.
  `test_views.py::test_view[behaviour@desktop]` differed from the baseline by
  12.14% of pixels. At 1440×900 with a mouse, the States & looks editor
  stacked under the preview instead of sitting beside it. RFC-007 requires
  mouse and keyboard pages to look as before.
- **Cause:** #57 changed the 1300 px `.bv-grid` breakpoint in
  [`reactions.js`](../../tools/jelli-art/reactions.js) from a media query to
  a container query. The asset sidebar makes the view's container narrower
  than the window, so the rule matched on desktop.
- **Remedy:** #59 applies that tier only under `@media (any-pointer: coarse)`.
  Baselines are unchanged.
- **Process gap:** `Studio UI tests` is not a required check in the `main`
  ruleset, so `gh pr merge --auto` merged #57, and later #56, before or
  despite that job. #56, #57 and #58 were reviewed on separate branches. The
  first run that tested them together was the `Jelli Art image` run on `main`.

# Verification

- **CI:** the `Jelli Art image` workflow passed on `main` at `cc07fce`, with
  all four stages and the fix. Its UI suite includes the touch tests
  `test_touch_foundation.py`, `test_touch_paint.py`,
  `test_touch_animation.py` and `test_touch_design.py`.
- **Release validation:** passed on `cc07fce` (run 38185922217).
- **Deployment:** `https://jelli-art.home.jrepp.com/healthz` reported
  `studio_version` 0.8.0 at 00:10:29Z and 0.9.0 at 00:11:29Z, about four
  minutes after the release merged. The nuc host followed the `:0` image
  through its five-minute auto-update timer (t1-hosting memo-039). The
  served page contains the inlined `window.JelliTouch` and `JelliDesign`
  scripts.

# Remaining uncertainty

- **Physical input:** iPad and Apple Pencil behaviour is not verified.
  Tests emulate Pencil input with pen `pointerType` and fingers with CDP
  `Input.dispatchTouchEvent`. Palm rejection, Pencil hover and Safari's
  gesture handling still need a run on a real device.
- **Required checks:** whether `Studio UI tests` should become a required
  check is undecided. Until then, wait for it before merging studio UI
  changes.
