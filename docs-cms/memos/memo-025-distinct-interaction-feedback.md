---
id: memo-025
title: Distinct interaction feedback
author: Codex
created: 2026-10-09
tags: [audio, interaction, menus]
project_id: jelligotchi
doc_uuid: 3bb188fd-21b2-4a11-b113-e08948078795
---

# Behavior

The user requested separate sounds for menu confirmation, Back/Close, and pet
interaction, and no sprite sprays on routine menu clicks. Confirmation now uses
a 105 ms two-note rise; Back/Close retains the original 65 ms tap. Accepted pet
touches and care actions use a softer 170 ms voiced chirp. Swipe navigation
follows the same distinction. Disabled actions stay silent, and input cues are
still coalesced to one per 120 ms. Idle coos remain separate.

Navigation, settings, collection selection, and present deselection do not emit
sprite sprays. Accepted care changes and celebrations retain them. Brushing and
washing reserve their intermediate visual feedback for localized bubbles;
completion still celebrates. The cue selection does not change saved pet state.

# Resources and verification

Two new fixed sound programs add 100 bytes of note data (450 bytes total), with
no new task, queue, allocation, or PCM block. UI state gains a one-byte pending
cue selector. The existing volume preference applies to all automatic audio.
The CLI and audition exporter expose the new `confirm` and `pet` cues.

The navigation regression checks confirmation/back distinction, no menu
particles, preserved water-action particles, and the pet-action cue. Synth tests
cover all nine cues for block invariance, envelopes, duration, headroom, DC, and
bounds. The protocol rejection test now uses the new first invalid cue index.
`make test` passed 41 tests, `make core-test` passed 20, and `make sanitize`
passed 41. `make lint-c`, `make esp-build`, and documentation checks passed. The deterministic SDL demo screenshot was inspected and
nine WAV cues were exported. Physical listening awaits device reconnection.

# References

- [Feedback implementation](../../core/pet_feedback.c)
- [Sound sources and audition](../../assets/sound/README.md)
- [Playing guide](../../docs/playing.md)
