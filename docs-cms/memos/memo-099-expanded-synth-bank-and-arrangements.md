---
id: memo-099
title: Expanded synthesized rhythm bank and original arrangements
author: Codex
created: 2026-10-21
tags: [audio, authoring, music, validation]
project_id: jelligotchi
doc_uuid: 60292e4c-881a-4735-a266-beff7d511ceb
---

# Requested increment

The owner requested more generated drum and bass sounds to fill out existing
tracks, with composition ideas informed by sheet-music references. This increment
adds original synthesized instruments and adjusts Iron Pulse, Bubble Rush and
Garden Waltz. Track IDs, melodies' original pitch material, tempos, the four-channel
player and public save contract remain stable. No score image, lyric, external
melody or recording is imported into the repository.

# Instruments and authoring boundary

The [sample generator](../../tools/audio/music_samples.py) adds a tight overdriven
kick, metallic hat, short clap, harmonic pulse bass and sine sub-bass. Existing
sample bytes are unchanged. Drum envelopes taper to silence; the basses are
zero-mean cycles intended for looping. Independent seeds keep generation stable.

The offline source bank measured 3,904 bytes against its existing 4,096-byte bound,
including the retained legacy duplicate keys voice. This is not the runtime
catalog footprint: since the MOD migration described in
[memo-096](memo-096-music-tracker-authoring.md), each authored module embeds its
selected samples and the build emits their immutable C tables. The selected
modules reduce total catalog PCM from 25,280 to 23,808 bytes at this revision,
a 1,472-byte reduction because the new percussion is more compact. No additional
runtime allocation, sample loader or global bank is introduced.

Open **Iron Pulse** in Studio's existing sample manager to select all five new
voices: Tight drive kick, Metal hat, Clap, Acid pulse bass and Sine sub bass.
Every new voice is used by the arrangement, not merely an unused sample slot.
Bubble Rush also uses the kick, hat and sub-bass. Sample names, volumes, loops and
PCM remain ordinary editable MOD content; there is no new instrument-selection UI.

Regeneration remains an explicit authoring operation. The
[workout composer](../../tools/audio/create_workout_music.py) and
[Bubble composer](../../tools/audio/create_bubble_music.py) require an output path.
The [starter composer](../../tools/audio/create_music.py) now accepts
`--song garden-waltz` with its required output directory, so a selected arrangement
can be regenerated without overwriting other authored modules. Normal builds only
compile the saved MODs. Only the three named MOD files changed in this increment.

# Reference study and actual application

The references were studied visually as arrangement examples. The two Musicnotes
sources are one-page/audition extracts, not complete scores. The Mozart composition
is public domain, but the viewed modern transcription was only studied, not copied.

- [Blue Monday — licensed preview](https://www.musicnotes.com/sheetmusic/new-order/blue-monday/MN0026300):
  a repeating upper ostinato with rests/ties and later lower broken-chord entry.
  Iron Pulse retains its original acid motif and workout tempo, delays bass until
  after its sparse opening, varies the tail of repeated phrases, and adds brief
  rests. Pulse and sine bass alternate within the existing form.
- [Get Lucky — licensed audition extract](https://www.musicnotes.com/sheetmusic/daft-punk/get-lucky/MN0206090):
  sustained harmony over an active bass, followed by denser offbeat chord attacks.
  Bubble Rush keeps its own hook and harmony, adds short bass pickups and rests,
  leaves more space around the opening lead, and makes later supporting arpeggios
  denser. The source melody and bass line are not reproduced.
- [Mozart K.545 — score listing](https://imslp.org/wiki/Piano_Sonata_No.16%2C_K.545_%28Mozart%2C_Wolfgang_Amadeus%29):
  long singing phrases above broken chords contrast with shorter answering runs.
  Garden Waltz now contrasts longer opening notes with its existing shorter-note
  material, using a low/high/middle/high accompaniment over its own chord roots.
  Its existing tracker grid and tempo remain unchanged.

# Verification and findings

The new bank tests verify the legacy PCM hash, deterministic output, signed-byte
bounds, MOD word lengths, the source-bank budget, drum envelope ends and bass loop
seams. Composer tests also verify that all five named voices are present and used
in Iron Pulse; shipped-module checks confirm their availability to Studio.

The first Garden Waltz generation rejected an unsupported tracker period in the
new accompaniment. Correcting it to the valid note-table entry allowed the codec
to generate the module; the failed attempt did not overwrite its authored file.
The strict codec remains the authority for the supported MOD subset.

Validation on 2026-10-21:

- Bank/composer tests passed 7/7; MOD codec tests passed 6/6.
- Desktop music checks passed 6/6, including exact native/preview PCM equality for
  all 15 catalog tracks. The three changed tracks rendered for 30 seconds at 80%
  gain with peaks of 5,175 (Garden Waltz), 9,188 (Bubble Rush), and 6,565 (Iron
  Pulse), below signed 16-bit clipping.
- Pinned-container Studio music tests passed 10/10 with screenshots enabled and
  no baseline updates. The new sample-manager test selects all five voices and
  auditions the looping sub bass.
- Address/undefined-behavior sanitizer music checks passed 3/3.
- A fresh ESP-IDF v5.5.5 firmware build passed. The ESP32-S3 image measured
  2,112,768 bytes (`0x203d00`), leaving `0x1fc300` bytes (50%) of the app partition
  free. Its descriptor reports version 0.16.0; checksum and validation hash passed.
- Documentation validation and repair passed with no unresolved findings.

SHA-256 comparisons confirm the other twelve authored MODs are unchanged.
No hardware was flashed or physically auditioned. Native and browser rendering
and automated audition evidence do not establish physical speaker quality or
subjective listening preference.
