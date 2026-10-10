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
changing it amends this ADR. Claude amended it that evening, after two security
reviews of the first running version: near-miss names are confirmed, the general
session is read-only and has no network, secret file names are denied in every
session, git hooks and the bridge's own checkout are write-protected, sessions use
the Agent SDK's bundled Claude Code with a filtered environment and a fenced `PATH`,
protected configuration is refused without a card, and cards are shown one at a
time.

**Any project.** The bridge resolves the spoken name against the user's project
roots (`~/dev` to start) to one directory directly under a root. A name said
exactly, after normalizing case, spaces and punctuation, runs without a tap. A near
miss (a recognizer's spelling such as "codergotchy") is confirmed on the board ("Did
you mean codergotchi?"), an ambiguous match becomes a choice, and a declined or
unanswered card drops the request aloud. Each job in a project starts with a spoken
"Okay, in codergotchi.", so a wrong place is caught before much happens. The
resolved real path must stay inside a root, so a root itself, the home directory, and
symlinks that lead out are refused. So is a folder whose path holds a glob character
(`*`, `?`, `[`, `{` or `\`): Claude Code does not apply its read block to commands
when the working directory's path has one.

A request that names no project follows up the job the user last started or heard
about, while it runs or for two minutes after it ends (stopped jobs do not count).
When more than one place is busy, the board asks which one. Otherwise the request
goes to the general session.

**The general session.** Questions that name no project (the user's decision,
2026-10-09) go to one session working from the projects folder. It reads the
projects and searches the web but changes nothing: its whole folder is write-denied
for commands and for the Edit and Write tools, and every entry beside the projects
(files, links, hidden folders) is read-denied, so a token file at the top of `~/dev`
stays out of reach. Its commands have no network (`deniedDomains: ["*"]`; an empty
allow-list alone leaves the network open), so nothing it reads can leave through an
allowed host; it searches and reads the web through Claude Code's own tools. A change has to be asked for in a project by name. The session reopens after
ten idle minutes with a fresh view of the folder.

**Any command, inside the sandbox.** Each session's Bash commands run sandboxed and
auto-approved, configured through the SDK's `settings` option (the command-line
settings layer):

| Setting | Value | Effect |
| --- | --- | --- |
| `sandbox.enabled`, `failIfUnavailable` | `true`, `true` | No sandbox means no session, never unsandboxed commands |
| `sandbox.autoAllowBashIfSandboxed` | `true` | Commands the user asks for run without a tap |
| `sandbox.allowUnsandboxedCommands` | `true` | A command that cannot work inside may ask to leave it |
| `permissions.ask` | `Bash(dangerouslyDisableSandbox:true)` | Leaving the sandbox always takes a tap |
| `sandbox.network.allowedDomains`, `strictAllowlist` | Developer hosts, `true` | Package registries and Git hosting (below); other hosts are refused rather than prompted, since the SDK documents no way for that prompt to reach the board |
| `permissions.blockReadsOutsideWorkingDirectories` | `true` | Commands and the file tools read only the project, not the rest of the home directory |
| `sandbox.filesystem.allowRead` | Toolchain folders | Re-opens what builds need under the home directory (below) |
| `sandbox.credentials.envVars` | Token variables, `deny` | `GITHUB_TOKEN`, `GH_TOKEN`, `NPM_TOKEN` and similar are unset for commands |
| `sandbox.filesystem.denyRead`, `permissions.deny` | Secret file names anywhere under the session's folder | `.env*`, keys and certificates, `id_rsa*`, `.npmrc`, `.netrc`, `.pypirc`, `.git-credentials`, `.pgpass`, `credentials.json`: unreadable and unwritable by commands and by the file tools |
| `sandbox.filesystem.denyRead`, `permissions.deny` | More secret names | `.envrc`, `.dev.vars`, Terraform variables and state, `.aws` and Docker credentials, keystores, `secrets.*`, `.claude/settings.local.json` |
| `sandbox.filesystem.denyWrite`, `permissions.deny` | Git hooks in each project | `core.hooksPath` (read from `.git/config` as text), `.githooks`, `.husky`, `.pre-commit-config.yaml`, Lefthook files and `.git/modules`: a commit would run them outside the sandbox |
| `sandbox.filesystem.denyWrite`, `permissions.deny` | The bridge and its checkout's tooling | `bridge/`, and the checkout's `.githooks`, `scripts`, `Makefile`, `toolchain.env`, `.pre-commit-config.yaml` and `.tools`: a session cannot rewrite code that later runs outside the sandbox |

The network allow-list starts with GitHub (`github.com`, `*.github.com`,
`*.githubusercontent.com`), npm and Yarn, PyPI (`pypi.org`,
`files.pythonhosted.org`), crates.io, the Go module proxy, and Espressif's
component and download hosts. The bridge's settings can extend it. `WebFetch`
allow rules mirror the list, and `WebSearch` is allowed.

Reads are blocked rather than listed because credentials do not only live in known
places: a token file can sit at the top of a project root, beside the projects.
Under the block a sandboxed command reads the project, the session's temp
directory, the system, and the user's global Git configuration; Claude Code keeps
`~/.git-credentials` blocked. The bridge re-opens only toolchain folders that hold
no credentials (package caches, language toolchains such as `~/.cargo` and
`~/.rustup`, Xcode's `~/Library/Developer`), and `denyRead` still covers any
re-opened file that holds a secret. The support folders of Chatter (its API token)
and the bridge (its TLS key and pairing data) are never re-opened.

**Session options.** `permissionMode: "acceptEdits"`, always set explicitly, so
edits inside the project need no tap and the session never starts in auto mode or
`bypassPermissions`. `settingSources: []`, so no user, project or local settings,
hooks, skills or plugins load; the bridge reads the project's `CLAUDE.md` (or
`AGENTS.md`), only as a regular file and never through a symlink, up to 64 KB, and
appends it to the system prompt as the project's instructions.
`strictMcpConfig: true` with only the bridge's in-process `device` server, so the
user's claude.ai connectors and MCP servers stay out. Auto memory is off
(`autoMemoryEnabled: false`). Each session stops after 300 turns or an estimated
25 US dollars (`maxTurns`, `maxBudgetUsd`); the next request to its place opens a
new one.

Sessions run the Claude Code bundled with the pinned Agent SDK. Another binary
(`loki-bridge run --claude`) must be 2.1.296 or newer, since older versions ignore
settings this policy depends on. The sidecar receives only a short list of the
bridge's environment variables (home, path, user, shell, locale, terminal, the SSH
agent socket), so other secrets in the bridge's environment never reach a session.
`PATH` itself is fenced: relative and empty entries (which resolve inside the
project) and entries inside a project root are dropped, since Claude Code runs some
programs itself outside the sandbox, such as `git` when a session starts. `node` is
looked up on the same fenced `PATH`.

**What takes one tap on the board.** A command that leaves the sandbox (for
example a dev server that must listen, `docker`, `git push` over SSH, which the
macOS sandbox cannot tunnel, or a command that needs a host outside the allow-list);
edits outside the project; `WebFetch` to other hosts; and Claude Code's own safety
checks (removals of critical paths). The card shows the literal command or path,
whole or not at all, in at most eight lines, without invisible or
direction-changing characters or runs of spaces, and an unanswered card denies. A
change is judged and shown by where it lands: through links in its folders, and
through a link at its end, so a link inside the project cannot dress an outside or
protected file as its own. The board holds one card, so cards are shown one at a
time in the order asked, and those still waiting when the board leaves are denied.

Edits to configuration that runs code later (`.claude`, `.git/hooks`, `.git/config`,
`.git/info`, `.git/modules`, `.githooks`, `.husky`, `.pre-commit-config.yaml`,
Lefthook files, `.mcp.json`, shell startup files, `.gitconfig`, `.ssh`, launch agents
and daemons) are refused without a card, comparing names without case as APFS does,
so a single tap cannot install a hook that runs outside the sandbox.

Claude Code itself refuses its file tools' reads outside the working directory
(`blockReadsOutsideWorkingDirectories`; a canary confirmed no card is asked), so a
read outside the project never becomes a card today. As a second line, should a
later Claude Code ask instead, the bridge refuses without a card any read of
protected configuration, of secret files (the names above), or of the places
credentials live (`~/.ssh`, `~/.aws`, `~/.config`, `~/.gnupg`, `~/.kube`,
`~/.docker`, `~/.claude`, `~/.claude.json`, `~/Library`, and the bridge's and
Chatter's folders), and any search over the home folder or above it. Every path a
tool names counts, including a search pattern and file filter, and a search's card
shows what it looks for, where, and in which files. Claude is told when the policy,
not the user, refused.

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
- The general session changes nothing, so "fix the build" without a project name
  gets an answer, not a change, until the user names the project.
- A session reads one project. Work that spans two projects, or a tool that needs a
  folder under the home directory the bridge has not re-opened, fails inside the
  sandbox and then asks to run outside it (a tap).
- Code a project session changes runs with the user's full rights when the user,
  or their interactive Claude session, later builds or tests it. Only the hook entry
  files are protected (the bridge's own checkout also protects its scripts, Makefile
  and toolchain); the code a hook calls, such as `package.json` scripts, a project's
  Makefile or its `scripts/`, stays writable and runs at the next commit. Review a
  voice session's changes before committing or running them.
- The proxy allows by host name and does not inspect TLS, so a project session's
  commands can still send what that project holds to an allowed host such as GitHub,
  using credentials an injected prompt supplies (the user's are not available to
  them). The secret names above keep the usual credential files out of that; tokens
  written into other files, such as a remote URL in `.git/config`, are not covered.
- Whether a sandboxed command can reach the macOS keychain through its system
  service is not established; the bridge checks it with a harmless probe before
  the first release, and the result is recorded with phase 1.

## Neutral

- Canary probes against the bundled Claude Code 2.1.296 (2026-10-09), in a scratch
  project, confirmed the enforcement rather than the model's restraint: a secret
  file and files beside the projects were refused to commands and to the Read tool,
  hook folders could not be created by commands or the Write tool, the general
  session could not write or reach GitHub, project sessions could write their
  project and reach GitHub, and a Read outside the project was refused by Claude
  Code without asking. That version has no separate Grep or Glob tools.

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
