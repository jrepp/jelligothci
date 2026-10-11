---
title: Additional location backgrounds
author: Codex
created: 2026-10-10T16:46:39Z
tags: [art, locations, memo, testing]
id: memo-042
project_id: jelligotchi
doc_uuid: 791e47c4-8521-4e7a-a4d1-21f6ba4f960f
---

# Requested increment

The user asked to try additional location backgrounds after authored activity
locations landed. Four scenes were added: Park, Pond, Beach and Library. They
extend the existing code-authored 64×64, 16-gray background style with a black
vignette, leaving the colorful pet and UI readable. Home/Garden artwork is
unchanged. A reproduction script is tracked with the source art and integrated
into the existing full authoring recipe.

# Runtime and authoring

Location IDs 0/Home and 1/Garden are preserved. IDs 2/Park, 3/Pond, 4/Beach and
5/Library are appended. The generated location catalog supplies names, heading
labels, background asset IDs and indoor/outdoor flags. The renderer, travel
validation, saved-game validation and activity selection now use the catalog,
replacing two-location assumptions. TRAVEL cycles through all six places.
Outdoor scenes show the nighttime moon; Home and Library remain indoors.

Activities choose from plausible locations: jogging and ball games use Park;
fishing uses Pond; swimming uses Pond/Beach; reading and chess can use Library.
Quieter activities have multiple allowed places. Garden remains the gardening
location and keeps its existing garden-specific prize behavior. Selection is
bounded, deterministic per accepted start and restricted to the authored set.
Jelli Art reads all six choices from the catalog without hard-coded UI lists.

The catalog supports up to eight locations in the existing activity byte mask.
IDs must remain sequential and keys/names unique; Home/Garden retain their old
IDs. Builds reject unknown location keys, invalid background references (including
existing assets of the wrong kind), invalid atmosphere flags and oversized names.
The core-only build validates references without a Python or image dependency;
desktop/firmware asset embedding also validates dimensions, palette, alpha and
manifest/PNG agreement. The activity editor runs the same reference validator.

# Resource and persistence budget

Four backgrounds add 32,768 bytes of RGB565 pixels and 2,048 bytes of masks
(34,816 total) plus small read-only asset/catalog records. There is no additional
framebuffer, mutable pet state, dynamic core allocation or save payload. The
existing saved location byte holds the appended IDs, and old Home/Garden saves
retain their meaning. Existing older binaries cannot display these new IDs;
keep the updated code and content together.

# Validation

Validation passed: `make test` (56/56), `make core-test` (27/27),
`make sanitize` (56/56), `make esp-build`, `make hooks-check` and
`make docs-check`/`make docs-fix` (no repairs). Cppcheck caught a test pointer
that could be const; correcting it cleared the check without suppression.
Day/night snapshots were inspected at the actual
466×466 panel layout. The first Pond draft put details under the stats card;
the shoreline, reeds and dock were raised so the scene remains identifiable.
New tests cover travel/save round trips for every location, valid randomized
activity selection, distinct rendering, full damage after travel, unchanged
frames, circular clipping and stride preservation. Content mutation tests cover
unknown/wrong-kind backgrounds, duplicate keys, invalid flags and unstable IDs.
Hardware is unflashed and unverified.

PR #22 release validation failed in run `38069721880` after all 27 core tests
passed: the incremental-version test's isolated source copy omitted
`assets/slice/assets.json`. Core configuration now reads that catalog to validate
activity and location references. The fixture now copies the manifest while
keeping PNG payloads unnecessary for the core-only build. The full local
`scripts/release-validate` passes, including the existing incremental-version
regression. CI packaging remains to be confirmed on the corrected revision.

The next run, `38070099269`, passed release validation and all 57 Windows tests,
then rejected the artwork export against the old 256 KiB live pack cap. The
complete authored pack contains 107 assets: 125,504 pixels, 15,688 mask bytes,
1,152 glyph bytes and 2,584 bytes of headers (270,432 bytes total). The existing
131,072-pixel and 16,384-byte mask banks fit; the desktop staging buffer grows
by 32,768 bytes to 288 KiB. Firmware memory and the core are unaffected.
The decoder's stale 98,304-pixel guard now uses the actual array capacity, and
the publisher checks bank bounds as well as file size. A full-pack export and
SDL load test now runs in desktop CI, including Windows; small synthetic packs
alone had not exposed either mismatch. Local desktop and sanitizer suites pass
62/62 tests, as do hooks, artwork export, full-pack loading and macOS packaging.

Merging main's baby axolotl and hug frames (PR #37) with these backgrounds
brought the pack to 114 assets, 141,632 pixels and 17,704 mask bytes, over
the 131,072-pixel and 16,384-byte banks. The owner chose to raise the desktop
banks to 196,608 pixels, 24,576 mask bytes and 192 assets, with a 448 KiB
staging buffer, recorded in ADR-012. Firmware memory is still not measured.
Final corrected CI results are pending.

# References

- [Activity balance and location integrity](memo-041-activity-balance-and-location-integrity.md)
- [Artwork inventory and reproduction](../../assets/slice/README.md)
- [Jelli Art authoring](../../tools/jelli-art/README.md)
