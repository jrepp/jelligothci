---
id: memo-037
title: Offline behavior timeout review
author: Codex
created: 2026-10-10
tags: [behavior, persistence, testing]
project_id: jelligotchi
doc_uuid: ea6878a4-50d6-4487-9e38-a6738a6e4f16
---

# Observation

Review of PR #14 found that offline catch-up called the timeout action before
checking the offline flag. A saved unanswered potty request therefore created
an accident while the application was closed. This contradicted RFC-005's
contract that offline behavior only expires states and avoids unseen outcomes.

# Remedy and regression

The countdown now receives the offline flag and suppresses accident actions
during catch-up. State expiration and cooldowns still advance. Live unanswered
requests still create accidents; existing messes retain their normal decay.

The behavior regression resumes a two-second potty request for five seconds
and checks that the state expires without creating a mess or clearing the urge.
It failed on the original implementation. Validation during landing includes
core, desktop, sanitizer, static-analysis and ESP32 builds. No hardware flash or
physical display/touch verification is part of this review.

# References

- [Behavior proposal](../rfcs/rfc-005-stimulus-driven-creature-behaviour.md)
- [Behavior tests](../../tests/test_behavior.c)
- [PR 14](https://github.com/jrepp/jelligothci/pull/14)