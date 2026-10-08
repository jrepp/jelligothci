---
title: Responsive pet routines, events, and audio
author: Codex
created: 2026-10-08T04:32:05Z
tags: [audio, gameplay, memo, validation]
id: memo-016
project_id: jelligotchi
doc_uuid: 2d5f1e23-33f0-41b5-8278-8ec1ca34ff0b
---

# Implemented increment

The user requested a readable companion event stream, a simpler emotional meter,
personality/time preferences, touch reactions, slower day/night transitions,
blue sleeping Z particles, quiet menu sounds, simplified menus, a recognizable
moon, shorter activities, then a variable multi-stage dental routine and namespaced
tunables. They requested a development tag and flash after validation.

The main tile is now MOOD (care comfort, fun, connection, bond; low health and
negative touch reactions lower it). Odd-ID pets favor breakfast/tea, even-ID pets
outings/movies, with stronger morning/afternoon-evening bonuses respectively.
Gentle touches add social/fun/bond; repeated touches move through annoyed and
overwhelmed states. Touch load drains in ten seconds, reactions expire after
three seconds. Short touch memory is session-only; care changes use existing saves.
These are prototype balancing choices, not approved final gameplay defaults.

There is one bottom BACK control; duplicate ring return actions, save/confirm,
redundant suggestion action, and the extra pet-switching submenu are removed.
Actions still request saves; ESP32 still reports persistence unavailable.

Dental steps are brush, floss, quick brush, mouthwash, spit, cleanup. Brush/floss
initial counts vary 1–3, later steps default to one; other activities default to
three. New dental clicks affect hygiene and bond. Counts are fixed for a round,
using bounded deterministic PRNG state, without changing the save format.
Canonical tunables have namespaces; flat legacy names remain input aliases.
`activity.taps`, `activity.brush.min_taps/max_taps`,
`activity.floss.min_taps/max_taps`, and `activity.finish.taps` support 1–6 taps.
Effective endpoints are sorted after scope resolution. Overrides are session-only.

Night uses a 12-second injected-time fade (scene.night_transition_ms), darker
gray with a light crescent and gently tinted icons. Z particles originate half
an opaque sprite height above its cached centroid, drift upward without gravity,
and disappear on wake. They share the 24-slot, eight-byte particle pool.

# Event and resource contracts

The core optionally writes a borrowed 32-record ring: 64 bytes/event, at most
2064 bytes including bookkeeping. It records input, command outcome, before/after
needs/bond/inventory, delayed completion, and important health/sleep/evolution
changes. It omits frame events, routine decay, and animation. Wire reads return
at most four records; cursor gaps/reset are explicit. The loopback companion
polls every 800 ms, at most two batches, groups taps, caps history at 100 rows,
and supports pause/follow plus collapsed care details and input controls.
The companion exclusively owns its serial/socket connection.

The expanded namespaced tuning list requires a 4096-byte reply buffer (previously
2048); debug state is bounded at 4352 bytes. The 13-key tuning store is bounded
at 576 bytes, including eight pet override slots. No steady-state core allocation
was introduced. There are 63 PNGs: 117568 raw bytes; with existing 12288 bytes of
definition/metadata allowances the pack uses 129856 of its 131072-byte budget.

# Sound and flash research

The board has an ES8311 codec feeding its single NS4150B-amplified speaker. The
pinned BSP handles the wiring and codec: I2S DMA transports decoded PCM, it does
not decode ADPCM or provide S3 hardware sprite blending. Six original procedural
programs occupy 300 bytes plus a 128-byte sine table before names/code/alignment.
The bounded 24-byte synth emits 22050 Hz mono PCM16. The ESP worker has a static
4096-byte stack, 1024-byte PCM buffer, and four two-byte queue entries. Startup
codec/I2S allocation measured a 6788-byte internal-heap delta in the earlier sound
build. Menu feedback uses a 65 ms cue at volume 25 with 120 ms input coalescing.
Codec initialization or a full audio queue never blocks game input.

For sampled audio, IMA-ADPCM is preferable to four-bit linear PCM: 16 kHz mono
needs roughly 8 kB/s plus block headers, decoded to PCM16. PCM16 at that rate uses
32 kB/s. Opus can be smaller but adds decoder complexity and memory. The tiny
procedural synth is sufficient for present cues and vowel-like pet babble;
it is not intelligible TTS. Espressif's documented embedded TTS is Chinese-only.
No voice model, networking, recording, codec dependency, or partition change was added.

The physical device has 16 MiB flash; current partitions end at 0x110000, leaving
14.9375 MiB outside partitions. That space is not an accessible
sound store until a deliberate partition/layout change. The current app partition
is 1 MiB; final image size is recorded with deployment evidence below.

# Validation and remaining polish

The native companion was exercised through its actual HTTP page and game socket:
five consecutive taps (before the requested three-tap/routine revision) grouped
into one entry with cumulative internal changes; pausing held the display still.
Native snapshots show the crescent, night gray, and mood tile. Core tests cover
bounded event history/rollover, absence of decay spam, preference bonuses,
touch overload/recovery, routine counts/overrides, night fade, centroid Z origin,
wake cleanup, and sound coalescing. Final runs/deployment are recorded below.

Failures fixed during integration: removed menu IDs broke old acceptance tests;
checks now use the single bottom return and current visible actions. Expanded
art inventory required updating the exact-count/payload validator. The audio
exporter's old hardcoded cue limit omitted the new tap; it now follows cue count.
The CLI initially rejected dotted tunable names; validation now accepts bounded
nonempty namespace segments, and the external acceptance run exercises them.
Cppcheck const findings and UI tap complexity were resolved by const-correctness
and extracting celebration/frame-key helpers, without loosening C size/complexity gates.
The first board run measured a 123 ms changed render after adding night shading;
per-pixel division by 255 was replaced with bounded fixed-point shifts before
the final deployment. Re-running acceptance on an already exercised high-energy
pet caused REST to auto-wake; the final run starts from a fresh volatile session.

Remaining polish: authored annoyed/sad poses (currently reuse happy/unwell art),
preference profile authoring and balance review, smoother icon art crossfades
beyond the current night tint, RTC/time-zone configuration on ESP32, hardware
persistence, audio voice triggers, and physical confirmation of screen/touch/sound.
Serial boot and I2S submission alone do not establish perceptual quality.

# References

- [Action audit and prior deployment](memo-015-healthy-activities-and-action-audit.md)
- [Board hardware reference](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
- [ESP32-S3 I2S](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2s.html)
- [Espressif software codecs](https://components.espressif.com/components/espressif/esp_audio_codec/versions/2.6.2/readme)
- [Embedded speech synthesis](https://docs.espressif.com/projects/esp-sr/en/latest/esp32/speech_synthesis/readme.html)

# Final build and deployment evidence

Validation: `make test` 21/21; `make core-test` 9/9; `make sanitize` 21/21;
`make lint-c`, `make hooks-check`, `make docs-check` and `make docs-fix` passed.
The final local acceptance run passed 116 separate CLI invocations, including all
activity routines, game-state effects, rejection, screenshots, reconnect, and
canonical dotted tunable names. Browser testing also followed an eight-tap dental
round through all six steps and displayed Floss ×3 as one readable event.
The HTML asset preview completed a nine-tap [3,2,1,1,1,1] routine.

Final application: 799072 bytes (0xc3160), leaving 249504 bytes in the existing
1 MiB factory app partition. SHA-256:
`4052f20e499d42457e178131e4c5b13200c5a494debf4330e51fc34d467c0ae9`.
Desktop measured sizes: debug 4248 bytes, tunables 552, event log 2056,
pet 128, pet engine 4264, synth 24, particle pool 216. These are individual
objects, not total firmware RAM; SDK/driver heaps and framebuffers remain separate.

Flashed the rediscovered `/dev/cu.usbmodem1101`, native USB VID:PID 303A:1001,
serial 90:70:69:FE:21:DC. Esptool verified transfer hashes and reset successfully.
The final monitor-initiated USB reset booted ESP-IDF 5.5.5/app 0.1.1 with 8 MiB
PSRAM, panel/touch/LVGL ready, ES8311 mono PCM16 at 22050 Hz, and debug ready.
Audio startup internal-heap delta remained 6788 bytes with 254591 bytes free.
One changed frame measured 107 ms rendering / 101 ms present after the fixed-point
change; ordinary engine update samples were 47–50 Hz. These are not panel FPS or
a controlled benchmark. Further cached-background optimization remains polish.
No physical screen/touch or speaker audibility confirmation has been received.
Logs and captures are under ignored `build/deploy-*` and
`build/acceptance-routines-{local,esp32}`.

The same final acceptance passed 116 separate CLI invocations over USB. It
verified current button labels/IDs, all dental stages and healthy clickers,
actual bond changes, completion rejection, namespaced tuning, screenshot
transport/release, reconnect, and sleep/wake. The requested development tag was initially blocked because the configured
1Password SSH signer failed with “failed to fill whole buffer.” The flash above
succeeded independently; a later release record must identify the committed
source and tag. VERSION remains Release Please-managed at 0.1.1.
