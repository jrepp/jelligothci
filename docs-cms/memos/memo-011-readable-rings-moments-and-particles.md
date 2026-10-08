---
title: Readable rings, moments, and native particles
author: Codex
created: 2026-10-08T02:39:31Z
tags: [assets, memo, rendering, validation]
id: memo-011
project_id: jelligotchi
doc_uuid: 81ed5a25-48a3-4f5d-bcc3-924d7eb66f6c
---

# Observation and Remedy

After the deployment in [memo-010](memo-010-pet-and-debug-firmware-deployment.md),
the user reported unreadable text and excessive pixel density. The remedy doubles
the pet to 192px, adds six 96px ring icons and a large bottom control, and replaces
five narrow bars with one sliding stat tile and a large 1–100 number. Nested rings
show arrow BACK; the root ring shows X CLOSE. Moments is now a main category.

# Art Record

The source inventory is 47 PNGs: 12 creature frames, 12 small icons, 13 large menu
icons, five meter pictograms, four props, and one font. Care/social hearts have
rounded lobes; cups, gifts, toast, and other menu silhouettes have a softer pass.
The original palette, binary alpha, dimensions, and IDs are retained.

Native pixels/masks total 77,856 bytes. With 12,288 bytes of definition/metadata
allowances the proposed pack is 90,144 bytes, leaving 8,160 under a 96 KiB ceiling.
The standalone HTML and native game consume the same PNGs. The HTML shows art and
interactions; it does not execute gameplay or grant rewards.

# Native Particle Budget and Presentation

Each particle is exactly eight bytes: two signed Q4 positions, two signed
velocities, one lifetime byte, and one packed palette/size/shape byte. A pool of
24 plus renderer history and random state measures 216 bytes on the host,
with a compile-time ceiling of 224 bytes on both targets. There is no
extra framebuffer, background cache, heap allocation, or SDK dependency.

Accepted buttons emit eight particles; rejected actions emit three and retain
error text. Round-robin replacement bounds bursts under repeated input. Integer
20 ms motion steps admit at most 40 iterations; elapsed intervals of 800 ms or
more expire all effects. Maximum particle life is 580 ms. Cosmetic random state
is separate from gameplay and saves. Effects continue while simulation is paused.

The renderer restores the union of previous/current particle bounds by clipped
scene drawing, then overlays native RGB565 squares/crosses. Sprite rejection and
clipped primitive loops avoid scanning the full scene for a small burst. Existing
damage is merged for synchronous host presentation; widely separated bursts can
still produce a large union. There is no claim of measured device speedup.

# Verification and Remaining Work

Validation completed:

- `make test`: all 15 desktop tests passed, including real SDL debug transport.
- `make core-test`: all six SDL-free tests passed.
- `make sanitize`: all 15 tests passed with address/undefined-behavior checks.
- `make lint-c`: formatting, Clang-Tidy, Cppcheck, and 51-file size gates passed.
- Asset validation: all 47 PNGs, palette/bounds/masks, and 77,856-byte payload passed.
- Chrome preview: all five category drill-downs, BACK/CLOSE, particle expiry,
  reduced-motion behavior, 390px layout, and JavaScript error checks passed.
- Actual SDL screenshots inspected: home, category ring, moments, and celebration.
  Particle tests compare restored pixels exactly against the clean scene, verify
  deterministic time partitioning, bounded replacement, and long-stall expiry.
- `make esp-build`: linked binary 0xa7fd0 (688,080) bytes; 34% of the 1 MiB app
  partition remains free. No hardware performance measurement was performed.
- `make hooks-check`: all repository hooks passed.
- Documentation validation repaired only sorted frontmatter tags after adding
  the required unscheduled target-release field; no unresolved findings.

The new interface has not been flashed. Physical readability, touch accuracy, frame timing, and the
perceived charm of these assets remain pending device review.

Dedicated moment scenes/effects, reliable device wall time, accessible ring
labels, reduced-motion settings, and hardware particle tuning remain in
[PRD-001](../prd/prd-001-moments-and-readable-care.md). Existing moment actions are
feed/play/travel prototypes. No new ritual reward economy is implemented.
# Subsequent Deployment and Tuning

The rings were subsequently flashed and the CLI externally accepted on SDL and
ESP32. Slower configurable animation, stat-tap settling, and creature tuning
profiles follow in [memo-012](memo-012-cli-acceptance-and-creature-tunables.md).
The timing values above describe the initial implementation.
