---
id: rfc-003
title: Voice agent companion — an alternate board app and a Mac bridge to Claude Code
status: Draft
author: Michael Weingartner
created: 2026-10-09T13:23:33Z
tags: [agent, audio, bridge, claude-code, esp32, network, rfc, security, voice]
project_id: jelligotchi
doc_uuid: bfbec3fd-3d7e-420f-9d50-6b0092d7abc7
---

# Summary

Build a second app for the Waveshare ESP32-S3-Touch-AMOLED-1.75C that turns the
board into a voice-first interaction point for Claude Code running on the desk
Mac. The user holds the unicorn to talk; the board streams 16 kHz audio over an
authenticated Wi-Fi link to a native macOS menu-bar **bridge**. The bridge
transcribes on-device, runs the request as a Claude Code task through the Claude
Agent SDK in an allow-listed project, streams progress back so a large animated
unicorn shows listening, thinking, working, needs-approval, success and failure,
routes permission prompts to the board for a physical tap, and answers with a
short spoken reply rendered by Chatter in the user's chosen voice. A durable work
queue in the bridge is the backbone, so other executors (for example the user's
own interactive Claude Code session) can be added later.

The pet game stays on the board: the agent installs as a second app and the two
switch from their settings.

# Motivation

- Kick off and supervise Claude Code work hands-light from anywhere at the desk,
  without switching windows: "run the tests in codergotchi", "what did the last
  build say", "go ahead".
- Glanceable status: an expressive character is easier to read across a room
  than a terminal, and the round AMOLED is well suited to it.
- Reuse what exists: the board's microphones, speaker, Wi-Fi and the new
  high-resolution unicorn (ADR-012); Chatter's voices; on-device speech
  recognition already used in Chatter's `LocalTranscriber`.

# Research findings that shape the design

## The board

- **Proven hardware path.** The board's factory flash (our backup
  `build/device-backup/amoled-1.75c-7c0c5f423354-full-32MB.bin`) held Waveshare's
  launcher in `factory` and **xiaozhi v2.1.0**, an open-source voice assistant
  (ESP-SR, Opus, WebSocket, LVGL 9.3, IDF 5.5.2, ≈2.87 MB image), in `ota_0`. The
  two-app layout and the voice stack both run on this exact board.
- **Microphones.** ES7210 at I2C 0x40 on the shared I2S bus (MCLK 16, BCLK 9,
  WS 45, DIN 10). The BSP opens it mono (MIC1) at the pet's 22,050 Hz. The
  schematic loops the ES8311 speaker output into ES7210 **MIC3 through an RC
  network labelled AEC**, but only SDOUT1 reaches the ESP32, so the echo
  reference is available **only in TDM mode**, which the BSP does not do; xiaozhi's
  `BoxAudioCodec` (MIT) shows the TDM setup. Capture and playback share one clock,
  so they must run at the same rate: fix 16 kHz in the agent.
- **Microphone power** comes from AXP2101 ALDO1. Pet sounds play on the 1.75C, so
  it is on at power-up; the agent reads register 0x90 at boot and never writes the
  PMU unless explicitly authorized.
- **Buttons.** The PWR key is readable on GPIO3 (through a BSS138), no PMU access
  needed; holding it powers the board off after 4–10 s, so it is for "stop" clicks
  only. BOOT is GPIO0, a strapping pin. **Touch-and-hold on the unicorn** is the
  safest, most discoverable push-to-talk.
- **Display.** The CO5300 runs over 40 MHz QSPI (≈20 MB/s): a full 466×466 frame
  needs ≥21.7 ms on the wire; the pet's canvas path measured ≈7 Hz for full frames
  and 61 Hz for small dirty regions (memo-006). A large character therefore needs
  layered parts and small dirty regions, not full-frame flipbooks. The panel's tear
  signal is wired to GPIO13 but unused by the BSP.
- **Wi-Fi and TLS fit.** Wi-Fi leaves ≈184–274 KB internal heap depending on
  buffer settings; mbedTLS needs 22–42 KB and can live in PSRAM; ESP-SR's front end
  (AEC + VAD + wake word) costs ≈60 KB internal + 0.7–1.2 MB PSRAM and 10–25 % of a
  core. xiaozhi's settings show what it takes: 240 MHz, 64-byte cache lines, mbedTLS
  in PSRAM, lean Wi-Fi buffers — so the agent needs its own sdkconfig.
- **Bandwidth is trivial.** 16 kHz 16-bit mono PCM is 256 kbit/s, under 1 % of
  the S3's Wi-Fi throughput. Send 60–100 ms frames.
- **Flash trap.** With a `factory` partition present, `idf.py flash` always writes
  `factory` and resets `otadata`; the agent must be flashed to `ota_0` by name or
  it overwrites the pet.

## Claude Code

- **Agent SDK, streaming input** is the robust way to drive Claude Code from
  another program (current: `@anthropic-ai/claude-agent-sdk` 0.3.x, bundling CLI
  2.1.29x; Python `claude-agent-sdk` 0.2.x). One long-lived session per
  conversation, `cwd` per session, `streamInput` for follow-ups with `priority`,
  `interrupt()` for "stop", `resume` after restarts, `canUseTool` for permission
  prompts (also receives `AskUserQuestion`), in-process MCP tools via
  `createSdkMcpServer`, hooks as callbacks, `maxTurns`/`maxBudgetUsd` limits,
  `includePartialMessages` and `CLAUDE_CODE_EMIT_SESSION_STATE_EVENTS=1` for
  progress (`running | idle | requires_action`).
- The Claude desktop app drives the CLI the same way (stream-json in/out,
  `--permission-prompt-tool stdio`), but that control protocol is SDK-internal, so
  a bridge should use the SDK rather than re-implement it.
- **Settings load by default.** Without `settingSources`, a bridge session loads
  the user's global CLAUDE.md, hooks and 11 plugins (including the requirement for
  Opus review agents) — slow and expensive for voice tasks. Choose sources
  deliberately.
- **Subscription login works** for a personal tool on the user's own Mac (normal
  credential precedence); distribution would need API keys. Usage is shared with
  the user's other Claude Code sessions.
- **Pushing work into an already-open interactive session** exists only as
  research-preview "channels" (needs a development flag and a confirmation at each
  launch) or **mods** (plugin JavaScript with `$.prompt.submit`, `turn.complete`,
  `tool.check`, on by default since 2.1.286/287). Polling a queue from an idle
  interactive session is otherwise not possible without a model turn per poll.
- The `claude` on the user's PATH is v2.1.83 (March); the SDK bundles its own CLI.

## Chatter

- A Swift 6.2 menu-bar app (Qwen3-TTS on MLX/Metal). Programmatic access is a
  loopback REST API `127.0.0.1:18423` (bearer token; scoped client tokens via
  Chatter → Connections) and an MCP server with the same tools. `POST /v1/speech`
  `{voice, text, mode, quality, tone, instruction, pace, language, requestID}`
  queues a job; poll `GET /v1/jobs/{id}`; fetch `GET /v1/jobs/{id}/audio`.
- Output is always 24 kHz mono 24-bit WAV. Warm built-in voices (Ryan, Aiden)
  render at ≈0.3–0.4× real time; `save` forces Studio quality, so cloned voices are
  slower; built-in/designed voices take `tone` and `instruction`, clones do not.
- Gaps for a voice bridge: no silent fast render mode (`play` speaks on the Mac,
  `save` forces Studio), no synchronous or streaming result, one FIFO queue with
  no priority lane, built-in voices unload after 10 idle minutes, no output-format
  options. Chatter must be running.

## Speech recognition

- macOS 26+ `SpeechAnalyzer`/`SpeechTranscriber` runs on-device and is already
  wrapped by Chatter's `ChatterAudioKit/LocalTranscriber.swift`; it supports
  volatile (live) results for captions. `whisper.cpp` (`whisper-cli`) is installed
  as a fallback.

# Recommendation

Build three pieces, in this order of importance:

1. **Jelli Bridge** — a native Swift menu-bar app on the Mac (Atrium design),
   which owns the device link, speech recognition, the work queue, Chatter, the
   UI, and supervises a small **TypeScript Agent SDK sidecar** that owns Claude
   Code sessions.
2. **Unicorn Agent** — a second ESP-IDF app on the board (`ota_0`), sharing the
   bootloader, `nvs` and BSP with the pet, with its own sdkconfig, partitions and
   art, and a portable C agent UI core that also runs in the SDL host as a desktop
   simulator.
3. **A versioned device protocol** (`jelli-agent/1`) with golden vectors shared
   by the C, Swift and TypeScript tests.

Use the bridge's durable queue as the integration point, with bridge-managed SDK
sessions as the default executor and "attach to my open session" (a mod) as a
later, optional executor.

## Decisions (2026-10-09)

The user settled four questions after reviewing this draft:

| Question | Decision |
| --- | --- |
| Executor | Bridge-run Agent SDK sessions; attaching to the user's open session stays a phase 3 option |
| Autonomy | Reads, file edits inside the project, and each project's listed commands (for example tests and builds) run without a tap; everything else waits for a tap on the board |
| Chatter | Extend Chatter's API for faster replies (see Changes to Chatter) |
| Board | Same board, switchable: pet in `factory`, agent in `ota_0` (layout A) |

Because edits and listed commands both run without a tap, Claude can change code
and then run it, for example edit a test and run the suite, with nobody looking.
The autonomy policy (under Security) therefore pairs it with Claude Code's
OS-enforced Bash sandbox.

# Detailed Design

## System overview

```text
 ┌──────────── Unicorn Agent (ESP32-S3, ota_0) ────────────┐
 │ touch-hold → mic (ES7210 TDM, 16 kHz) → PCM frames       │
 │ unicorn state machine ← status/approval/reply events     │
 │ speaker (ES8311, 16 kHz) ← reply PCM, mouth from RMS     │
 └──────────────── mTLS WebSocket over Wi-Fi ───────────────┘
                              │  (mDNS discovery, USB pairing)
 ┌──────────────────────── Jelli Bridge (macOS) ───────────────────────────┐
 │ DeviceLink (Network.framework TLS, pinned certs, Bonjour)               │
 │ SpeechIn (SpeechAnalyzer, live captions; whisper.cpp fallback)          │
 │ Router (workspace + conversation selection, local commands)             │
 │ WorkQueue (SQLite: utterances, jobs, events, approvals, replies)        │
 │ StatusMapper (events → unicorn states, captions, progress)              │
 │ SpeechOut (Chatter REST → 24 kHz WAV → 16 kHz PCM → device)             │
 │ Menu-bar UI (devices, jobs, history, voice, workspaces, policy, logs)   │
 │        │ JSON lines over stdio                                          │
 │ AgentRunner sidecar (Node, @anthropic-ai/claude-agent-sdk)              │
 │   sessions per conversation (cwd allow-list), canUseTool → approvals,   │
 │   in-process MCP: speak, progress; AskUserQuestion → device choices     │
 └─────────────────────────────────────────────────────────────────────────┘
        │ Claude Code (bundled CLI)        │ Chatter.app (127.0.0.1:18423)
```

## One request, end to end

1. **Talk.** The user touches and holds the unicorn. The board enters
   *listening* (ears up, a level-reactive ring on the bezel), sends `ptt_start`,
   and streams 60–100 ms PCM frames. Releasing sends `ptt_end`. The PWR key
   clicked at any time sends `cancel`.
2. **Hear.** The bridge feeds frames straight into `SpeechAnalyzer` while the
   user speaks and returns live captions; the final transcript is ready ≈0.2–0.5 s
   after release (estimate). Contextual strings (workspace names, "Nirmal",
   "codergotchi") improve recognition. The board shows the transcript and nods.
3. **Route.** The router handles local commands without Claude ("stop", "what's
   running", "use the Aiden voice", "repeat that") and otherwise picks a workspace
   (explicit "in codergotchi…" against the allow-list, else the current
   conversation's) and a conversation (follow-up vs. new task). It records an
   utterance and a job in the queue.
4. **Run.** The sidecar sends the text into the conversation's Agent SDK session
   (`streamInput`, `priority: "next"` for follow-ups while busy). Events flow back
   as job events.
5. **Show.** The status mapper turns events into unicorn states (below), a
   one-line caption ("Running tests…", from the tool name or Bash `description`)
   and, when known, a progress arc around the round bezel. Updates are rate-limited
   to the board (≤ 5/s).
6. **Approve.** When Claude needs permission, `canUseTool` raises an approval.
   The board shows the exact action ("Run `npm test` in codergotchi?") with two
   large targets; only a **physical tap** answers, bound to the request ID, a hash
   of the shown summary and a nonce. No answer within the policy timeout denies.
   `AskUserQuestion` becomes a choice card (2–4 options).
7. **Answer.** Claude ends a task by calling the in-process `speak` tool with one
   or two plain sentences and a mood; if it does not, the bridge speaks a trimmed
   summary of the result. The bridge asks Chatter for audio in the selected voice
   (tone from mood for built-in/designed voices), converts 24 kHz/24-bit to
   16 kHz/16-bit mono, and streams it; the board pre-buffers 300 ms, plays it, and
   drives the mouth from the playback level. Then it celebrates or looks sad.
8. **Details stay on the Mac.** The bridge window keeps the transcript, tool log,
   diff summary and cost, and offers "Open in Claude Code" (`claude --resume <id>`
   or the desktop app) for anything that needs a screen.

## Unicorn states and animation

| State | Trigger | Look |
| --- | --- | --- |
| idle | nothing running | breathing, blinks, glances around; dims and drifts to avoid burn-in; screen sleep after 3 min |
| listening | `ptt_start` | ears up, leans in, bezel ring follows mic level |
| heard | final transcript | nod, transcript caption |
| thinking | session running, text/thinking | eyes up, sparkles orbit the horn |
| working | tool running | focused, horn glows; small badge for terminal / files / web / git; progress arc |
| needs you | approval or question pending | looks at the user, raised hoof, approval or choice card |
| speaking | reply playing | mouth shapes from playback RMS |
| success | `result` success | hop, confetti, rainbow |
| partial | success with warnings / denied steps | shrug |
| failure | error result, interrupt, timeout | sad, small rain cloud |
| offline | no bridge / Wi-Fi | sleepy with a Wi-Fi glyph |
| queue badge | jobs waiting | small count near the bezel |

**Art pipeline.** Reuse the ADR-012 density/alpha format at 384×384 (density 2 of
a 192 logical frame) for 6–8 key poses generated as one consistent sheet (as for
the current pets). Derive eye and mouth overlays by editing the same composition
(eyes closed, 5–6 mouth shapes) and storing only the changed rectangle as a patch.
Motion comes from procedure, not frames: breathing and bob as small offsets of
layers, sparkles/confetti/rain as particles, the bezel ring and progress arc as
vector drawing. A body pose redraws only on a state change; per-frame work is
limited to eyes, mouth and particles. Store art opaque-on-black where layers do not
overlap (the AMOLED background is black) to skip blending, and in a dedicated `art`
partition mapped at run time.

**Rendering.** Write the agent UI as a portable C core (`core/agent/`) on the
existing canvas, sampler and asset code, with host-injected time, input and output
like the pet. On the board, replace the pet's three-copy present path (engine
framebuffer → LVGL canvas → draw buffer) with direct dirty-rectangle presents
through internal-RAM DMA stripes, and adopt the TE signal on GPIO13 if tearing
shows. Run at 240 MHz. Target 30+ fps for overlay animation; measure in phase 0.

## Device app

- **Project** `ports/esp32-agent/`: same IDF 5.5.5 and LVGL 9.3.0 pins as the
  pet; own sdkconfig (240 MHz, 64-byte data-cache lines, mbedTLS in PSRAM, lean
  Wi-Fi buffers, `CONFIG_ESP_WIFI_NVS_ENABLED=n`, PHY calibration not stored in the
  shared `nvs`); reuses the board wrapper and BSP for display, touch and I2C but
  opens audio itself.
- **Tasks**: core 0 — Wi-Fi/lwIP, the link (WebSocket, mDNS, reconnect with
  backoff), audio in and out; core 1 — UI and animation. Queues and PSRAM ring
  buffers between them.
- **Audio**: fixed 16 kHz 16-bit. ES7210 in TDM capturing MIC1 and the MIC3 echo
  reference from day one (reference unused in v1); half-duplex (playback stops when
  the user holds to talk); reply ring buffer 128 KB with 300 ms pre-buffer; reuse the
  pet's calibrated volume curve (memo-021) and add a sound-level setting.
- **Power**: `WIFI_PS_NONE` while listening, thinking or speaking, minimum modem
  sleep when idle; screen sleep and double-tap wake as in the pet; read-only
  battery/PMU logic from `power.c`.
- **Settings**: Wi-Fi status, paired bridge, volume, brightness, "Back to pet".

## Two apps on one board

Partition layout A (16 MB, both boards; validated with `gen_esp32part.py`):

| Name | Type | Offset | Size | Use |
| --- | --- | --- | --- | --- |
| nvs | data/nvs | 0x9000 | 24 KB | unchanged — keeps the pet save |
| phy_init | data/phy | 0xF000 | 4 KB | unchanged |
| factory | app | 0x10000 | 7 MB | pet (5.45 MB today, 22 % free) |
| ota_0 | app | 0x710000 | 4 MB | agent (xiaozhi-class ≈2.9 MB) |
| otadata | data/ota | 0xB10000 | 8 KB | boot choice |
| agent_nvs | data/nvs | 0xB12000 | 56 KB | Wi-Fi credentials, pairing keys |
| model | data | 0xB20000 | 1 MB | reserved for ESP-SR models |
| art | data | 0xC20000 | 3.9 MB | agent unicorn art |

The pet gains "Switch to agent" (`esp_ota_set_boot_partition(ota_0)` + restart);
the agent gains "Back to pet". Rollback is enabled so a crashing agent falls back
to the pet. The agent never calls `nvs_flash_erase()`. A dedicated flash target
writes `ota_0` and `art` by name. A later 32 MB layout B (1.75C only) adds `ota_1`
for desktop updates. Layout A shrinks the pet's slot from ADR-012's 8 MB to 7 MB,
so adopting it amends ADR-012; sizes here are binary megabytes.

## Bridge app

- **Swift menu-bar app** (macOS 26+, Atrium), running as a per-user LaunchAgent;
  needs Local Network permission.
- **DeviceLink**: Network.framework TLS listener with a self-signed P-256 identity,
  client certificates required and pinned per paired device; Bonjour service
  `_jelli-agent._tcp`; WebSocket framing; per-device rate limits.
- **SpeechIn**: `SpeechAnalyzer`/`SpeechTranscriber` with live results and
  contextual strings; `whisper-cli` fallback; audio kept only for the current job
  unless the user enables history.
- **WorkQueue**: SQLite tables `devices`, `workspaces`, `conversations`
  (workspace, SDK session ID), `utterances`, `jobs` (state machine: queued →
  running → needs-approval → succeeded/failed/cancelled), `events`, `approvals`,
  `replies`. Survives restarts; sessions resume by ID.
- **AgentRunner sidecar** (Node, Agent SDK, JSON lines over stdio, restarted by
  the bridge): one streaming session per conversation with `cwd` from the
  allow-list; `systemPrompt: {type: "preset", preset: "claude_code", append: <voice
  operator rules>}`; `settingSources: ["project", "local"]` by default (the user's
  global CLAUDE.md, hooks and plugins opt-in per workspace); `permissionMode:
  "acceptEdits"` always set explicitly (omitting it can start a session in auto
  mode; never `bypassPermissions`); the workspace's listed commands as
  `allowedTools` rules; the autonomy policy's sandbox settings through the SDK's
  `settings` option; `maxTurns` and
  `maxBudgetUsd` per job; `CLAUDE_CODE_EMIT_SESSION_STATE_EVENTS=1`,
  `includePartialMessages: true`; in-process MCP server `device` with `speak(text,
  mood)` and `progress(message, fraction?)`, auto-approved and always loaded.
- **SpeechOut**: Chatter REST with a scoped, speak-only client token restricted to
  the selected voice; voice list from `GET /v1/voices`; mood → Chatter tone for
  built-in and designed voices; WAV conversion with AVAudioConverter.
- **UI**: devices and pairing, current job and queue, history with transcripts and
  replies, voice picker with preview, workspace allow-list and per-workspace
  policy (listed commands, allowed hosts, and the effective allow rules including
  any from the project's own settings), logs, and "Open in Claude Code".

## Device protocol `jelli-agent/1`

WebSocket over mutually authenticated TLS. Text frames carry JSON control
messages `{v, type, id, seq, ...}`; binary frames carry audio with a 12-byte header
(stream ID, sequence, flags). Every message has a per-direction sequence number;
approvals carry the request ID, summary hash and nonce.

| Direction | Messages |
| --- | --- |
| device → bridge | `hello` (firmware, board, capabilities), `ptt_start`, audio, `ptt_end`, `cancel`, `approval` (allow/deny, id, nonce), `choice`, `ack`, `ping` |
| bridge → device | `welcome`, `caption` (live/final transcript), `state` (unicorn state, caption, progress, queue size), `approval_request`, `choice_request`, `reply_begin`/audio/`reply_end`, `settings`, `pong` |

## Pairing and provisioning

v1 pairs over USB: the bridge finds the board on USB-Serial/JTAG (303A:1001) and,
through an ADR-010-style line protocol, sends Wi-Fi credentials (typed by the user
in the bridge) and the bridge certificate fingerprint, and receives the device
certificate generated on first boot. Both sides show the same short confirmation
code. Physical USB access already implies full flash access, so this adds no new
trust. Wireless re-pairing (SRP6a over an on-screen code) and BLE provisioning come
later.

## Security

This app turns speech near the board into code execution on the Mac, so security
is part of the core design, not a later phase.

- **Link**: mutual TLS with pinned certificates; nothing on the bridge's LAN port
  answers an unpaired peer; mDNS is a hint only; replay protection from TLS plus
  application sequence numbers; per-device revocation in the bridge.
- **Who can talk**: push-to-talk needs a physical touch; anyone at the desk could
  still speak, so the action policy, not the voice, is the gate.
- **What Claude may do**: workspaces are allow-listed; the autonomy policy below
  decides what runs without a tap; budgets and turn limits per job; no
  `bypassPermissions`; Claude Code's own permission rules remain the final gate.
- **Approvals**: physical tap only, never voice; bound to the exact summary shown;
  time out to deny; the bridge logs every decision.
- **Prompt injection** from repositories or web content is contained by the same
  permission gates and the sandbox; approval cards show the literal command, not
  Claude's paraphrase.
- **Data**: audio is discarded after transcription by default; history is local;
  the Chatter token never leaves the Mac; device keys sit unencrypted in
  `agent_nvs` (eFuse-based NVS/flash encryption burns fuses irreversibly and is
  deferred to an explicit decision); LAN only in v1.

## Autonomy policy

Decided 2026-10-09: reads, edits inside the project and each project's listed
commands run without a tap; everything else waits for one.

| Action | Without a tap | Mechanism |
| --- | --- | --- |
| Read and search inside the project | yes | Claude Code approves these by default |
| Edit files inside the project | yes | `acceptEdits`, which also covers `mkdir`, `touch`, `mv`, `cp`, `rm`, `sed` there |
| Listed commands, such as `make test` | yes | per-workspace `allowedTools` rules such as `Bash(make test)` |
| Other shell commands, `git push`, web fetches | no | falls through to `canUseTool`, then the board |
| Edits outside the project or to protected paths | no | `acceptEdits` never approves these |

Edits plus listed commands let Claude change code and run it unattended, so every
voice session also runs inside Claude Code's Bash sandbox (macOS Seatbelt), which
applies to each command and the processes it starts:

- Writes only inside the project and a per-user temp directory. Claude Code's
  protected paths (`.claude` settings, skills and hooks, `.mcp.json`, `.git/hooks`
  and `.git/config`, shell startup files) stay write-denied even there.
- No network except through Claude Code's proxy to an allow-list that starts
  empty and is set per workspace.
- Strict settings: `allowUnsandboxedCommands: false` (no unsandboxed retry),
  `failIfUnavailable: true`, and `autoAllowBashIfSandboxed: false` so the sandbox
  never widens what runs without a tap beyond the listed commands.
- Delivered through the SDK's flag-settings layer, which makes the sandbox
  admin-required: a repository's own settings cannot loosen it.
- Reads of credential stores (`~/.ssh`, `~/.aws` and similar) denied with
  `filesystem.denyRead`, since the sandbox allows most reads by default.

The bridge records the Git state before each job and shows the resulting diff
afterwards; commits and pushes still need a tap unless a workspace lists them.

## Latency budget (targets)

| Step | Target |
| --- | --- |
| touch → listening shown | < 100 ms |
| release → final transcript | 0.2–0.5 s |
| transcript → thinking shown | < 1 s (pre-warmed session) |
| task | seconds to minutes, with live status |
| `speak` → first reply audio | ≈2–2.5 s today (warm built-in voice); ≈0.5–1 s with Chatter render/streaming |

## Optional executor: attach to my open session

A Claude Code **mod** (plugin) in the user's own session can poll the bridge
queue, submit voice requests with `$.prompt.submit`, report `turn.*` and
`tool.call` events, and route `tool.check` approvals to the board. It is useful
when the user wants to watch work in their own window, but it shares one
conversation and working directory with their typing, so it is opt-in and comes
after the SDK executor is solid. Channels are a later alternative once they leave
research preview.

## Changes to Chatter

Approved 2026-10-09. The work happens in `~/dev/chatter`, and the bridge falls
back to today's API when an addition is missing.

- Phase 1: a silent `render` mode that respects `quality` and is ephemeral, and a
  synchronous result (`?wait=true`).
- Phase 2: streamed results (0.8 s chunks), an interactive priority lane, keeping
  the built-in profile warm, and `sampleRate`/`bitDepth` output options.

# Delivery plan

1. **Phase 0 — spikes and measurements.** Read-only hardware checks (PMU 0x90,
   0x22, 0x27; PWR on GPIO3; flash JEDEC ID; TE rate); TDM capture to a WAV on the
   Mac over plain WebSocket; Chatter WAV played on the board; render cost of a
   layered 384 px unicorn at 240 MHz; partition layout A with app switching.
2. **Phase 1 — MVP.** USB pairing, mTLS link, live captions, one workspace, SDK
   sidecar with the autonomy policy and sandbox, six core states, tap approvals,
   Chatter replies in a chosen voice (with the Chatter render mode and synchronous
   result), SDL simulator, protocol vectors and end-to-end tests with a simulated
   device.
3. **Phase 2 — daily driver.** Multiple workspaces and conversations, follow-ups
   and interrupt, choice cards, progress arc, full animation set, queue and history
   UI, Chatter streaming and priority lane.
4. **Phase 3 — hands-free and beyond.** Wake word (free WakeNet9 words such as
   "Hi ESP", "Computer" or "Jarvis", or a custom word through Espressif), barge-in
   with the hardware echo reference, the attach-to-session executor, desktop
   updates (layout B), more devices, remote access over a VPN such as Tailscale.

# Alternatives Considered

- **Single firmware with a pet/agent mode.** Rejected: ≈8.6 MB image over the
  8 MB slot, conflicting configurations (160 vs 240 MHz, caches, Wi-Fi), and network
  attack surface added to the pet.
- **Shared queue polled by an interactive session as the primary path.** Rejected
  for v1: nothing wakes an idle session without a model turn per poll; progress and
  approvals need a second mechanism anyway; one conversation and one working
  directory shared with the user's typing. Kept as an optional executor.
- **Swift driving the CLI's stream-json directly.** Feasible (the desktop app does
  it) but the permission and interrupt control protocol is SDK-internal. The SDK
  sidecar is the supported surface.
- **All-TypeScript bridge.** Simpler Claude integration but loses native
  `SpeechAnalyzer`, Network.framework TLS/Bonjour, Keychain and an Atrium UI.
- **Claude calling Chatter through its MCP tools.** Adds a model round trip and a
  permission surface to every reply and puts voice choice in the prompt. The bridge
  owns speech output instead; Claude only supplies text and mood.
- **Wake word in v1.** Deferred: free words are fixed, a custom "Hey Unicorn"
  needs Espressif training, and hold-to-talk avoids false triggers near a
  code-executing agent.
- **Opus audio.** Unnecessary on a LAN; raw PCM is lossless for recognition and
  costs no CPU.

# Open Questions

Executor, autonomy, Chatter changes and the board were settled on 2026-10-09 (see
Decisions). Still open:

1. Which projects belong on the voice allow-list, and which commands does each
   list?
2. Default reply voice: a built-in or designed Chatter voice (fast, follows mood)
   or a cloned voice (slower, and ignores mood until the render mode lands)?
3. LAN only for v1, or is away-from-home use needed soon?
4. Names for the board app and the bridge.

# References

- [ADR-010: Bounded USB serial debug interface](../adr/adr-010-bounded-usb-serial-debug-interface.md)
- [ADR-011: Selectable ESP32 board variants](../adr/adr-011-selectable-esp32-board-variants.md)
- [ADR-012: High-resolution asset density and flash partition](../adr/adr-012-high-resolution-asset-density-and-flash-partition.md)
- [memo-006: Input animation and frame performance](../memos/memo-006-input-animation-and-frame-performance.md)
- [memo-021: Settings sound level](../memos/memo-021-settings-sound-level.md)
- [memo-023: Screen sleep and double-tap wake](../memos/memo-023-screen-sleep-and-double-tap-wake.md)
- [Claude Agent SDK overview](https://code.claude.com/docs/en/agent-sdk/overview.md),
  [user input and approvals](https://code.claude.com/docs/en/agent-sdk/user-input.md),
  [permissions](https://code.claude.com/docs/en/agent-sdk/permissions.md),
  [Bash sandbox](https://code.claude.com/docs/en/sandboxing.md),
  [custom tools](https://code.claude.com/docs/en/agent-sdk/custom-tools.md),
  [sessions](https://code.claude.com/docs/en/agent-sdk/sessions.md),
  [mods](https://code.claude.com/docs/en/plugins/mods/overview.md),
  [channels](https://code.claude.com/docs/en/channels.md)
- [ESP-SR benchmarks](https://docs.espressif.com/projects/esp-sr/en/latest/esp32s3/benchmark/README.html),
  [esp_websocket_client](https://docs.espressif.com/projects/esp-protocols/esp_websocket_client/docs/latest/index.html),
  [Waveshare 1.75C schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75C/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf),
  [xiaozhi board support for the 1.75](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/waveshare/esp32-s3-touch-amoled-1.75)
- Chatter: `~/dev/chatter` (REST `Sources/Chatter/APIRouter.swift`, speech request
  `Sources/ChatterCore/SpeechRequest.swift`, transcriber
  `Sources/ChatterAudioKit/LocalTranscriber.swift`)
