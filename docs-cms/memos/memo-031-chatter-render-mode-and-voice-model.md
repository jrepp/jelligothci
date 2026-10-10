---
title: Chatter render mode, cancel and the voice model
author: Claude
created: 2026-10-10T18:58:36Z
tags: [agent, audio, bridge, chatter, latency, memo]
id: memo-031
project_id: jelligotchi
doc_uuid: 5d1574aa-a233-4e96-9890-33e5c47eb485
---

# Overview

This memo records the levers from [memo-030](memo-030-voice-reply-turnaround.md) that
Michael chose on 2026-10-10, what changed in Chatter and in Loki Bridge, and what was
measured. Chatter 3.2.0 adds the phase 1 items of
[RFC-003's Changes to Chatter](../rfcs/rfc-003-voice-agent-companion.md#changes-to-chatter):
a silent `render` mode that respects `quality` and keeps its audio briefly, and a
synchronous result (`?wait=`). Its cancel no longer stops audio on the Mac unless the job
was playing. The bridge renders at Responsive quality, cancels the render it abandons,
and runs every voice session on Sonnet 5.5 at low effort.

# Context

memo-030 left three costs, each Michael's decision:

1. **Studio rendering.** The bridge used `mode: "save"`, the only silent mode, and
   Chatter forces Studio quality for every save (the 1.7B model, about real time for Scar).
2. **Cancel.** Chatter's cancel stopped whatever it was playing on the Mac, so the
   bridge never cancelled the render it abandoned. A reply after an interruption waited
   for it: "Status" took 5.3 s.
3. **Model time.** Claude's first words came 1.8 to 4.2 s after the release with the
   account default model (Opus 5.5) and its default effort.

Michael asked for all three. For the model he chose Sonnet 5.5 at low effort, for every
voice session (general and project).

# Details

## Chatter 3.2.0

- `mode: "render"` generates audio silently at the requested quality (or Chatter's live
  quality). Nothing plays on the Mac and nothing is written to the output folder. The
  audio is downloadable for about 10 minutes, then deleted. Render is for single-voice
  speech; dialogue keeps play and save.
- `?wait=true` (or a number of seconds, at most 30) on `POST /v1/speech` and
  `GET /v1/jobs/{id}` holds the reply until the job finishes; at most 8 such waits per
  credential. The listener's 10 s idle limit no longer applies while Chatter prepares a
  reply.
- Only `play` jobs start, stop or wait for the Mac's playback, so cancelling a render
  stops its generation at the next frame and leaves the Mac's audio alone.

## Loki Bridge

- `ChatterClient.render` posts `mode: "render", quality: "responsive"`, then waits with
  `GET /v1/jobs/{id}?wait=20` (inside the session's 30 s request timeout) and fetches the
  audio. A Chatter older than 3.2 answers render with 400 "Mode must be play or save.";
  the client then uses `save` as before and never cancels it.
- Cancelling the task that awaits a render, which the Speaker does when the user talks
  over a reply or a reply ends early, sends `DELETE /v1/jobs/{id}` from a detached task.
- `SessionPolicy` sets `model: "claude-sonnet-5-5"` and `effort: "low"` on every session.

## Measurements

Chatter 3.2.0 on the Mac, the Scar voice (a cloned voice), warm models, a reply of about
3 to 4 s ("Two plus two is four. Anything else I can check for you?"), seconds from the
request to a finished WAV:

| Mode and quality | Model | Seconds |
| --- | --- | --- |
| `save` (Studio, as before) | 1.7B | 3.4 to 3.7 |
| `render`, Studio | 1.7B | 3.7 to 3.8 |
| `render`, Balanced | 0.6B, 8-bit | 1.8 |
| `render`, Responsive | 0.6B, 8-bit | 1.0 to 1.1 |

Balanced is slower than Responsive on the same model because it decodes the whole
reference recording with each passage; Responsive decodes only what it generated. The
Responsive render transcribed word for word (Whisper medium).

Cancel: a render of six long sentences was cancelled after 1.5 s. Chatter answered in
2 ms, and a short render queued behind it finished 0.9 s after it was queued, where
before it would have waited for all six sentences.

The bridge's `ChatterClient`, against Chatter 3.2.0 and against Chatter 3.1.1, Scar,
seconds from `render` to the WAV:

| Step | Chatter 3.2.0 (render, Responsive) | Chatter 3.1.1 (falls back to save) |
| --- | --- | --- |
| "Two plus two is four." | 0.54 | 8.1 (Studio model loading) |
| A sentence of about 4 s | 1.05 | 4.1 |
| A short reply after an abandoned six-sentence render | 0.60 after the interrupt | 21.1 after the interrupt |

On 3.1.1 the client asked for render, was refused ("Mode must be play or save."),
rendered with `save` and never cancelled it, as designed.

Not yet measured: the turnaround on the board with these changes and with Sonnet 5.5 at
low effort. memo-030's method (hold, speak with `say`, release, read the bridge's log)
applies.

## Review

Each repository had a security review and a test review with fresh context, and each
behaviour-changing fix was reviewed again on its own.

- **Chatter.** The security review's one medium finding was that a render archived from
  the Activity list within its 10 minutes kept its audio downloadable until normal
  retention. Two skeptic reviews confirmed it; one rated it low, since no new party gains
  access. It is fixed: the sweep reads the job database, archived rows included, and
  downloads check the file's age themselves. The test review found that the idle-limit
  pause also covered sending a reply, so a client that stopped reading a download held
  its connection for 120 s instead of 10 s. Fixed and pinned by a test.
- **Bridge.** The security review found nothing. The test reviews found two defects,
  both fixed. A task already cancelled still sent its POST, which started a job no one
  could cancel. A timeout and an interrupt landing together could send two DELETEs for
  one render.
- **Known gaps.** A cancel that lands while the bridge's POST is in flight cannot cancel
  that job, a window of milliseconds on the local socket. One render folder that cannot
  be deleted holds up Chatter's render sweep (downloads stay refused). Audio dated in the
  future (the clock set back) is kept until the clock catches up.

# Recommendations

- Measure the turnaround on the 1.75C with the method of memo-030, and record whether
  Sonnet 5.5 at low effort keeps project work good enough. If not, the model can differ
  between general and project sessions (`SessionPolicy.model`).
- RFC-003 phase 2 (streamed 0.8 s chunks) would start a reply before its first sentence
  finishes rendering. With Responsive renders taking about a quarter of the reply's
  length, it saves well under a second on the first sentence and nothing on later ones.

# References

- [memo-030: Voice reply turnaround](memo-030-voice-reply-turnaround.md)
- [RFC-003: Voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- Chatter: `~/dev/chatter`, `docs/API.md` (render mode, waiting for the result)
