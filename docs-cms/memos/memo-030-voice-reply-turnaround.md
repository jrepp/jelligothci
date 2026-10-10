---
title: Voice reply turnaround on the 1.75C
author: Claude
created: 2026-10-10T17:35:26Z
tags: [agent, audio, bridge, latency, memo]
id: memo-030
project_id: jelligotchi
doc_uuid: 81960abf-b35c-456a-8370-ee13b9bb4b5d
---

# Overview

This memo records where the time goes between releasing the screen and hearing Loki's
reply, what Loki Bridge now does to shorten it, and what is left. The cuts are in the
bridge only. The Claude model, Chatter's voice and Chatter's quality are unchanged.

# Context

Michael asked for a faster turnaround on voice replies. A voice request runs:
release (`ptt_end`) → final transcript → Claude Code in the general session → Claude
calls `mcp__device__speak` → Chatter renders the reply in the Scar voice at Studio
quality (`mode: "save"`) → the bridge streams it to the board.

# Details

## Method

`ui hold start` on the board's console, the Mac speaks a request with `say` beside
the board, `ui hold stop`, and the bridge's log, timestamped per line, is read in
seconds after the release. Same board (1.75C), Wi-Fi, account default model and
Chatter settings throughout. First audio is about 0.1 s after a piece is rendered:
the bridge sends the first second at once, so the board's 300 ms prebuffer fills
almost immediately.

## Where the time went

| Step | Before |
| --- | --- |
| Release → Claude's words | 1.8–2.7 s with a warm session; 4.1 s when the session started cold |
| Claude's words → run ends | 1.0–1.3 s: Claude's last turn, after `speak` |
| Render starts | when the run ended |
| Studio render (Scar) | about 0.9–1.3× the reply's length, plus about 0.3 s |
| Cold session | about 2.2 s more, on the first request and after 10 minutes idle |

A long reply was one render, so a nine-second reply waited about nine seconds for its
first word.

## Changes

1. **Speak when Claude speaks.** The bridge queues Claude's `speak` text as it
   arrives instead of at the run's `result`, so Claude's last turn and the render
   overlap. What Claude already said is not said again; a failure is always said.
2. **No tool search.** Claude Code defers MCP tool schemas behind a search, which
   cost an extra turn before the first `speak`. The session environment sets
   `ENABLE_TOOL_SEARCH=false`; the only MCP server is the device's two tools. Every
   measured reply since has taken 2 turns; before, some took 3.
3. **A warm general session.** The bridge opens the general session when the board
   connects and when the user starts holding the screen, replacing one idle for
   more than 10 minutes, so its start-up overlaps the user's speech.
4. **A sentence at a time.** `SpokenChunks` splits the reply into pieces of about a
   sentence (sentences under 30 characters join the next; replies under 60
   characters stay whole). The bridge renders the next piece while the board plays
   the current one.
5. **Talking over a reply stops it.** Holding the screen again, or cancelling, ends
   the piece being streamed where it is, and the bridge stops waiting for the render
   under way. Chatter still finishes that render: its cancel would also stop
   whatever Chatter is playing on the Mac.

## Results

Seconds after release.

| Run | Claude's words | Rendered | First audio |
| --- | --- | --- | --- |
| Before, warm session, one sentence | 1.84 | 4.37 | ≈4.5 |
| Changes 1–2, cold session | 4.12 | 5.73 | ≈5.8 |
| Changes 1–2, warm session | 2.66 | — | — |
| Changes 1–3, first request after a restart | 1.91 | 3.64 | ≈3.7 |
| Changes 1–4, three sentences | 2.32 | 5.12 (first) | ≈5.2 |
| Changes 1–4, after 10 minutes idle | 2.93 | 4.86 | ≈5.0 |
| Final build, first request after a restart (two runs) | 2.11, 4.24 | 4.37, 6.39 | ≈4.5, ≈6.5 |
| Final build, warm session (two runs) | 2.48, 4.18 | 4.72, 6.41 | ≈4.8, ≈6.5 |

Model time varies more than anything the bridge changed: Claude's words came 1.8 to
4.2 s after the release for the same kind of one-sentence question. What the bridge
saves is fixed. The render now starts at Claude's words, 0.6 to 1.3 s before the run
ends, where before it started at the end. The first request after a restart, or after
10 minutes idle, no longer waits about 2.2 s for the session to start.

The first request after a restart cost $0.076 with the warm session against $0.332
cold. After 10 minutes idle the session was replaced when the hold began, and its
start-up overlapped the 2.2 s of speech.

Holding the screen during the first sentence of a three-sentence reply stopped it
there (`Reply 1 (2.56 s) stopped`), and nothing more of it was said; the new question
("What is two plus two?") was rendered 3.58 s after its release. A local reply after
such an interruption waits for Chatter to finish the abandoned sentence: "Status",
said while the second sentence rendered, was rendered 5.3 s after its release.

Three sentences of 2.8, 4.5 and 6.0 s: first audio at ≈5.2 s, done at ≈19.3 s, with
gaps of ≈1.2 s and ≈0.7 s where a sentence was longer than the one before it. Before,
the same reply would have started only after the whole 13 s rendered.

## Review

The security review passed the change. Its one medium finding: Claude's words were
queued without their job, so words already waiting were still said after the user
stopped that job, and a job calling `speak` many times could fill the queue. Two
skeptic reviews confirmed it in the code and rated it low, a voice problem rather than
a security boundary. It was fixed anyway:

- Queued words carry their job, and a job's newer words replace its words still
  waiting.
- Stopping a job, the board's cancel, a session that closes or errors, and Claude Code
  exiting drop that job's waiting words.
- From the release until what the user said is routed nothing starts, so a spoken
  "stop" comes first.

Lower findings, also fixed:

- Talking over a reply ends the stream and the wait for its render.
- No queued line starts while a press or a cancel is still interrupting the speaker.
- A recognizer that fails to start, or a board that reconnects, no longer leaves lines
  waiting.
- Claude's text in the bridge's log is cleaned of control, format and separator
  characters (`LokiText.loggable`).

Noted, not fixed:

- Claude's words can be heard while an approval card is on the board; the card still
  shows the whole action.
- The general session opened ahead reads what lies beside the projects up to 10
  minutes before its first request.
- The hold from release to routing is one flag: a second utterance released before the
  first is routed could let a line start early. A person's hold cannot do it.
- `transcriber.finish()` has no deadline, as before; a hang there now also holds the
  spoken queue.
- A combining mark right after a space is dropped from the spoken text.

The test reviews added tests for every interleaving of speak, result, close and stop
across two sessions, the session opened ahead, the queue against a model (48 seeds),
interrupts that suspend, and `SpokenChunks` on hostile and 100 KB input; their mutants
were all caught. Suites: `swift test` 341 passed (one known issue, the combining mark),
sidecar 30 of 30.

# Recommendations

What is left is model time (1.8–4.2 s to Claude's words) and Studio rendering (about
real time for Scar). Each remaining lever is Michael's decision:

- **Chatter quality or streaming.** A `responsive` render, or the streamed results
  planned in [RFC-003](../rfcs/rfc-003-voice-agent-companion.md#changes-to-chatter)
  phase 2, would take most of the render out of the wait and close the gaps between
  sentences. Chatter today offers `play` or `save`, and `save` is always Studio.
- **The Studio model after idle.** Chatter unloads its Studio model after 600 s idle
  unless "Keep the studio model loaded" is on. After 10 minutes idle, "Two plus two is
  four." rendered in 1.92 s against 1.6–1.7 s warm, so keeping the 1.7B model resident
  is not worth its memory for this.
- **Model or effort for voice sessions.** A faster model or lower effort would shorten
  the 1.8–4.2 s before Claude speaks. The bridge leaves the model to the account
  default.
- **Cancelling an abandoned render.** Chatter's cancel also stops whatever it is
  playing on the Mac, so the bridge does not use it. If cancelling a `save` job left
  playback alone (a Chatter change), the bridge could cancel the sentence it no longer
  needs, and a reply after an interruption would not wait for it.
- **Streaming `speak`'s input.** With partial messages the bridge could start rendering
  the first sentence while Claude still writes the rest. It helps only multi-sentence
  replies; not built.

# References

- [RFC-003: Voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- [memo-029: Hold to talk and petting](memo-029-hold-to-talk-and-petting-on-hardware.md)
