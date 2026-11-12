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
