---
id: memo-029
title: Actionable recovery guidance and countdown
author: Codex
created: 2026-10-09
tags: [care, interface, validation]
project_id: jelligotchi
doc_uuid: 632bc75f-fe75-4843-ae71-f8fb70d3860d
---

# Observation and remedy

NEEDS CARE did not explain how to restore health. The home status now reads
CARE > BASIC CARE. Starting treatment shows RECOVERING 30S and counts down,
rounding remaining ticks up to the next second. The render key tracks this
value so progress updates even with a held animation frame. No game or save
format change is needed.

# Verification

The navigation test verifies 30 seconds, the transition to 29 seconds, redraw
on countdown change, and completion. Host tests pass 42/42, core tests 21/21,
and sanitizer tests 42/42. C lint and the ESP32 build pass. Inspected native-size
captures in build/recovery-needed.png and build/recovery-progress.png show
readable status text. Physical display and touch remain unverified because the
board has not responded to the latest USB connection attempts.

# References

- [Playing guide](../../docs/playing.md)
- [Display investigation](memo-024-esp32-display-mismatch-diagnostics.md)
