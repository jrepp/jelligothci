import Foundation

/// ADR-014 as data: what a voice session may do without a tap, and how Claude Code
/// enforces it. The sidecar passes `settings` to the Agent SDK's `settings` option
/// (the command-line settings layer) and `options` as the query options. Nothing
/// here loosens with the project: no project, user or local settings load.
public struct SessionPolicy: Sendable {
    /// Hosts sandboxed commands may reach: Git hosting and the package registries.
    public static let developerHosts = [
        "github.com", "*.github.com", "*.githubusercontent.com",
        "registry.npmjs.org", "registry.yarnpkg.com",
        "pypi.org", "files.pythonhosted.org",
        "crates.io", "index.crates.io", "static.crates.io",
        "proxy.golang.org", "sum.golang.org",
        "components.espressif.com", "components-file.espressif.com", "dl.espressif.com",
    ]

    /// Folders under the home directory that builds read and that hold no credentials,
    /// re-opened inside the read block (ADR-014).
    public static let toolchainFolders = [
        "~/.cargo/bin", "~/.cargo/registry", "~/.rustup", "~/.npm/_cacache", "~/.nvm", "~/.pyenv",
        "~/.local/share/uv/python", "~/.cache/uv", "~/go/pkg/mod", "~/.swiftpm",
        "~/Library/Developer/Xcode/DerivedData", "~/Library/Caches/org.swift.swiftpm", "~/Library/Caches/pip",
        "~/Library/Caches/Homebrew", "~/Library/Caches/go-build",
    ]

    /// Environment variables that hold tokens, unset for sandboxed commands.
    public static let tokenVariables = [
        "GITHUB_TOKEN", "GH_TOKEN", "GITLAB_TOKEN", "NPM_TOKEN", "NODE_AUTH_TOKEN", "PYPI_TOKEN",
        "TWINE_PASSWORD", "CARGO_REGISTRY_TOKEN", "HF_TOKEN", "OPENAI_API_KEY", "ANTHROPIC_API_KEY",
        "AWS_ACCESS_KEY_ID", "AWS_SECRET_ACCESS_KEY", "AWS_SESSION_TOKEN", "GOOGLE_API_KEY",
        "BRAVE_API_KEY", "SLACK_TOKEN",
    ]

    /// Folders that hold the bridge's and Chatter's secrets; never re-opened.
    public static let secretFolders = [
        "~/Library/Application Support/Loki Bridge", "~/Library/Application Support/Chatter",
    ]

    /// Names of secret files found anywhere under a session's folder: never readable
    /// or writable from it (.env files, keys and keystores, package-manager, cloud,
    /// Docker, Terraform and git credentials, and local Claude Code settings).
    public static let secretNames = [".env", ".env.*", ".envrc", ".dev.vars", "*.pem", "*.key", "*.p8", "*.p12", "*.pfx",
                                     "*.jks", "*.keystore", "*.kdbx", "*.ovpn", "id_rsa*", "id_ecdsa*", "id_ed25519*",
                                     ".npmrc", ".netrc", ".pypirc", ".git-credentials", ".pgpass", "credentials.json",
                                     "service-account*.json", "secrets.*", "*.tfvars", "*.tfvars.json", "*.tfstate",
                                     "*.tfstate.backup", ".aws/credentials", ".aws/config", ".docker/config.json",
                                     ".claude/settings.local.json"]

    public var extraHosts: [String] = []
    /// Files and folders no session may change: the bridge's own code, which runs
    /// outside the sandbox (its sidecar, packages and build), and the tooling around it,
    /// so a session cannot rewrite the policy.
    public var protectedFolders: [String]
    /// Per session, at most this many turns and this estimated spend; a session that
    /// reaches either ends, and the next request to its place opens a new one.
    public static let maxTurns = 300
    public static let maxBudgetUsd = 25.0
    /// The model and effort for every voice session. Loki says nothing until Claude's first
    /// words, which took 1.8 to 4.2 s with the account default (memo-030); Michael chose
    /// Sonnet 5.5 at low effort on 2026-10-10.
    public static let model = "claude-sonnet-5-5"
    public static let effort = "low"

    public init(extraHosts: [String] = [], protectedFolders: [String] = []) {
        self.extraHosts = extraHosts
        self.protectedFolders = protectedFolders.map { URL(fileURLWithPath: $0).standardizedFileURL.path }
    }

    var hosts: [String] { Self.developerHosts + extraHosts }

    /// The flag-layer settings: the sandbox, the read block, and the rules that make
    /// leaving the sandbox take a tap.
    public var settings: [String: Any] { settings(denying: []) }

    /// The settings for a session in `root`: secrets under it unreadable and
    /// unwritable, `paths` (absolute) too, the protected folders and its hooks
    /// unwritable, and everything under it unwritable when `readOnly`, by sandboxed
    /// commands and by Claude Code's own file tools alike. A read-only session's
    /// commands have no network: it reads for the user, and nothing it read can leave
    /// from a command. Its WebFetch tool still reads the developer hosts without a tap.
    public func settings(root: URL? = nil, readOnly: Bool = false, denying paths: [String]) -> [String: Any] {
        let base = root.map { Self.loose($0.standardizedFileURL.path) }
        let secrets = base.map { base in Self.secretNames.map { "\(base)/**/\($0)" } } ?? []
        let unwritable = protectedFolders.map(Self.loose) + (root.map { Self.hookPaths(in: $0).map(Self.loose) } ?? [])
            + (readOnly ? base.map { [$0] } ?? [] : [])
        return [
            "sandbox": [
                "enabled": true,
                "failIfUnavailable": true,
                "autoAllowBashIfSandboxed": true,
                "allowUnsandboxedCommands": true,
                // Other hosts are refused, not prompted: the SDK does not say how such a
                // prompt would reach the board. Reaching one means leaving the sandbox,
                // which takes a tap.
                // An empty allow-list leaves the network open; a bare "*" denies every host.
                "network": (readOnly ? ["allowedDomains": [String](), "deniedDomains": ["*"]] : ["allowedDomains": hosts])
                    .merging(["strictAllowlist": true]) { $1 } as [String: Any],
                "filesystem": ["allowRead": Self.toolchainFolders, "denyRead": Self.secretFolders + paths + secrets,
                               "denyWrite": unwritable],
                "credentials": ["envVars": Self.tokenVariables.map { ["name": $0, "mode": "deny"] }],
            ] as [String: Any],
            "permissions": [
                "blockReadsOutsideWorkingDirectories": true,
                // Leaving the sandbox always takes a tap, whatever else would allow it.
                "ask": ["Bash(dangerouslyDisableSandbox:true)"],
                "allow": ["WebSearch"] + hosts.map { "WebFetch(domain:\($0))" },
                "deny": (paths + secrets).flatMap { ["Read(/\($0))", "Edit(/\($0))", "Write(/\($0))"] }
                    + unwritable.flatMap { ["Edit(/\($0))", "Write(/\($0))", "Edit(/\($0)/**)", "Write(/\($0)/**)"] },
            ] as [String: Any],
            "autoMemoryEnabled": false,
        ]
    }

    /// Everything directly in the projects folder that is not a project (files, links,
    /// hidden entries, folders a project cannot be), so the general session reads only
    /// the projects themselves. Glob characters in a name are matched loosely, so the
    /// name is still covered.
    public static func besideProjects(in folder: URL, projects: [Project]) -> [String] {
        let names = (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
        let keep = Set(projects.map { $0.url.lastPathComponent })
        return names.sorted().filter { !keep.contains($0) }.map { loose(folder.appendingPathComponent($0).standardizedFileURL.path) }
    }

    /// A path as a pattern that still matches it: each glob character (and the escape)
    /// matches any one character, so the path is covered, with at most a lookalike.
    static func loose(_ path: String) -> String {
        String(path.map { "*?[]{}\\".contains($0) ? "?" : $0 })
    }

    /// Where git and the common hook managers keep hooks, which run outside any
    /// sandbox when the user next commits in `root`: never writable from a session.
    /// Claude Code itself protects .git/hooks and .git/config.
    public static func hookPaths(in root: URL) -> [String] {
        let base = root.standardizedFileURL
        let common = [".githooks", ".husky", ".pre-commit-config.yaml", "lefthook.yml", "lefthook.yaml", ".lefthook.yml",
                      ".lefthook.yaml", "lefthook-local.yml", ".lefthook-local.yml", ".git/modules"]
        return common.map { base.appendingPathComponent($0).path } + (configuredHooks(in: base).map { [$0] } ?? [])
    }

    /// core.hooksPath from the project's .git/config, read as text (git never runs on a
    /// session's behalf here); a relative path is the project's.
    static func configuredHooks(in root: URL) -> String? {
        guard let data = regularFile(root.appendingPathComponent(".git/config"), upTo: 65_536) else { return nil }
        var core = false
        for line in String(decoding: data, as: UTF8.self).split(whereSeparator: \.isNewline) {
            let text = line.trimmingCharacters(in: .whitespaces)
            if text.hasPrefix("[") {
                core = text.lowercased().replacingOccurrences(of: " ", with: "") == "[core]"
                continue
            }
            let pair = text.split(separator: "=", maxSplits: 1).map { $0.trimmingCharacters(in: .whitespaces) }
            guard core, pair.count == 2, pair[0].lowercased() == "hookspath", !pair[1].isEmpty else { continue }
            let value = (pair[1].trimmingCharacters(in: CharacterSet(charactersIn: "\"")) as NSString).expandingTildeInPath
            return (value.hasPrefix("/") ? URL(fileURLWithPath: value) : root.appendingPathComponent(value)).standardizedFileURL.path
        }
        return nil
    }

    /// The general session's extra rules (RFC-003): what to do with requests that
    /// name no project.
    public static let generalRules = """
        This is the general session, working from the folder that holds the user's projects. Answer questions \
        briefly and plainly. It can read the projects and search the web but cannot change files: when the user \
        wants a change in a project, tell them to ask for it in that project by name ("in chatter, ..."). Files \
        beside the projects are off limits.
        """

    /// Query options for the general session, rooted at `folder`: read-only, and
    /// unable to read anything beside the projects.
    public func generalOptions(folder: URL, projects: [Project], operatorRules: String) -> [String: Any] {
        var options = options(project: Project(name: "General", url: folder), instructions: nil,
                              operatorRules: operatorRules + "\n\n" + Self.generalRules)
        options["settings"] = settings(root: folder, readOnly: true,
                                       denying: Self.besideProjects(in: folder, projects: projects))
        return options
    }

    /// Query options for one session in `project`. `instructions` is the project's
    /// CLAUDE.md or AGENTS.md, read by the bridge, since no settings load from disk.
    public func options(project: Project, instructions: String?, operatorRules: String) -> [String: Any] {
        let append = [operatorRules, instructions.map { "Project instructions (\($0.count) characters):\n\($0)" }]
            .compactMap { $0 }.joined(separator: "\n\n")
        return [
            "cwd": project.url.path,
            "permissionMode": "acceptEdits",
            "settingSources": [String](),
            "strictMcpConfig": true,
            "systemPrompt": ["type": "preset", "preset": "claude_code", "append": append],
            "settings": settings(root: project.url, denying: []),
            // Tool search off: the session's only MCP tools are the device's speak and
            // progress, so loading them up front costs little, and saves the turn Claude
            // otherwise spends looking speak up before it can answer.
            "env": ["CLAUDE_CODE_DISABLE_AUTO_MEMORY": "1", "CLAUDE_CODE_EMIT_SESSION_STATE_EVENTS": "1",
                    "ENABLE_CLAUDEAI_MCP_SERVERS": "false", "ENABLE_TOOL_SEARCH": "false"],
            "includePartialMessages": true,
            "maxTurns": Self.maxTurns,
            "maxBudgetUsd": Self.maxBudgetUsd,
            "model": Self.model,
            "effort": Self.effort,
        ]
    }

    /// The project's own instructions, as Claude Code would read them: CLAUDE.md,
    /// else AGENTS.md, at most 64 KB. The bridge reads them outside the sandbox, so only
    /// a regular file in the project itself counts: never a link, which could point
    /// anywhere (a secret would otherwise go to Claude as instructions).
    public static func instructions(in project: Project) -> String? {
        for name in ["CLAUDE.md", "AGENTS.md"] {
            // A cut inside a character must not lose the whole file: decode leniently.
            if let data = regularFile(project.url.appendingPathComponent(name), upTo: 65_536), !data.isEmpty {
                return String(decoding: data, as: UTF8.self)
            }
        }
        return nil
    }

    /// The first bytes of a regular file with a single name: never through a link or a
    /// second hard link, either of which could stand for a file outside the project,
    /// and never blocking on a pipe.
    static func regularFile(_ url: URL, upTo limit: Int) -> Data? {
        let descriptor = open(url.path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
        guard descriptor >= 0 else { return nil }
        let handle = FileHandle(fileDescriptor: descriptor, closeOnDealloc: true)
        defer { try? handle.close() }
        var info = stat()
        guard fstat(descriptor, &info) == 0, info.st_mode & S_IFMT == S_IFREG, info.st_nlink == 1 else { return nil }
        return try? handle.read(upToCount: limit)
    }
}
