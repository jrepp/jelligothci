---
title: Settings sound level and version 6 save
author: Claude
created: 2026-10-08T22:19:31Z
tags: [audio, memo, save, ui]
id: memo-021
project_id: jelligotchi
doc_uuid: 19eab5a2-8269-4084-be13-c4dfb613d9a7
---

# Overview

The user asked to "add an on screen sound output level adjustment to the gears
menu and set the default output level to the middle of the range." The gears
menu is the Settings ring. This memo records the level scale, the volume mapping
that both hosts now share, the version 6 save change, the editor's layout and
input codes, and what has not been verified.

# Context

Before this change each host chose a fixed request volume per cue: 25 for the UI
tap and 18 for every other cue, duplicated in `ports/sdl/main.c` and
`ports/esp32/main/main.c`. Requests use a 0–80 scale. SDL multiplies samples by
volume/100; the ESP32 sets the ES8311 codec's output volume to the requested
value before each cue. There was no player-facing control, and the explicit
`jelli-debug sound CUE VOLUME` command passed its own volume.

# Details

## Level scale and mapping

- `JelliGame.sound_level` is a persisted integer 0..10 (`JELLI_SOUND_LEVEL_MAX`).
  New games start at 5 (`JELLI_SOUND_LEVEL_DEFAULT`), the middle. 0 mutes.
  `jelli_game_valid` rejects levels above 10.
- `jelli_sound_volume(level, cue)` in `core/sound.c` is the only mapping. It
  returns `round(base * level / 5)`, with base 25 for the tap cue
  (`JELLI_SOUND_TAP`, zero-based cue 5) and 18 otherwise. Level 5 reproduces
  the former constants exactly; level 10 doubles them (tap 50, others 36),
  below the 80 cap, which a static assertion enforces. Levels above 10 clamp to
  10; unknown cues return 0. Every step is strictly louder than the previous one.
- Hosts call the mapping after `jelli_pet_frame` and skip the request entirely
  when it returns 0. Because input is handled inside the frame, the tap that
  confirms a VOL press plays at the new level. At level 0 neither taps nor coos
  are requested.
- The debug `sound` command is unchanged and unscaled; it ignores the level.
  The three range checks now share `JELLI_SOUND_VOLUME_MAX` (80) instead of
  repeating the literal.

## Save codec version 6

`JELLI_SAVE_VERSION` is 6, shared by SDL storage. One byte follows the version 4
battery setting: the sound level, rejected as corrupt when above 10. Versions
1–5 decode with level 5. Version 5's ninja migration is unchanged. A v6 record
relabelled as v4 or v5 is rejected for its extra byte, and decode failure still
leaves the output untouched.

Older builds reject version 6 cleanly. A decoder compiled from the parent
commit (version 5) rejected a version 6 save produced by this build and left its
output unchanged. SDL storage classifies a higher version as incompatible and
preserves the file; the ESP32 protects the checkpoint and disables writes.
Downgrading therefore keeps the save but cannot load it.

## Settings UI

- SOUND is Settings slot 6, the free left ring position. Existing slot IDs are
  unchanged. It reuses the 16x16 `effects.music` sprite (ID 9006) at 3x; no new
  art was added. Its badge shows the level number on the ON color, or OFF on the
  OFF color at 0, using the same rectangle as the REST and BATTERY badges.
- Tapping SOUND opens a sound editor (`JelliPetUi.sound_edit`), modeled on the
  clock editor and never open at the same time as it. The center disk shows the
  level as a 4x number, "SOUND", and a ten-segment bar: filled segments are
  solid mint, empty ones hollow grey outlines. The caption reads
  `LEVEL n OF 10` or `MUTED`. Slot 1 is VOL − on the left and slot 2 is VOL + on
  the right; other ring slots and the clock gear are absent. VOL − at 0 and
  VOL + at 10 return NOT_READY and are dimmed. Back or a downward swipe leaves
  the editor, then Settings, as with the clock editor.
- Each accepted step sets the save request like other persisting Settings
  actions. Opening the editor changes nothing and does not request a save.
- Input codes: 42 opens the editor; 43 (VOL −) and 44 (VOL +) are 42 + slot.
  The debug viewer names them.
- The render key carries `sound_level`, `sound_edit`, and `ring_sound_edit`, and
  ring transitions treat entering or leaving the editor like a page change.
  Unchanged state causes zero damage. Settings frames always redraw fully, so
  partial redraws there equal full redraws by construction; tests check it.
- `jelli-debug state` adds `visual.sound` with `level`, `max`, `edit`,
  `tap_volume`, and `voice_volume`. The busiest-page reply was 1,303 of 4,096
  bytes in the debug test.
- `tests/test_asset_reload.py` sampled a background pixel at (100, 233), now
  inside the SOUND button. It samples (135, 233), between that button and the
  center clock disk.

## Budget

| Measure | memo-020 | Now |
| --- | --- | --- |
| 1.75 app image (1 MB partition) | 0xde620, 13% free | 0xdebb0 (912,304 B), 0x21450 (13%) free |
| 1.75C app image | 0xdbc50, 14% free | 0xdc1b0 (901,552 B), 0x23e50 (14%) free |

On the desktop build, `JelliGame` stays 3,528 bytes because the new byte fits
existing padding. `JelliPetUi`, which holds the render key, grew 8 bytes, so the
pet engine is 10,552 bytes against its 32 KiB static assertion. No allocation
or new buffers were added.

# Verification

- `make test`: 47 of 47 pass. `make core-test`: 22 of 22. `make sanitize`:
  47 of 47. `make hooks-check` passes, and `make docs-check` passes.
- New tests: `sound_level` (core; full cue × level table, clamping, default,
  validity), `pet_sound` (button and slots, entering and leaving, limits,
  persistence, events, clock editor unaffected, and a fake host that hears each
  press at the new level and requests nothing at 0), and `pet_sound_render`
  (badge, bar shapes, number, dimmed limits, transitions, damage, asleep).
  Removing `sound_level` from the render key, or filling the empty segments,
  makes `pet_sound_render` fail.
- Updated tests: save settings, save fuzz (sound byte sweep 0..255, v5/v3
  truncation labels, version sweep now accepts only 6), roster and roster
  migration (v4/v5 layouts drop the sound byte, v5 and v6 relabels), save
  storage (v5 file loads level 5), SDL session (v5 and v6 files), debug state,
  and the unscaled debug sound command.
- A headless SDL instance with its own save and socket was driven with the
  debug CLI. It produced Settings with a "5" badge, the editor at 5, 0 and 10,
  and Settings with a "10" badge. VOL + reported NOT READY at 10. After a
  restart from the same save, the state reported level 10 and the save's
  version 6 sound byte was 10.
- Both firmware images build. Nothing was flashed.

# Remaining uncertainty

- Loudness on the physical speaker is unverified. The ES8311 output volume is
  not necessarily linear in perceived loudness, so equal steps may not sound
  equal, and level 10 (codec volume 50 for the tap) may be louder than desired.
  Measure or listen on hardware before changing the mapping.
- Appearance and touch of the SOUND button and the editor on the AMOLED are
  unverified, including legibility of the hollow segments.
- Desktop audio was not listened to; headless runs have no audio device.

# References

- [Playing guide: sound level](../../docs/playing.md#sound-level)
- [memo-020: Ninja pet and version 5 save](memo-020-ninja-pet-and-three-pet-roster.md)
- [memo-016: Responsive routines and audio](memo-016-responsive-pet-routines-and-audio.md)
