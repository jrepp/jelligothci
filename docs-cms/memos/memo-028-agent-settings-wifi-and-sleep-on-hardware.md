---
title: Agent Settings, Wi-Fi and screen sleep on the 1.75C
author: Claude
created: 2026-10-10T16:20:19Z
tags: [agent, esp32, hardware, memo, network, ui]
id: memo-028
project_id: jelligotchi
doc_uuid: e54943d2-c8e0-4358-8fd9-8a40c7352a84
---

# Overview

This memo records what was verified on the 1.75C board after the Settings, Wi-Fi and
screen sleep firmware ([ADR-016](../adr/adr-016-on-device-settings-wifi-and-screen-sleep.md))
was flashed, and what is still open. It is the hardware half of that work; the build and
test results are in the commit that introduced it (3766497).

# Context

ADR-016 added a Settings screen on the side button that is not power (BOOT), Wi-Fi
scanning, selection and password entry on the board, and screen sleep after two idle
minutes with double-tap or side-button wake. Touch, the button and a real join needed a
person at the board. A desk camera (camctl, `~/dev/camctl`) now lets Claude look at the
screen without touching the board.

# Details

## Verified, 2026-10-10

| Check | How | Result |
| --- | --- | --- |
| BOOT opens Settings | Michael, by hand | Works |
| Wi-Fi screens (network list, selection, password entry) | Michael, by hand | Work |
| Screen sleep | Camera pictures at the "board" position, nobody touching the board | Lit with the unicorn at 16:18:25 UTC, dark at 16:20:08 UTC |
| Agent screen while awake | Camera, 4K | Unicorn drawn full size, centred, colours correct |

## Bridge link before the check

The Loki Bridge log (a run started earlier on 2026-10-10) shows the board, Loki Agent
423354 on firmware v0.2.0 at 192.168.68.69, dropping and rejoining several times before
09:58 local time ("no answer to a ping", then "Operation timed out"), and then staying
connected without a drop until at least 12:20. The drops were not investigated.

# Recommendations

Still unverified on hardware:

- A real join through the Settings screens with a typed password, including a wrong
  password (the stored network must stay unchanged) and the slow 30-second retry.
- Double-tap wake and side-button wake from a dark screen, and that neither waking tap
  presses anything.
- Why the link dropped repeatedly before 09:58: Wi-Fi power save, the board's ping
  timing, or the network. Watch the bridge log for drops after the next flash.

# References

- [ADR-016: On-device settings, Wi-Fi and screen sleep](../adr/adr-016-on-device-settings-wifi-and-screen-sleep.md)
- [memo-027: Link bring-up](memo-027-agent-phase1-link-bring-up.md)
- [Hardware bring-up](../../docs/hardware.md#hardware-bring-up)
