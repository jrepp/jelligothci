# Procedural pet sounds

Nine original cues live in `core/sound.c`: chirp (two bubbles), happy (four-note
rise), sparkle (ascending chime), hello (oo–ah–ee babble), sleepy (descending
vowels), tap (65 ms rising blip), coo (360 ms gentle voiced pair), confirm (105 ms two-note rise), and pet
(170 ms voiced chirp). Hello, sleepy, and coo are pet vocalizations, not intelligible text-to-speech.
The note/formant programs occupy 450 bytes and the sine table 128 bytes before
names/code/alignment. The synth state is 24 bytes or less. PCM is generated in
bounded blocks at 22,050 Hz, mono signed 16-bit; WAV samples are review exports,
not embedded recordings. No external voices or models are used.

After `make build`, generate the audition sheet:

```sh
python3 tools/audio/preview.py
open build/audio/preview.html
```

The exporter runs the exact portable C synth; the self-contained sheet embeds
nine WAV files. Source programs remain tracked; generated WAV/HTML files stay
under `build/audio/`. Native SDL and ESP32 use the same synthesis code.

For a running pet with a debug connection:

```sh
./scripts/jelli-debug sound hello --volume 35
./scripts/jelli-debug --port /dev/cu.usbmodemXXXX sound sparkle --volume 35
```

Volume is 0–80 (default 35); a response means queued, not proof of audible output.
SDL enables audio for interactive runs; add `--audio` for a headless run. For
non-audible integration checks use `SDL_AUDIODRIVER=dummy`. Menu confirmation uses confirm; Back/Close uses the original tap cue; accepted
pet interactions and care actions use pet. Input sounds are limited to one per
120 ms. Automatic gain follows the persisted master volume (65% by default);
pet voices and idle coos use a softer gain than menu cues. Zero mutes automatic
sounds. The debug command remains an explicit volume override.
