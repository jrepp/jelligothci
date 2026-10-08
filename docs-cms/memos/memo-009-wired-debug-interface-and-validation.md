---
title: Wired debug interface and validation
author: Jacob Repp
created: 2026-10-08T02:01:24Z
tags: [debug, embedded, memo, validation]
id: memo-009
project_id: jelligotchi
doc_uuid: afd1803c-a11f-440c-9fb3-89b22cc1d4dd
---

# Overview

`scripts/jelli-debug` inspects the live ESP32 game's rendered state, lists visible
buttons, sends button/tap inputs, and captures a PNG plus matching JSON metadata.
The portable implementation is in `core/debug.c` and `core/debug_state.c`; the
USB adapter is `ports/esp32/main/debug_wire.c`. This implementation has passed
host tests and firmware compilation. It has not been flashed or exercised over
the physical wire in this increment.

Subsequent flash/startup evidence is recorded in
[memo-010](memo-010-pet-and-debug-firmware-deployment.md); the original test
boundaries below remain the evidence from interface implementation.

# Usage

Use [the README instructions](../../docs/playing.md#debug-cli) for port discovery,
firmware setup, and CLI commands. Only one client should open the port. The CLI
avoids intentional DTR/RTS reset pulses; OS/driver open behavior is not yet verified.
Do not confuse detecting a serial port with running this debug firmware.

`state` is a read-only JSON reply. Its `visual` object is the renderer's cached
view of the pixels currently presented, not a prediction of the next frame.
It contains page, active/stored pet IDs and forms, location, health/activity,
sleep, animation phase, pause/resume state, time availability, save status,
result text, relative day/minute, needs, bond, inventory, reward flags, and bedtime.
The separate `ticks` field is the current world clock. `buttons` contains visible slot IDs (0 for the bottom control, 1–6 for the
open ring), labels, icon IDs, and native pixel centers.

`press` resolves a label or ID from the current view and includes the expected
page. A page change rejects the request instead of pressing the same slot on a
different menu. Dynamic buttons such as REST/WAKE use their state at delivery.
Both `press` and `tap` enter normal UI hit testing on the engine thread; they do
not bypass inventory, sleep, or activity restrictions. A successful protocol
acknowledgment means input was delivered; read `state.visual.result` afterward
for the gameplay result. Taps outside buttons behave like physical taps.

`screenshot` obtains a capture token, reads the frozen framebuffer row by row,
releases it, and writes PNG plus `.png.json`. The PNG is renderer output, not a
camera image or panel readback. It cannot verify display wiring or physical color.

# Local desktop endpoint

The SDL app now uses the identical portable parser and renderer interface through
`ports/sdl/debug_socket.c`. `make run` enables `build/jelli-debug.sock` by default.
The CLI defaults to that checkout-relative endpoint when `--port` is omitted;
`--socket PATH` overrides it. macOS/Linux support follows the existing rendered
build support; this does not add native Windows SDL support.

The adapter is engine-thread-only, nonblocking, one client at a time, with a
64-byte receive buffer and the same 2 KiB reply buffer. It preserves partial
writes and closes stalled clients after two seconds of pending output. Disconnect
clears partial commands and releases captures. Normal shutdown closes descriptors
and removes only the path this process bound. Existing paths cause startup to
fail without unlinking them. Socket setup precedes opening a save session, so a
second instance that collides with the endpoint cannot rewrite the save first.

Capture skips simulation, rendering, and periodic save work while maintaining
SDL event polling. Quit/Escape still closes the game; other physical inputs are
discarded while frozen. Headless debug sessions pace frames and default to staying
alive; normal deterministic headless tests remain unchanged. Demo script events
scheduled during a capture are skipped, so scripted demos should be run without
an attached screenshot capture when checking their expected sequence.

The local integration test launches the real headless SDL executable and checks
button navigation, sleep/wake, full PNG capture, capture abandonment/reconnect,
partial-request isolation, endpoint collision/file preservation, and clean socket
removal. The suite now has 15 desktop/sanitizer tests; compiler-only tests remain
six. The CLI is also exercised against a running visible SDL window.

# Protocol v1

Requests are ASCII terminated by LF (CRLF also accepted):

```text
@J1 42 state
@J1 43 press 0 2
@J1 44 tap 114 332
@J1 45 capture
@J1 46 pixels 1 0 466
@J1 47 release 1
```

The first number is a caller-selected unsigned 32-bit request ID, echoed in the
reply. Responses start with a separating LF, then `@J1 ID ` and a JSON object,
then LF. `ok:false` includes an error string. Ignore unrelated console lines;
never treat a different request ID as your acknowledgment.

| Command | Arguments and behavior |
| --- | --- |
| `state` | Returns protocol version, dimensions, capture token or zero, rendered flag, world ticks, visual state, and buttons |
| `press PAGE BUTTON` | Page 0–5 and button 0–6; slot 0 is MENU/CLOSE/BACK; reject hidden rings, changed page, capture, or resume |
| `tap X Y` | Native coordinates 0–465; rejected during capture/resume |
| `capture` | Returns state plus nonzero capture token; requires a rendered frame and no existing capture |
| `pixels TOKEN OFFSET COUNT` | Row-major offset; count 1–466; returns token, offset, and four hex digits per RGB565 word |
| `release TOKEN` | Ends the matching capture; an expired/wrong token returns an error |

Page order is Home, Care, More, Collection, Settings. Form 0/1 is baby/grown;
location 0/1 is home/garden. Health is Well/Unwell/Recovering (0–2), activity is
Idle/Eating/Playing/Cleaning/Caring/Giving (0–5), and save status is
Unavailable/Pending/Saved/Failed (0–3). Needs are satiety, energy, hygiene,
amusement, social (0–1000). These numeric fields follow the public C enums.
RGB565 words are written most-significant hex digit first, independent of CPU
endianness. Pixels skip stride padding. The CLI expands 5/6-bit channels to RGB8
using bit replication and writes PNG through Python's standard library.

One request may be outstanding. Lines are bounded to 95 bytes before LF; invalid
characters and overflow discard the entire line through LF. Malformed prefixes
are silently ignored; recognized requests with invalid arguments return errors.
Responses fit 2,048 bytes. The device does not deduplicate request IDs; the CLI
never retries an input automatically. After a timeout, inspect state before
choosing a new action. Reconnects do not imply commands were canceled.

# Ownership, timeout, and resource budget

All protocol work runs on the engine task between frames. USB interrupts move
bytes only; they do not mutate gameplay. The adapter reads at most 64 bytes and
processes at most one complete request each iteration. A response is copied into
the USB TX ring in a single zero-timeout write; if full it is retried on later
iterations, then dropped after two seconds. The host is never required to read
for simulation to continue outside an active capture.

The capture holds the existing framebuffer, freezes animation and game updates,
and expires after five seconds without a valid pixel request or thirty seconds
in total. The adapter still polls and yields. It drains the bounded physical
input queue while frozen, including the release/expiry iteration. Elapsed capture
time is explicitly forgiven; resuming does not drain thirty seconds of backlog.
The user's pause flag is unchanged. Client failure triggers best-effort release;
firmware expiry is the fallback.

Measured/bounded resources:

- ESP32 link map: debug context 2,176 bytes, receive scratch 64 bytes, transport
  counters/timestamp 16 bytes: 2,256 bytes of adapter-owned static RAM.
- USB rings: 4,096-byte TX and 256-byte RX, allocated once at startup, plus SDK
  ring/driver/semaphore/interrupt metadata. This is not the complete heap delta.
- No new task/stack or framebuffer. Small token/formatting call frames use the
  existing engine stack; stack high-water measurement is still pending.
- At most 466 pixel reads and 1,864 hex pixel characters per request, plus JSON.
  A full screenshot carries 868,624 hex characters plus 466 response envelopes.
- Firmware image `0x9d9c0` bytes, 38% application partition free. Compared with
  the preceding MVP image `0x9aa30`, the current image adds 12,176 bytes.

The pinned USB driver source confirms that a ring-buffer write enqueues the
entire supplied response or returns zero. Console VFS writes use that same
driver; protocol lines cannot be split by task logging between separate writes.
Normal SDK console logging retains its existing bounded waiting behavior;
zero-timeout debug calls do not make all SDK logging nonblocking.

# Validation and findings

- `make test`: 14 tests pass, including new parser/capture tests and a CLI test
  against the actual C protocol and renderer through pipes.
- `make core-test`: six existing portable core/storage tests pass; debug/UI tests
  run with the rendered build and do not add SDL/Python to the compiler-only path.
- `make sanitize`: all 14 rendered tests pass under ASan/UBSan.
- `make lint-c`: formatting, clang-tidy, Cppcheck, and size/complexity gates pass.
- `make esp-build`: the actual USB adapter compiles against the pinned SDK.
- CLI help and serial discovery work through pinned pyserial 3.5 and local uv.
- The actual serial CLI also passed state, button navigation, and full PNG capture
  through a pseudo-terminal bridge to the C fixture, injecting console noise and
  a 16 ms delay per request. The resulting PNG was visually inspected. This tests
  serial client framing and pacing, not the USB hardware or ESP32 driver runtime.
- `make hooks-check`, docs validation/repair, shell syntax, and ShellCheck passed.
  Documentation required no repairs.
- Tests cover stale-page rejection, malformed/oversized input, numeric overflow,
  wrong/expired tokens, idle and total capture deadlines, padding exclusion,
  exact RGB565 chunks, bounded replies, no capture-time catch-up, navigation,
  sleep/wake, PNG chunk CRCs, ignored logs, and no automatic input retries.

Static analysis caught an unchecked enum conversion in button metadata; checking
the page before casting fixed it. Capture integration also needed frame-rate and
presentation timing to exclude frozen iterations, otherwise stale presentation
duration could underflow the diagnostic. These corrections are included.

# Remaining work

Verify the flashed board's state/input flow, serial-open/reset behavior, screenshot
latency and colors, disconnect recovery, console coexistence, heap delta, and
engine stack high-water mark. A host capture is not evidence of physical touch or
panel correctness. If transfer time or pause duration is excessive, consider
bounded RLE chunks or a reduced-resolution capture before adding another full
framebuffer. Add a production build toggle when release requirements call for it.

# References

- [ADR-010: Debug tradeoffs](../adr/adr-010-bounded-usb-serial-debug-interface.md)
- [MVP and polish backlog](memo-008-playable-pet-mvp-and-polish-backlog.md)
- [Process learnings](memo-004-process-learnings-and-context-remediation.md)
- [Protocol API](../../include/jelli/debug.h)

# Ring UI follow-up

The readability pass adds page 5 (moments) and slot 0 (MENU/CLOSE/BACK).
Only visible controls are listed; open MENU before pressing a category.
Visual state also includes menu_open, stat_index, stat_score, tile_phase,
clock_known, and clock_minute. Screen capture includes the rendered confetti.
See [memo-011](memo-011-readable-rings-moments-and-particles.md).

# External Device Acceptance Follow-Up

[Memo-012](memo-012-cli-acceptance-and-creature-tunables.md) supersedes the original
unverified USB status above. It records the serial-open reset found by the
external CLI test, the DTR/RTS remedy, successful local/device acceptance, and the
new bounded tunable commands.
