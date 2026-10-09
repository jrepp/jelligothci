---
title: Agent phase 0 audio spike, duplex audio, Wi-Fi, and the Mac link
author: Claude
created: 2026-10-09T16:54:24Z
tags: [agent, audio, esp32, memo, network]
id: memo-025
project_id: jelligotchi
doc_uuid: c68ebbea-788d-4710-929b-a89aeed6a23e
---

# Overview

The second part of [RFC-003](../rfcs/rfc-003-voice-agent-companion.md) phase 0,
after [memo-024](memo-024-agent-phase0-two-apps-and-hardware-probes.md): the agent
app gained Wi-Fi, full-duplex audio, and a spike link to the Mac, so the audio path
can be measured before the real bridge exists. This memo records the design, what
the hardware showed, the end-to-end stream measurement, and the review outcome.

# Context

- Board: the 1.75C of memo-024, running the agent from `ota_0` on trial-confirmed
  boots; the pet's save is untouched throughout.
- Chatter (the user's local TTS app) renders 24 kHz, 24-bit mono WAV. A test phrase
  of 3.66 s took 1.92 s with the Ryan voice in save mode, about half real time.
- The open-source xiaozhi firmware drives this board's audio at 24 kHz with the
  ES8311 in standard I2S and the ES7210 in TDM on one port (its `BoxAudioCodec`).

# Details

## Design

- **Audio** (`ports/esp32-agent/main/audio.c`): one I2S port, the ES8311 speaker in
  standard stereo mode and the ES7210 in TDM with four slots, both at 24 kHz so
  Chatter's output plays without resampling. Codec settings follow the BSP; the
  speaker uses the pet's calibrated volume curve (memo-021). Capture delivers all
  four TDM slots, in the ES7210's order MIC1, MIC3, MIC2, MIC4. MIC1 runs at 30 dB
  gain; the others keep the codec default.
- **Wi-Fi** (`wifi.c`, `settings.c`): station mode with credentials provisioned over
  the USB console (`scripts/jelli-agent wifi set` prompts without echo and sends
  hex). Credentials live in `agent_nvs`; the driver keeps its state in RAM and the
  PHY calibrates at each start, so nothing opens the pet's `nvs`, and `wifi.c`
  refuses to build with a configuration that would.
- **Link** (`link.c`): on `mic stream N` (1..30 s) the board opens an unencrypted
  WebSocket to the Mac address stored with `link set`, sends 60 ms frames of the
  four slots (a 12-byte header, `core/agent/args.c`), and plays any mono PCM the
  Mac sends between `play_begin` and `play_end`, after a 300 ms lead, from a 128 KB
  PSRAM ring. Network reads can split samples; `core/agent/pcm.c` rejoins them.
- **Mac** (`tools/agent/spike_server.py`): records each stream to a four-channel WAV
  with a JSON report (frames, missing frames, arrival jitter, levels) and can
  answer with a WAV or a Chatter rendering, played while the board streams.

## Hardware results so far (1.75C)

`audio test 2000` plays a 1 kHz tone at −12 dBFS while capturing every slot and
measures each slot's level and the share of its energy at 1 kHz (Goertzel):

| Slot | RMS | Tone share | Reading |
| --- | --- | --- | --- |
| MIC1 | 12,313 (−8.5 dBFS) | 99% | Main microphone at 30 dB gain, hearing the speaker |
| MIC3 | 237 (−42.8 dBFS) | 100% | The echo reference: a pure electrical copy of the output |
| MIC2 | 446 (−37.3 dBFS) | 100% | Second microphone at 0 dB, about 30 dB below MIC1 as its gain predicts |
| MIC4 | 1 | — | Not connected |

The board therefore has two microphones and the echo reference RFC-003 assumed, on
TDM slot 1; the reference needs gain before acoustic echo cancellation can use it.
With Wi-Fi started and audio open, the agent had 173,951 bytes of internal RAM free
(minimum 128,708), against 333,963 before Wi-Fi and audio (memo-024). The image is
about 1 MB in the 4 MB `ota_0`.

The PWR key test with the line floating saw no change in 10 s; whether a press
happened is unconfirmed. The probe now offers only the GPIO's internal pull-up
(a pull-down could read as a held PWRON press, which powers the board off at 6 s).

## End-to-end stream over Wi-Fi (2026-10-09)

The board joined the home network (`loki-tg`, channel 2, RSSI −72 dBm) with
credentials typed into `scripts/jelli-agent wifi set`. The spike server rendered a
reply with Chatter (Ryan, 14.12 s of speech in 4.76 s, about 0.34 of real time) and
played it while the board streamed `mic stream 8`:

| Measure | Result |
| --- | --- |
| Frames | 133 (7.98 s of four-slot audio), none missing, duplicated, or reordered |
| Frame arrival at the Mac | Mean 63.9 ms (60 ms nominal), p95 96.5 ms, max 260 ms |
| Board send time per 11.5 KB frame | Mean 39.9 ms, max 231 ms |
| Reply playback | 677,760 bytes received, all played, none dropped; drained after the stream |
| Echo reference | MIC3 matches the reply at a correlation of 0.97–0.99 in every speech window |
| Reply on the speaker | About 0.14 s after the first captured sample (network, 300 ms lead, DMA) |
| Levels during the reply | MIC1 −8 to −27 dBFS, MIC3 −41 to −55 dBFS, MIC2 −38 to −57 dBFS |
| Internal RAM, session minimum | 104,624 bytes |

The capture lost audio while Wi-Fi sends stalled. Aligning the reference with the
reply in one-second windows, the offset drifts by about 100 ms over 5 s (−141 ms to
−39 ms), so roughly 2% of the captured audio was overwritten in the I2S buffers;
the reply itself played without gaps. Capture and sending share one loop, and a
send that takes longer than the 60 ms the DMA buffers hold drops samples. On this
link (1.5 Mbit/s of four 24 kHz slots at −72 dBm) that happens a few times in 8 s.
Phase 1 therefore needs a sender decoupled from capture by a PSRAM queue, and
should send fewer channels (a microphone and the reference, 768 kbit/s) or
compress; the echo reference also needs gain (it sits 30–40 dB below MIC1).

## Review

Three security review rounds and three test rounds ran on this change; findings of
medium severity or more went to two skeptic reviews each.

- Fixed: a stalled or trickling peer could hold the WebSocket client's lock and
  wedge the console. The link now owns its transports, ends a session by shutting
  its socket down when a frame stalls or claims more than 16 KiB, uses TCP
  keepalive, and tears down with a bounded close. Odd-length reads, continuation
  frames, and leftover playback between sessions were fixed. The spike server now
  needs `--board`, runs one session at a time, caps a recording at 35 s and a run
  at four recordings, and ignores malformed messages; the client validates
  `link set` hosts (a newline could have injected a second console request).
- Refused by both skeptics: a claim that server pings end a stream (the transport
  answers pings itself without raising a data event), and a one-session bug that was
  a test agent's temporary mutation.
- Noted, not fixed: a millisecond-wide window at the end of a slow connect where
  teardown could still block; a theoretical socket-number reuse race; Wi-Fi
  credentials remain in plain text in `agent_nvs`, driver RAM, and the USB receive
  ring; with the external transport the client never sees the server's pongs, so a
  session longer than 120 s would drop (sessions are capped at 30 s; phase 1 must
  enable control-frame propagation if it keeps this design).

# Remaining uncertainty

- One stream on one link was measured; how often sends stall at better signal, and
  how the loss sounds to a recognizer, are open. Playback quality was not judged
  by ear in this run.
- MIC2 and MIC3 gains are untuned; whether 24 kHz capture suits on-device ASR or
  the Mac's recognizer as is, or needs resampling, is open.
- The PWR key's pressed level is still unobserved.

# Next

1. Phase 1 (on the user's go-ahead): the Mac bridge, mutual TLS, a buffered
   sender, and the reference gain the echo canceller needs.
2. The render spike is in [memo-026](memo-026-agent-phase0-render-spike.md).
