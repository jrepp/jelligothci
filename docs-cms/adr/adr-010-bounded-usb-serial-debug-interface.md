---
title: Bounded USB serial debug interface
status: Proposed
created: 2026-10-08T02:01:24Z
deciders: Jacob Repp
tags: [architecture, debug, embedded]
id: adr-010
project_id: jelligotchi
doc_uuid: 1d8d4b5c-c3f0-448e-bf50-b6f8acef05ce
---

# Context

The user requested a small wired debug interface that can inspect screen state,
read visual output, and send button inputs to the live ESP32 game. The existing
USB Serial/JTAG console provides a wire without new pins or networking. Another
466x466 RGB565 framebuffer would cost 434,312 bytes; background access to the
engine buffer would also violate its ownership contract.

# Decision

Propose a versioned ASCII request/JSON response protocol on the existing USB
console, with a Python CLI and bounded portable C parser. An initial implementation
exists for review; Proposed records that the specific transport and capture
tradeoffs have not been separately approved or measured on hardware.

Handle requests on the engine thread between frames. Inject taps through normal
UI hit testing and semantic command validation. Expose the last rendered view,
current button labels/centers, and exact native framebuffer pixels as explicit
RGB565 hexadecimal words. Use one request at a time, fixed buffers, no new task,
and nonblocking driver reads/writes. The SDL host exposes the same protocol on
a user-only local Unix socket; the CLI selects local or serial transport. Send each response in one atomic driver
ring-buffer write so ordinary console output cannot split a response line.

For coherent screenshots, temporarily borrow the existing framebuffer by
suspending game updates. Read at most one 466-pixel chunk per request. Forgive
capture elapsed time and discard physical taps received while frozen. Release
explicitly, after five idle seconds, or at thirty seconds total. Neither capture
nor release changes the user's pause setting. Normal `state` reads do not freeze.

# Consequences

## Positive

- No extra framebuffer, task, or steady-state debug allocation.
- State/PNG evidence uses the same UI and pixels as the game renderer.
- Bounded work and timeout recovery when clients stop reading or disconnect.
- The portable protocol and CLI can be tested without a board.

## Negative

- Screenshot capture visibly pauses the game and ignores physical taps briefly.
- Hexadecimal encoding doubles raw pixel transfer size; this is inspection, not
  video streaming. Actual USB capture time still needs measurement.
- USB driver rings allocate at startup; driver/console overhead is additional.
- Input acknowledgment means delivered, not that gameplay accepted the action.
  Lost acknowledgments have an unknown outcome; clients must not blindly retry.

## Neutral

- One local client owns the serial port; close the serial monitor first.
- This is a development interface, currently enabled in firmware builds. A
  release build option can be added when production packaging is defined.
- No arbitrary memory reads/writes, shell execution, flash mutation, or remote
  networking is introduced.

# Alternatives Considered

A second full framebuffer would permit nonintrusive snapshots but increases
PSRAM use and copy cost. Live row reads without freezing can produce torn images.
Reconstructing a screen only from semantic state is smaller over the wire but
cannot reveal renderer errors. Continuous binary streaming or a second task
adds framing, concurrency, and backpressure complexity beyond this first tool.

# References

- [Engine/host ownership](adr-001-portable-c-core-and-host-adapters.md)
- [Injected timing](adr-002-inject-time-and-keep-pacing-in-hosts.md)
- [Debug protocol and validation](../memos/memo-009-wired-debug-interface-and-validation.md)
