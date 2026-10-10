---
title: Agent phase 1 link bring-up, mutual TLS between Loki Agent and Loki Bridge
author: Claude
created: 2026-10-09T21:14:15Z
tags: [agent, esp32, memo, network, security]
id: memo-027
project_id: jelligotchi
doc_uuid: df0f6747-7ce2-4dfb-b782-a96de8982567
---

# Overview

The first [RFC-003](../rfcs/rfc-003-voice-agent-companion.md) phase 1 increments
connect the board to the Mac for real: the [`loki/1`](../../docs/loki-protocol.md)
protocol with shared golden vectors, Loki Bridge's link (a Swift package in
`bridge/`), and the board's identity, USB pairing, and link client. This memo records
what the hardware showed while bringing the link and its audio up, what was fixed,
and what is still open.

# Context

- Board: the 1.75C of memo-024 (`7C:0C:5F:42:33:54`), on the home network at about
  −80 dBm; the Mac at `MacBook-Pro-4.local` on the same LAN.
- The board's link: ESP-IDF 5.5.5, mbedTLS 3.6.6 (allocating from PSRAM),
  `esp_websocket_client` 1.8.0 over SSL and WebSocket transports the link owns
  (memo-025's stall-safe teardown).
- The bridge: Network.framework TLS and WebSocket, each side pinning the other's
  certificate by its SHA-256.

# Details

## What worked

- USB pairing: `loki-bridge pair` read the board's fingerprint, sent the bridge's
  certificate in 96-byte parts, and committed the Mac's name, port 8766, and its
  IPv4 address; the board checked the certificate against its fingerprint.
- The board connected over Wi-Fi, both certificates were accepted, and `hello` and
  `welcome` were exchanged. A `state` and an approval `ask` from the bridge reached
  the board's C decoder; the board's `answer` carried the card's digest and the
  bridge accepted it. The session then ran for more than 15 minutes with pings both
  ways and a `status` every 30 seconds (RSSI −77 to −81 dBm) without a drop.

## What failed first, and the fixes

| Observation | Cause | Fix and check |
| --- | --- | --- |
| The first start after flashing rolled back to the pet | Making the key and certificate on the main task needed about 4.2 KB of stack; the main task had 3.5 KB | `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`; a build that forced a new key logged 362 ms and 3,980 bytes spare, and loading an existing identity leaves 5,884 |
| Every session ended right after the WebSocket handshake ("hello failed") | The board reported its long label (`ESP32-S3-Touch-AMOLED-1.75C`, 27 bytes) where `hello` allows 16, so its own encoder refused the message | `JELLI_BOARD_ID` (`1.75`, `1.75c`) per board; the bridge then reported the board connected |
| About one attempt in three failed to resolve `MacBook-Pro-4.local` (`getaddrinfo` 202) | mDNS answers through lwIP are not reliable at −80 dBm | The link dials the last address that resolved, else the IPv4 address the bridge gave at pairing (`pair commit … IPV4`) |
| (From memo-025) a client with an external transport drops healthy sessions after its pong timeout | `esp_websocket_client` applies none of its transport settings to an external transport, so it never saw pongs | The link sets path, subprotocol, and control-frame propagation on its WebSocket transport itself; the 15-minute session confirmed pings both ways |

The bridge's listener first lost every connection: its state handler held the
pending connection weakly, so nothing kept it alive past accept. Pending
connections are now held until they are ready or fail; the loopback tests caught it
before the hardware did.

## Audio over the link (2026-10-09)

With `loki-bridge serve --echo` and the board's console standing in for the touch
screen:

- Hold to talk while the Mac said a sentence aloud: 4.56 s arrived as 76 frames with
  none dropped, SpeechAnalyzer transcribed it ("In Kotter Gotchi, run the tests,
  please": the project name was misheard despite the project names given as
  contextual strings), and Chatter rendered the echo in Scar in 2.4 s. The board
  played it and reported `played` complete.
- A reply that began while the board was listening was not played and was answered
  at once with `played` not complete; a reply the bridge stopped part way was cut off
  and answered the same way; both left the session up.
- Pressing to talk again before the last utterance's `ptt_end` had gone is refused
  as busy, rather than opening a second stream, which the bridge closes the
  connection for.
- Chatter takes 2.3 to 6.2 s to render a sentence, so replies start late; RFC-003's
  render-mode change in Chatter is what shortens that.

Flashing once failed partway (esptool got no answer from the bootloader and the
board needed a power cycle) while the agent ran a live session. Restarting into the
pet first (`loki-agent boot pet`) and then flashing worked every time since.

## Review state

The protocol increment passed its security review with low findings only (fixed:
the C decoder clears its output on every refusal, the Swift decoder compares names
by their bytes, and the specification states the 32-byte name limit; open: approval
cards must never shorten what they ask about).

The link, pairing, and audio review found five medium defects, each upheld by two
skeptics, all about teardown and trust that outlive their session:

| Finding | Fix |
| --- | --- |
| A send that failed while a reply played deadlocked the link, console, and microphone: the client's error events run inside the failing sender, which then waited on the send lock it held | Session-end events mark the link offline first and drop playback without sending; a send from a task that already holds the lock is refused |
| Teardown freed the client and transports while a sender could still be inside them | Senders check and use the client only under the send lock, and teardown frees it under that lock once the client's task has finished |
| A host answering at the bridge's address could stall the TLS handshake forever, and teardown waited on it | The link finds the session's socket by its peer (the client hides it until the handshake ends) and shuts it down each second until the client's task finishes, so no read, write, or handshake can hold the link |
| The bridge kept any number of half-open connections with no deadline | Each handshake has 10 s, and at most 8 are in progress; a new one ends the oldest |
| `loki-bridge unpair` did not reach a running bridge | The pairing store rereads its file whenever it changes and trusts no one if it cannot read it; the link drops an unpaired board (close 1008) at its next message or ping |

Smaller fixes from the same review: the board refuses a reply while the user holds
to talk; the bridge keeps at most 30 s of an utterance; the board makes its key only
once Wi-Fi has started; and a host name must end in a label that starts with a
letter, so no name reads to lwIP as an octal or hex address. Answers to approval
cards still do not expire with their session; that waits for the board's card UI.

The speech review (Chatter client, transcriber, `serve`) found two findings the
skeptics both rated low. Utterance recordings had no limit or retention: they are
now opt-in (`serve --recordings`), private (folder 0700, files 0600), and only the
last 20 are kept. And the bridge, like Chatter's own clients, sends Chatter's bearer
token to whatever listens on 127.0.0.1:18423, so a process that took the port while
Chatter was down would receive it; closing that is Chatter's to do (a socket in a
private folder, or a challenge instead of a bearer token) and is noted with
RFC-003's Chatter changes. Lower findings were fixed: Chatter replies are size- and
time-capped, redirects, proxies and caches are off, error text is stripped of
control characters before it reaches the terminal, the job id must be 1 to 64
characters, the token file must be the user's own and unreadable by others, audio
files with extreme lengths or rates are refused, and echo replies run one at a time.

# Remaining uncertainty

- How often mDNS fails on a better signal, and whether the board should also learn
  a new address when the Mac's DHCP lease changes, is open; re-pairing fixes it today.
- The bounded teardown is reasoned from the client and lwIP sources and covered by
  loopback tests on the bridge side; a stalled handshake against the board has not
  been staged on hardware.
- The speaker's output was confirmed only by the board's `played` reports, not by
  ear, in this session.
- The board's battery level is not yet in `status` (it reports −1).
- Physical display and touch are unchanged from phase 0 and were not re-verified.

# References

- [docs/loki-protocol.md](../../docs/loki-protocol.md),
  [hardware guide: pairing](../../docs/hardware.md#pairing-with-loki-bridge-phase-1)
- [memo-025: audio spike](memo-025-agent-phase0-audio-spike.md)
- [ADR-014: voice agent autonomy](../adr/adr-014-voice-agent-autonomy-any-project-any-command.md)
