---
title: Hold to talk and petting the unicorn on the 1.75C
author: Claude
created: 2026-10-10T16:40:40Z
tags: [agent, audio, esp32, hardware, memo, ui]
id: memo-029
project_id: jelligotchi
doc_uuid: 531df417-c343-4f21-ac7b-625745dbea63
---

# Overview

This memo records how hold to talk and petting
([ADR-017](../adr/adr-017-hold-to-talk-and-petting-the-unicorn.md)) were built and
checked: on the desktop, in the firmware build, and on the 1.75C board.

# Context

Michael asked that sending speech to Loki Bridge need a press held on the screen, and
that petting the unicorn make it frolic. The voice path (`ptt_start`, MIC frames,
`ptt_end`) already existed behind the serial console.

# Details

## Built

- `core/agent/touch.c` reports HOLD, RELEASE and STROKE beside TAP and DRAG; holds are
  switched on only for the unicorn's screen.
- `core/agent/prance.c` adds the frolic (5,450 ms), checked by every routine test,
  including the art test that measures the drawn unicorn against the round panel.
- The board's controls start and end the utterance, draw the edge ring, and start the
  frolic; their console commands moved to `ui_controls_console.c` to keep both files
  under the 400-line limit.

## Verified, 2026-10-10

| Check | How | Result |
| --- | --- | --- |
| Install | `make esp-agent-install` on the 1.75C, agent image 0x2b8c30 bytes | Booted, bridge online again |
| Petting | `ui pet`, pictures from the desk camera during the routine | The unicorn shrank to prancing size and moved across the screen |
| Hold to talk | `ui hold start`, then `ui hold stop` about 1.5 s later | `screen status` showed `listening` true while held and false after; the lilac ring lined the screen's edge in a camera picture; the bridge received the utterance, heard a fragment of room speech, and asked for the request again |
| By finger | Michael held the screen and stroked the unicorn | Both worked |

## Review

The security review found two medium defects, each confirmed by two skeptic reviews (one
judged it low on exploitability), and both were fixed:

- A press that began on a dark screen or in Settings could become a hold when the
  screen woke or Settings closed with the finger still down. Whether a press may become
  a hold is now decided when the finger lands.
- Opening Settings or the screen going dark during a hold left the utterance running
  with no ring. Both now end it.

Lower findings, also fixed: an utterance begun from the console and one from the screen
could interleave (a mutex now serialises the start, and the end clears only its own
stream); a touch controller that stops answering ends a hold after three failed reads;
the render bench ends an utterance while it owns the panel; voice start-up publishes
its lock only once its queue and tasks exist. The UI task's stack grew to 6 KB, since a
hold now sends on the link from it: after a hold and a second hold that stopped a
spoken reply, `screen status` reported 3,524 bytes never used.

The test review added a seeded property test of holds (400 seeds of 1,500 samples) and
boundary tests; together the touch tests kill 15 of 16 mutants of the sampler, and the
frolic test all 6 of the routine's. Suites: `make test` 108 of 108, `make core-test` 51
of 51, `make sanitize` 108 of 108, `make lint-c` clean.

# Recommendations

Still unverified on hardware:

- The grey refusal blink: it needs the bridge offline or an utterance still open.
- How much speech the 400 ms before the ring cuts off in daily use.

# References

- [ADR-017: Hold to talk and petting](../adr/adr-017-hold-to-talk-and-petting-the-unicorn.md)
- [memo-028: Settings, Wi-Fi and sleep on the 1.75C](memo-028-agent-settings-wifi-and-sleep-on-hardware.md)
- [Hardware bring-up](../../docs/hardware.md#hardware-bring-up)
