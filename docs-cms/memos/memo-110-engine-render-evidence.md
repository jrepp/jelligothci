---
id: memo-110
title: SDL engine render evidence in pull request CI
author: Codex
created: 2026-11-12T12:00:00Z
tags: [ci, memo, rendering, validation]
project_id: jelligotchi
doc_uuid: 46ca5484-6d10-4402-8a73-116571e818af
---

# SDL engine render evidence in pull request CI

The [Desktop workflow](../../.github/workflows/desktop.yml) captures render evidence
from the built SDL game during pull request checks and pushes to `main`. The
[capture script](../../scripts/engine-evidence.py) reads the current
[scene catalog](../../content/scenes.json), invokes the headless SDL executable,
and emits raw PNGs, four-scene labelled contact sheets, and a JSON provenance
manifest. The manifest records the checked out Git SHA, executable, catalog and
capture-script hashes, worktree status, exact render commands, frame counts,
dimensions, image hashes, and per-command elapsed time. Each render receives a
fresh temporary save path that is removed after the process exits.

Every authored scene is captured at its first frame and after 240 simulated
frames with an 8 px/s travel override. The script verifies successful process
exit, the 466x466 engine surface, non-uniform image pixels, and changed pixels
in an upper background strip between the early and travel views of the authored
market scene. This avoids counting pet animation as scene motion. The workflow
uploads the unmodified PNG captures alongside the contact sheets and manifest;
the job summary links to the downloadable artifact.

Automated output is visual evidence for review, not a pixel-baseline acceptance
test or proof of physical display behavior. It exercises the SDL headless path
and does not establish firmware rendering, device timing, touch behavior, or
human visual quality. Run the focused script contract tests with
`./scripts/uv run --script tests/test_engine_evidence.py`.

## Validation

- Focused Python contract tests: two tests pass, covering catalog-wide capture,
  deterministic arguments, isolated saves, manifest/contact sheet generation,
  and command failure handling.
- Focused Python contract tests: `./scripts/uv run --script tests/test_engine_evidence.py`
  passed (2 tests).
- Real sanitized SDL executable: rendered all 56 images; the market background
  strip contained 8,564 changed pixels. Seven contact sheets were generated and
  the first sheet was visually inspected. `actionlint .github/workflows/desktop.yml`
  and `make docs-check` passed.
- `make sanitize` compiled the SDL executable and was still running the existing
  Jelli Art server integration test locally when this memo was committed; the
  complete sanitizer CTest result is not claimed here. The Desktop CI workflow
  continues to run the complete sanitizer suite.

## Nine-scenario gameplay canary

The [native gameplay canary](../../scripts/gameplay-canary.py) generates one
3×3 `canary.png` alongside the scene sheets. It covers four pet forms, the main
and care menus, breakfast, a collection with a gift, a celebration, and two
scene backdrops. The workflow retains the nine original PPM frames, PNG copies,
engine JSON state and expected values, exact commands, source and binary hashes,
worktree status, and a separate no-effect control. State checks verify each
scenario's pet, form, menu and relevant activity or collection state. Pixel
differences against same-state controls check that the celebration and garden
scene are visible. The native preview configures the pet and page state for each
scenario and uses the game command path for breakfast; it does not replay touch
inputs. These images are engine evidence, not a physical-device test.

The initial local canary rejected the collection screen because it uses only four
colors, fewer than a blanket eight-color threshold despite visibly containing its
menu. Requiring three colors, together with the collection state assertion and
visual review, accepts that real low-color screen while rejecting blank frames.
The nine-scenario grid and all frames were rendered and inspected locally; the
focused canary contract tests passed. CI results must be checked on the final PR
revision. The initial direct run used the current native preview binary and did not
flash or exercise a physical device.
