# Procedural pet sounds

Seven original cues live in `core/sound.c`: chirp (two bubbles), happy (four-note
rise), sparkle (ascending chime), hello (oo–ah–ee babble), sleepy (descending
vowels), tap (65 ms rising blip), and coo (360 ms gentle voiced pair). Hello, sleepy, and coo are pet vocalizations, not intelligible text-to-speech.
The note/formant programs occupy 350 bytes and the sine table 128 bytes before
names/code/alignment. The synth state is 24 bytes or less. PCM is generated in
bounded blocks at 22,050 Hz, mono signed 16-bit; WAV samples are review exports,
not embedded recordings. No external voices or models are used.

After `make build`, generate the audition sheet:

```sh
python3 tools/audio/preview.py
open build/audio/preview.html
```

The exporter runs the exact portable C synth; the self-contained sheet embeds
seven WAV files. Source programs remain tracked; generated WAV/HTML files stay
under `build/audio/`. Native SDL and ESP32 use the same synthesis code.

For a running pet with a debug connection:

```sh
./scripts/jelli-debug sound hello --volume 35
./scripts/jelli-debug --port /dev/cu.usbmodemXXXX sound sparkle --volume 35
```

Volume is 0–80 (default 35); a response means queued, not proof of audible output.
SDL enables audio for interactive runs; add `--audio` for a headless run. For
non-audible integration checks use `SDL_AUDIODRIVER=dummy`. Accepted menu presses trigger tap at volume 25, at most once per 120 ms.
Voice-specific gameplay triggers remain polish work.
