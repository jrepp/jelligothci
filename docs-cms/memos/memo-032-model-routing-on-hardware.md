---
title: Claude or Ollama by voice, and the model picker, on the 1.75C
author: Claude
created: 2026-10-10T21:43:22Z
tags: [agent, bridge, esp32, memo, ollama, protocol]
id: memo-032
project_id: jelligotchi
doc_uuid: 19142b1c-313e-4baa-aecc-298c2e348752
---

# Overview

This memo records how routing voice requests to Claude or a local Ollama model
([ADR-018](../adr/adr-018-route-voice-requests-to-claude-or-a-local-ollama-model.md))
was built and checked, and a fix for replies whose sentences clipped each other.

# Context

Michael asked that the bridge route requests to Claude, the default, or to an Ollama
model on the Mac chosen in the device's Settings; that "ask Claude to …" and "ask
Ollama to …" pick one; and that every Ollama call get Brave Search Bridge as a tool.
While testing he reported that replies on the device occasionally clipped.

# Details

## Built

- loki/1 gained `models` (bridge to board) and `set_model` (board to bridge), with 37
  golden vectors. The C codec shares one string-list field kind between `ask.options`
  and `models`; the Swift codec gained matching kinds.
- The bridge lists Ollama's local models that use tools, sends the list on every
  connect and after every pick, keeps the choice in `models.json` (0600, in its private
  folder), and routes requests as ADR-018 says. Ollama answers with Brave Search
  Bridge's `brave_` tools, at most 4 searches a round and 12 a reply.
- Settings on the board has a Model card and a scrolling list. A tap sends
  `set_model`; the bridge's next `models` confirms or corrects it.

## Clipping

A reply that begins makes the board current-switch at once and drop what it still
holds of the last one. With sentences sent one after another (memo-030) and Chatter's
faster render mode, the next sentence often began while the board held up to a second
of the last. The bridge now waits for the board's `played` (or the reply's length and
two seconds) before it begins the next reply: a pause of about 0.4 s between
sentences instead of a clipped end.

## Verified, 2026-10-10

| Check | How | Result |
| --- | --- | --- |
| Install | `JELLI_ESP_BOARD=1.75c make esp-agent-install` | Booted, rejoined the bridge |
| List | `loki models` on the console | Claude and 15 Ollama models, Claude current |
| Settings | `ui button`, `ui tap`, desk-camera pictures | Model card reads "Claude"; the list outlines the current model and scrolls |
| Pick | Tap qwen3.8:27b-mlx, then Claude | Bridge logged each pick, saved `models.json` (0600), and confirmed it; `loki models` current followed |
| Ask the local model | Spoken weather question | Searched Brave and answered; first audio 14 s after release with gemma4:12b-mlx |
| Ask Ollama, qwen3.8:27b-mlx | "insights on how AI harnesses work" | First audio about 30 s after release; a five-sentence answer |
| Ask Claude | "where they are with my Elysium tasks" | Went to Claude's general session |

The Mac's copy of `gpt-oss:20b` does not load ("key not found in model:
gpt-oss.expert_feed_forward_length"); a request to it now says so in words. Michael
chose `qwen3.8:27b-mlx` as the Ollama model, with Claude the default.

## Hearing "Ollama"

The recognizer mishears "Ollama": macOS voices reading the spelling came out as "Olim
and", "Ollimar", "Colin" and "Alame". The bridge accepts words within two edits of
"ollama" or one of "llama", two words that join into one ("oh llama"), and "local" or
"the local model", which were heard every time. Words like "mama", "drama" or "Lima"
are not taken for it.

## Review

Security reviews of the bridge and the board passed. Their one medium finding, that a
page in search results could make Ollama search without limit and spend the Brave
quota, was confirmed in the code by two skeptic reviews and rated low; the caps were
added anyway. Also fixed: only Brave's tools are passed to Ollama, a board can make
the bridge remember only the last 64 replies' `played`, a stopped Ollama job shows no
further caption, a default Ollama model is warmed at most once a minute, every pick is
answered with the list, and the saved choice keeps its 0600 mode and leaves no
temporary file. The test reviews added 84 bridge tests and the board's model tests;
the bridge suite is 489 tests (one known issue, from memo-030), the sidecar 31.

# Recommendations

Not done, for later or for Michael:

- Try the Settings list by finger; it was driven over the console.
- On the board, a list can briefly reappear after the link drops (cosmetic), and a pick
  waits on a stalled bridge for up to about 2 s, as holding to talk already does.
- "Ask Ollama … in chatter" goes to Ollama; whether naming a project should win is
  Michael's call.
- Brave Search Bridge listens on every interface without authentication, so others on
  the network can spend its quota.

# References

- [ADR-018: Route voice requests to Claude or a local Ollama model](../adr/adr-018-route-voice-requests-to-claude-or-a-local-ollama-model.md)
- [memo-030: Voice reply turnaround](memo-030-voice-reply-turnaround.md)
- [loki/1 protocol](../../docs/loki-protocol.md)
