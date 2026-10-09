---
title: Voice agent autonomy, any project and any command inside the sandbox
status: Accepted
created: 2026-10-09T19:40:09Z
deciders: Michael Weingartner
tags: [agent, bridge, claude-code, security]
id: adr-014
project_id: jelligotchi
doc_uuid: ed4d9834-abe2-4f1b-a283-a494500b193d
---

# Context

[RFC-003](../rfcs/rfc-003-voice-agent-companion.md) turns speech near the board into
Claude Code work on the Mac. Its first autonomy decision (2026-10-09, morning)
allowed reads, edits inside an allow-listed project, and each project's listed
commands without a tap, and sent everything else to the board.

Before phase 1 the user replaced it, in their words: "Allow the agent to work on
any project I ask for with any command I ask for." No project allow-list and no
command list remain, so the question is how to keep that safe when the words come
from a microphone and the commands from a model that reads untrusted repository and
web content.

Facts from the Claude Code documentation (checked 2026-10-09) that shape the answer:

- The Bash sandbox (macOS Seatbelt) applies to each shell command and the
  processes it starts. By default it allows writes only in the working directory
  and a per-user temp directory, keeps Claude Code's protected paths write-denied
  even there (`.claude` settings, skills, agents and hooks, `.mcp.json`, shell
  startup files, `.gitconfig`, `.git/hooks` and `.git/config`, most of
  `~/.claude`), and sends network traffic through a proxy that admits only allowed
  hosts. It allows reading most of the machine, including credential files, unless
  they are denied.
- With `autoAllowBashIfSandboxed`, a command that runs inside the sandbox needs no
  approval. A command that leaves it, as an unsandboxed retry
  (`dangerouslyDisableSandbox`) or through `excludedCommands`, goes through the
  normal permission flow, which in an SDK session ends at the bridge's
  `canUseTool` callback. An ask rule for `Bash(dangerouslyDisableSandbox:true)`
  forces that prompt even when an allow rule would match.
- The sandbox covers shell commands only. The Read, Edit, Write, WebFetch and
  WebSearch tools follow permission rules instead, and hooks, MCP servers and
  plugin code run outside it with the user's full access.
- An SDK session's `settingSources` decides what loads from disk. `"project"`
  loads the project's CLAUDE.md, but also its `.claude/settings.json`, including
  hooks, permission rules and sandbox settings. Omitting the option loads user,
  project and local settings. The SDK shows no trust dialog. Opening "any project"
  with project settings would let a cloned repository run a hook with the user's
  full access before Claude reads a word.
- An SDK session that does not set `permissionMode` can start in auto mode.

# Decision

## Decided by the user

The voice agent works on any project the user names and runs any command they ask
for, without a tap.

## How it is enforced

Claude proposed the enforcement below on the same day and stated it to the user;
changing it amends this ADR.

**Any project.** The bridge resolves the spoken name against the user's project
roots (`~/dev` to start) to one directory directly under a root, matched after
normalizing case, spaces and punctuation. A single match runs without a tap. An
ambiguous match becomes a choice on the board, and no match is answered aloud. The
resolved real path must stay inside a root, so a root itself, the home directory, and
symlinks that lead out are refused. A request that names no project continues the
current conversation's project; with none, the board asks.

**Any command, inside the sandbox.** Each session's Bash commands run sandboxed and
auto-approved, configured through the SDK's `settings` option (the command-line
settings layer):

| Setting | Value | Effect |
| --- | --- | --- |
| `sandbox.enabled`, `failIfUnavailable` | `true`, `true` | No sandbox means no session, never unsandboxed commands |
| `sandbox.autoAllowBashIfSandboxed` | `true` | Commands the user asks for run without a tap |
| `sandbox.allowUnsandboxedCommands` | `true` | A command that cannot work inside may ask to leave it |
| `permissions.ask` | `Bash(dangerouslyDisableSandbox:true)` | Leaving the sandbox always takes a tap |
| `sandbox.network.allowedDomains` | Developer hosts | Package registries and Git hosting (below) |
| `sandbox.filesystem.denyRead` and `permissions.deny` `Read(...)` | Credential stores | Neither commands nor the Read tool see them |
| `sandbox.credentials.envVars` | Token variables, `deny` | `GITHUB_TOKEN`, `GH_TOKEN`, `NPM_TOKEN` and similar are unset for commands |

The network allow-list starts with GitHub (`github.com`, `*.github.com`,
`*.githubusercontent.com`), npm and Yarn, PyPI (`pypi.org`,
`files.pythonhosted.org`), crates.io, the Go module proxy, and Espressif's
component and download hosts. The bridge's settings can extend it. `WebFetch`
allow rules mirror the list, and `WebSearch` is allowed.

Read is denied for `~/.ssh`, `~/.aws`, `~/.gnupg`, `~/.config/gh`, `~/.netrc`,
`~/.npmrc`, `~/.pypirc`, `~/.docker`, `~/.kube`, `~/Library/Keychains`, Claude
Code's own credentials, and the support folders of Chatter (its API token) and the
bridge (its TLS key and pairing data).

**Session options.** `permissionMode: "acceptEdits"`, always set explicitly, so
edits inside the project need no tap and the session never starts in auto mode or
`bypassPermissions`. `settingSources: []`, so no user, project or local settings,
hooks, skills or plugins load; the sidecar reads the project's `CLAUDE.md` (or
`AGENTS.md`) and appends it to the system prompt as the project's instructions.
`strictMcpConfig: true` with only the bridge's in-process `device` server, so the
user's claude.ai connectors and MCP servers stay out. Auto memory is off
(`CLAUDE_CODE_DISABLE_AUTO_MEMORY=1`). Turn and budget limits apply per job.

**What takes one tap on the board.** A command that leaves the sandbox (for
example a dev server that must listen, `docker`, or `git push` over SSH, which the
macOS sandbox cannot tunnel); a sandboxed connection to a host outside the
allow-list; edits outside the project; `WebFetch` to other hosts; and Claude Code's
own safety checks (protected paths, removals of critical paths). The card shows
the literal command or path, and an unanswered card denies.

The user can ask to drop the sandbox. That is a new decision that supersedes this
one, not a setting the voice path can change.

# Consequences

## Positive

- The user talks to any project without first listing it or its commands.
- What a command can reach is limited by the operating system, not by matching
  the command's text, so a command that does more than its name suggests is still
  contained.
- A repository cannot run code through hooks or loosen the sandbox through its
  settings, because no project settings load.

## Negative

- Inside the project, anything goes: a command can delete uncommitted work or the
  `.git` directory without a tap. A snapshot of each project into a store outside
  the sandbox's reach, taken by the bridge before each job, is planned (RFC-003
  phase 2); until then the user's own backups are the recovery path.
- Voice sessions ignore the project's Claude Code settings, hooks, skills and
  agents, and the user's global ones, so they behave less like the user's
  interactive sessions.
- The proxy allows by host name and does not inspect TLS, so allowed hosts such as
  GitHub remain a possible exfiltration path (for example through domain fronting).
  Without credentials, a command cannot publish there with the user's account.
- Whether a sandboxed command can reach the macOS keychain through its system
  service is not established; the bridge checks it with a harmless probe before
  the first release, and the result is recorded with phase 1.

## Neutral

- Tools that cannot work inside the sandbox (Go-based CLIs that verify TLS, `docker`,
  SSH remotes) cost a tap each time they run.
- The first decision's listed commands are superseded; `allowedTools` carries only
  the bridge's own tools.

# Alternatives Considered

## Listed commands per project (the first decision)

Safer, but the user replaced it: maintaining lists defeats talking to any project.

## Auto mode

A classifier reviews each action instead of a fixed boundary. It is model
judgment rather than operating-system enforcement, and it would also decide escapes
the user wants to see on the board.

## Strict sandbox mode

`allowUnsandboxedCommands: false` makes the sandbox admin-required, but then a
command that cannot run inside fails outright instead of asking for a tap. With no
project settings loaded, the admin-required mode adds little.

## Loading project settings for the user's own projects

Keeps project hooks and rules but needs a trust decision per project. Possible
later, with a one-time trust tap per project in the bridge.

# References

- [RFC-003: Voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- [Claude Code Bash sandbox](https://code.claude.com/docs/en/sandboxing.md)
- [Agent SDK permissions](https://code.claude.com/docs/en/agent-sdk/permissions.md)
- [Agent SDK and Claude Code features (settingSources)](https://code.claude.com/docs/en/agent-sdk/claude-code-features.md)
