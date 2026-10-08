---
title: Slice artwork and local preview validation
author: Codex
created: 2026-10-08T01:17:24Z
tags: [assets, preview, validation]
id: memo-007
project_id: jelligotchi
doc_uuid: 98837276-fda0-4b19-942b-2aa67eab8faa
---

# Scope and result

The user requested the RFC-001 slice assets and an HTML preview sheet. Created
29 source PNGs: 12 creature frames, 12 icons, four props, and a bitmap font atlas
with 96 slots. Added an explicit-ID manifest, original generation prompt, preserved
source atlas, deterministic authoring recipe, and a local preview/export tool.
Mint and Lilac are review names for the two forms, not approved character canon.

The self-contained HTML is generated under `build/assets/preview.html`. Its
controls demonstrate artwork only. No gameplay, content loader, firmware renderer,
storage service, or power-management code was changed. This is asset creation and
review tooling, not completion of the proposed slice.

# Observations and remedies

The built-in image generator returned a 1254x1254 transparent atlas despite the
prompt asking for 1024x1024. The initial fixed-size export assertion failed before
writing PNGs. The authoring recipe now uses rounded quarter-grid boundaries from
the actual source size, then nearest-neighbor sizing, binary alpha, and a fixed
palette. Reproduction matched every selected PNG and the manifest byte-for-byte.
Do not treat requested generation dimensions as the actual file dimensions.

Visual review also found a low-contrast font contact sheet and a stretched font
atlas in HTML. Dark font backdrops and explicit aspect-ratio preservation fixed
those issues. The browser walkthrough was rerun after the changes. The asset README
owns the reusable export and review instructions.

# Validation

- The asset builder validated all 29 files: counts, IDs, dimensions, palette,
  binary alpha, bounds, pivots, glyph occupancy, clip references, and durations.
- Raw exports contain 38,688 bytes: creature pixels/masks 26,112; icons 6,528;
  props 4,896; font bits 1,152. An independent check compared every exported
  RGB565 word, coverage bit, and glyph bit against its source PNG. A modified
  source with partial alpha was rejected.
- The authoring recipe reproduced source PNGs and the manifest byte-for-byte into
  a separate build directory without overwriting the reviewed sources.
- Headless installed Chrome, driven by Playwright 1.56.0, loaded all images with
  no JavaScript errors. Checked both forms, eight preview moments, sheet scales,
  dark backdrop, manifest/PNG downloads, a 390-pixel mobile viewport without page
  overflow, and reduced-motion behavior. Desktop/mobile screenshots and the
  contact sheet remain under ignored `build/assets/`.
- Inspected the contact sheet and desktop preview. The runtime asset font is an
  original small-caps bitmap; explanatory HTML text uses system fonts. The source
  prompt used no uploaded references, stock artwork, or external character designs.

Rebuild with `./scripts/uv run --python 3.12 tools/assets/build_slice.py`.
The tool pins Pillow 12.0.0 in inline metadata and uses the repository-local uv
wrapper. The preview requires no server or network after creation.

# Remaining limits

Art approval, per-pose polish, final creature names, firmware integration, and
physical display/touch verification remain open. The 50,976-byte planned pack
includes 12,288 bytes of allowances, not measured compiled definitions or metadata.
The large provenance atlas is not a runtime asset. Raw pixel exports are not a
finished game pack. No firmware build or flash was needed for this work.

# References

- [Asset inventory, formats, and provenance](../../assets/slice/README.md)
- [Slice architecture and asset plan](../rfcs/rfc-001-virtual-pet-systems-architecture.md)
- [Preview/export tool](../../tools/assets/build_slice.py)
- [Process learnings](memo-004-process-learnings-and-context-remediation.md)