---
title: Route voice requests to Claude or a local Ollama model
status: Proposed
created: 2026-10-10T20:48:21Z
deciders: Michael Weingartner
tags: [agent, bridge, esp32, ollama, protocol]
id: adr-018
project_id: jelligotchi
doc_uuid: 7677ca0b-431c-41c2-b82f-44edec957b24
---

# Context

Every voice request goes to Claude Code today
([ADR-014](adr-014-voice-agent-autonomy-any-project-any-command.md)). The Mac also
runs Ollama with about twenty models, and Brave Search Bridge, a background app that
serves the Brave Search MCP server at `http://127.0.0.1:18424/mcp`.

# Decision

## Decided by the user

On 2026-10-10 Michael asked that:

- The bridge route each request to Claude, the default, or to an Ollama model hosted
  on the Mac that the user selected.
- The device's Settings let the user select the default model.
- Saying "ask Claude to …" or "ask Ollama to …" send that request to Claude or to
  Ollama, whatever the default.
- Every Ollama call get Brave Search Bridge as a tool, so it can query the web.

## Proposed by Claude

**Models offered.** "Claude" first, then each Ollama model that runs on the Mac and
supports tools (`/api/show` lists `tools`), at most 15, most recently modified first.
Cloud models (with a `remote_host`) are left out, since they run elsewhere. A name
must also fit the protocol's model-name rule; one that does not is not offered.

**The default and the Ollama model.** The device's list sets the default. "Ask
Ollama" uses the last Ollama model picked there, `qwen3.8:27b-mlx` until one is
picked (Michael's choice, 2026-10-10; the copy of `gpt-oss:20b` on this Mac does not
load).
A default Ollama no longer lists stays on the board's list, and requests to it fail
aloud until another is picked. The bridge keeps both in `models.json` in its private
support folder, which voice sessions cannot read.

**Routing.**

| Request | Goes to |
| --- | --- |
| Status, stop, "say that again" | The bridge, as before |
| "Ask Claude …" | Claude, routed as before |
| "Ask Ollama …" (also "oh llama", "o llama") | The Ollama model |
| Names a project ("in chatter, …") | Claude: only Claude Code works in a project |
| A follow-up within 120 s | Whoever answered the last one |
| Anything else | The default |

**An Ollama request.** One `/api/chat` conversation per model, kept for follow-ups
and dropped after 10 idle minutes or 40 messages. The Brave tools the server lists
are passed on every call, and a reply may call tools for at most 6 rounds. Tool
results are cut to 8,000 characters. The answer is spoken like Claude's; it is a job
like Claude's, so status, stop and "say that again" work on it. Ollama has no other
tool: it reads no file and runs no command.

**The protocol.** Two additive loki/1 messages (`docs/loki-protocol.md`): `models`
(bridge to board: up to 16 names and the index in use), sent when the board
connects and after every change, and `set_model` (board to bridge: a name from the
last list). Peers that do not know them ignore them.

**The device.** Settings shows a Model card with the current default; it opens a
scrolling list like the Wi-Fi list, with the current model outlined. A tap sends
`set_model`, and the bridge's next `models` confirms it.

# Consequences

## Positive

- Questions can go to a local model, at no cost per request, and to Claude when the
  user wants Claude.
- Ollama models answer with current web results through the user's own Brave key.

## Negative

- Ollama replies are slower on large models, and the first request after a model
  unloads waits for Ollama to load it.
- What Ollama says comes from web pages it read; it reaches the user as speech, and
  its search queries as the board's caption. A reply makes at most 4 searches a round
  and 12 in all, and only Brave Search Bridge's `brave_` tools are passed on.
- Brave Search Bridge listens on every network interface with no authentication, so
  other machines on the network can use the user's Brave quota. That belongs to the
  app, not the bridge.

## Neutral

- Project work stays with Claude Code whatever the default.

# References

- [ADR-014: Voice agent autonomy](adr-014-voice-agent-autonomy-any-project-any-command.md)
- [ADR-016: On-device settings](adr-016-on-device-settings-wifi-and-screen-sleep.md)
- [RFC-003: Voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- [loki/1 protocol](../../docs/loki-protocol.md)
