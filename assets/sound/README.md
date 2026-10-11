# Procedural pet sounds

Procedural sound cues are implemented in [`core/sound.c`](../../core/sound.c).
Hello, sleepy, and coo are pet vocalizations, not intelligible text-to-speech.
The output contract is mono signed PCM at the rate and block bound in
[`include/jelli/sound.h`](../../include/jelli/sound.h). WAV samples are review
exports, not embedded recordings. No external voices or models are used.

After `make build`, generate the audition sheet:

```sh
python3 tools/audio/preview.py
open build/audio/preview.html
```

The exporter runs the exact portable C synth; the self-contained sheet embeds
audition WAV files. Source programs remain tracked; generated WAV/HTML files stay
under `build/audio/`. Native SDL and ESP32 use the same synthesis code.

For a running pet with a debug connection:

```sh
./scripts/jelli-debug sound hello --volume 35
./scripts/jelli-debug --port /dev/cu.usbmodemXXXX sound sparkle --volume 35
```

The command's volume option is a diagnostic override; a response means queued,
not proof of audible output.
SDL enables audio for interactive runs; add `--audio` for a headless run. For
non-audible integration checks use `SDL_AUDIODRIVER=dummy`. Menu confirmation uses confirm; Back/Close uses the original tap cue; accepted
pet interactions and care actions use pet. Input sounds are coalesced to avoid
overlap ([`core/pet_feedback.c`](../../core/pet_feedback.c)). Automatic gain
follows the persisted master volume;
pet voices and idle coos use a softer gain than menu cues. Zero mutes automatic
sounds. The debug command remains an explicit volume override.
