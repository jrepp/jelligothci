---
title: SDL debug startup review
author: Engineering Team
created: 2026-10-10T08:57:34Z
tags: [memo, stability, testing]
id: memo-038
project_id: jelligotchi
doc_uuid: da2fc6e6-cd36-46f4-a73f-1d826bccdc5b
---

# Observation

PR #16's Linux sanitizer run failed in `debug_local`: the second SDL process
accepted a state request before its first rendered frame. The client correctly
rejected `rendered: false`. The socket opens before engine initialization, and
the first run-loop iteration previously polled it before rendering. Local macOS
validation did not reproduce the timing on its first pass.

# Remedy

The SDL host now gates debug polling on `pet->ui.rendered`. The first normal
frame initializes the pixels and render key; queued socket requests are served
on the next iteration. This also protects capture and input clients from the
same startup window without changing the protocol or retrying commands.

# Verification

The existing `debug_local` integration test covers immediate connection, state,
input, capture, reconnect, and normal socket cleanup. Re-run desktop and
sanitizer suites and repeat that integration test on the sanitizer build before
landing. CI must validate the updated commit. Hardware behavior is unchanged.

# References

- [Failing Linux sanitizer run](https://github.com/jrepp/jelligothci/actions/runs/38039569395)
- [Process learnings](memo-004-process-learnings-and-context-remediation.md)