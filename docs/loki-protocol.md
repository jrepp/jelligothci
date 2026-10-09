# Loki protocol `loki/1`

How Loki Agent (the board) and Loki Bridge (the Mac) talk
([RFC-003](../docs-cms/rfcs/rfc-003-voice-agent-companion.md)). The board's codec
is `core/agent/loki.c`; the bridge's is the `LokiProtocol` Swift module in
`bridge/`. Both test against the golden vectors in `tests/vectors/loki-1.tsv`.

## Connection

- The board connects to `wss://HOST:PORT/loki/1`, offering the WebSocket
  subprotocol `loki.1`; the bridge listens (port 8766 by default) and accepts only
  that subprotocol.
- TLS 1.2 or later with mutual authentication. Each side has a self-signed ECDSA
  P-256 certificate and accepts only the peer certificate it stored when they were
  paired over USB, compared by the SHA-256 of the certificate's DER encoding. Names
  and validity dates are not checked; the pin is the trust.
- One connection at a time. The bridge refuses a certificate it has not paired
  during the TLS handshake, before any application data, and a new connection
  from a paired board replaces the old one (a board that rejoined Wi-Fi).
- The board says `hello` within 5 seconds of connecting. The bridge pings every 15
  seconds and closes a connection that has not answered by the next ping; both
  sides use TCP keepalive.
- Text frames carry control messages, binary frames carry audio. A control message
  is at most 2,048 bytes from the bridge and 512 bytes from the board; an audio
  frame at most 12 + 4,800 bytes. A larger message closes the connection (1009).
- The board sends `hello` first; the bridge answers `welcome`. Neither sends
  anything else before that exchange.

## Control messages

A control message is one JSON object, UTF-8, whose first key is `t` (the type),
with `seq` among the others. `seq`
starts at 1 for the first message in each direction on each connection and goes up
by one with every control message; a receiver closes the connection (1002) on any
other value.

Receivers are strict, so the two implementations cannot read one message two ways:

- The top level is an object and its first key is `t`. A repeated known key,
  invalid UTF-8, an unpaired surrogate escape, and anything after the object
  except whitespace are errors.
- Numbers in known fields are integers: no fraction, exponent, leading zero or
  `-0`, and each field has a range. Unknown fields are skipped, whatever their
  value, to a nesting depth of 4. Keys and type names are at most 32 bytes, and
  names are compared byte for byte.
- A known type with a missing, extra-typed or out-of-range field is an error. A
  well-formed message of an unknown type is ignored, so newer peers can add types.
- Strings are limited in bytes (UTF-8) as listed and contain no control
  characters (U+0000 to U+001F), except that `caption`, `text` and `detail` may
  contain line feeds. Identifiers (`id`) are 1 to 32 characters of
  `A–Z a–z 0–9 _ -`; nonces are 32 and digests 64 lowercase hex digits.

Senders write keys in the order shown, without spaces, so encoders can be compared
byte for byte. Optional fields with their default value are left out.

### Bridge to board

| `t` | Fields (in order) | Meaning |
| --- | --- | --- |
| `welcome` | `proto` (1), `bridge` (≤ 40), `voice` (≤ 40, optional) | Accepts the board's `hello` |
| `state` | `state`, `caption` (≤ 200, default `""`), `progress` (−1..1000, default −1), `queue` (0..999, default 0) | The unicorn's state; `progress` is per mille, −1 for none |
| `caption` | `text` (≤ 400), `final` (bool) | Live transcript; `final` when recognition ended |
| `ask` | `id`, `nonce`, `kind`, `title` (≤ 120), `detail` (≤ 600, default `""`), `options` (2–4 strings of ≤ 40), `timeout_ms` (1,000..600,000) | A question the board shows as a card |
| `ask_cancel` | `id` | The question is withdrawn |
| `reply_begin` | `reply` (1..2³²−1), `rate` (24000) | Reply audio follows as `reply` frames |
| `reply_end` | `reply`, `frames` (0..2³²−1) | The last frame of the reply was sent |
| `stop_audio` | — | Stop playback now and drop what is queued |
| `settings` | `volume` (0..100) | Speaker level |

`state` is one of `idle`, `listening`, `heard`, `thinking`, `working`, `asking`,
`speaking`, `success`, `partial`, `failure`. `kind` is `approve`, with exactly two
options of which 0 allows and 1 denies, or `choose`.

### Board to bridge

| `t` | Fields (in order) | Meaning |
| --- | --- | --- |
| `hello` | `proto` (1), `app` (`loki-agent`), `fw` (≤ 40), `board` (≤ 16), `rate` (24000), `channels` (1..4), `frame_ms` (20..100) | First message; the audio format of `mic` frames |
| `ptt_start` | `stream` (1..2³²−1) | The user started holding to talk; `mic` frames follow. One stream is open at a time, and each is numbered higher than the last on the connection |
| `ptt_end` | `stream`, `frames`, `dropped` (0..2³²−1 each) | The user let go of the open stream; `dropped` counts frames lost to a full send queue |
| `cancel` | — | The user asked to stop (PWR key or the card's stop) |
| `answer` | `id`, `nonce`, `digest`, `choice` (0..3) | The user tapped an option of the card `id` |
| `played` | `reply`, `complete` (bool) | Playback of `reply` finished or was cut short |
| `status` | `battery` (−1..100), `charging` (bool), `rssi` (−127..0) | Periodic health; −1 when the battery level is unknown |

An `answer` echoes the question's `id` and `nonce` and carries `digest`: the
SHA-256, in lowercase hex, of the card as the board decoded and showed it, written
in the canonical form below as `kind=… title=… detail=… options=[…]`. The quoting
makes the text unambiguous, so no other card has the same digest. The bridge
accepts the answer only if all three match an open question and the choice is in
range; an answer to a withdrawn, expired or already answered question changes
nothing.

## Audio frames

A binary frame is a 12-byte header and 16-bit little-endian PCM:

| Bytes | Field |
| --- | --- |
| 0–1 | `L`, `K` |
| 2 | kind: 1 `mic` (board to bridge), 2 `reply` (bridge to board) |
| 3 | flags: bit 0 marks the stream's last frame; the other bits must be 0 |
| 4–7 | stream: the `ptt_start` stream or the `reply` number, unsigned little-endian |
| 8–11 | sequence: 0 for a stream's first frame, then up by one |

The payload is 2 to 4,800 bytes of samples: mono for `reply` frames, and
interleaved over the `hello` channel count for `mic` frames, so its length is a
multiple of 2 × channels. A frame with another magic, kind or flag, or a payload
of another length, closes the connection (1003). Frames for a stream that is not open
are dropped. A gap in the sequence is not an error; the receiver counts it.

## Golden vectors

`tests/vectors/loki-1.tsv` has one vector per line, tab-separated:
`name`, `direction` (`b2d` bridge to board, `d2b` board to bridge), `verdict`
(`ok`, `ignore` or `reject`), the input, and for `ok` the canonical form. Fields are
not trimmed. Inputs are the exact text of a control message; `json-hex:` and the
bytes of a control message that holds characters a text file cannot carry (control
characters, invalid UTF-8); or `hex:` and the bytes of an audio frame. Lines
starting with `#` are comments.

The canonical form is the type, then each field in the table order as
`key=value`, separated by single spaces. Integers are decimal, booleans `true` or
`false`, enumerations (`state`, `kind`) bare, and strings are quoted with `\\`, `\"` and `\n` escaped and every other
byte below 0x20 or above 0x7E written as `\xHH` (so UTF-8 shows as its bytes).
Arrays are `[` and their elements joined by `,` and `]`. Defaults are written out.
Audio frames are `audio` with `kind`, `final`, `stream`, `seq` and `bytes`.

The board's tests decode every `b2d` vector; the bridge's tests decode every
vector in both directions, since its simulated device receives what the bridge
sends. Each side also checks that every message its encoder produces in the tests
appears in the file as an `ok` input of the direction it sends, so the board's
encoder output is decoded by the bridge's tests and the bridge's by the board's.
