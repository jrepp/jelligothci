import Foundation

/// How a permission prompt from Claude Code becomes an approval card on the board:
/// the literal action, whole (ApprovalCard), or nothing, in which case it is denied.
public enum PermissionCards {
    public struct Card: Equatable, Sendable {
        public let title: String
        public let detail: String
    }

    /// Files whose change runs code later, outside any sandbox: Claude Code's and git's
    /// configuration and hooks, hook managers' files, shell start-up files, launch
    /// agents, SSH. A voice session never changes them, not even with a tap. The path
    /// is taken without "~", "." or "..", and names are compared ignoring case and
    /// Unicode form, as APFS does.
    static func protected(_ path: String) -> Bool {
        let parts = lexical(path).map(folded)
        if parts.contains(where: { [".claude", ".ssh", "launchagents", "launchdaemons", ".githooks", ".husky"].contains($0) }) {
            return true
        }
        if let git = parts.lastIndex(of: ".git"), git + 1 < parts.count,
           ["hooks", "config", "info", "modules"].contains(parts[git + 1]) {
            return true
        }
        let name = parts.last ?? ""
        return [".zshrc", ".zprofile", ".zshenv", ".zlogin", ".zlogout", ".bashrc", ".bash_profile", ".bash_login",
                ".profile", ".gitconfig", ".mcp.json", ".pre-commit-config.yaml"].contains(name)
            || (name.hasPrefix("lefthook") || name.hasPrefix(".lefthook")) && (name.hasSuffix(".yml") || name.hasSuffix(".yaml"))
    }

    static func folded(_ name: String) -> String {
        name.folding(options: [.caseInsensitive], locale: nil).precomposedStringWithCanonicalMapping
    }

    /// The path's components with "~" expanded and "." and ".." taken out, as written.
    static func lexical(_ path: String) -> [String] {
        var parts: [String] = []
        for part in (path as NSString).expandingTildeInPath.split(separator: "/") {
            switch part {
            case ".": continue
            case "..": if !parts.isEmpty { parts.removeLast() }
            default: parts.append(String(part))
            }
        }
        return parts
    }

    /// Where a write to an absolute `path` lands: through the links in the folders that
    /// exist, and through a link at the end, which a write follows even to a file not
    /// there yet. Nil for a relative path or a chain of links too long to follow.
    static func landing(_ path: String) -> String? {
        let expanded = (path as NSString).expandingTildeInPath
        guard expanded.hasPrefix("/") else { return nil }
        var current = URL(fileURLWithPath: "/" + lexical(expanded).joined(separator: "/"))
        for _ in 0..<16 {
            var folder = current.deletingLastPathComponent(), rest = [current.lastPathComponent]
            while folder.path != "/", !FileManager.default.fileExists(atPath: folder.path) {
                rest.insert(folder.lastPathComponent, at: 0)
                folder.deleteLastPathComponent()
            }
            current = rest.reduce(folder.resolvingSymlinksInPath()) { $0.appendingPathComponent($1) }
            guard let target = try? FileManager.default.destinationOfSymbolicLink(atPath: current.path) else {
                return current.standardizedFileURL.path
            }
            let next = target.hasPrefix("/") ? target : current.deletingLastPathComponent().path + "/" + target
            current = URL(fileURLWithPath: "/" + lexical(next).joined(separator: "/"))
        }
        return nil
    }

    static let editTools: Set<String> = ["Edit", "Write", "MultiEdit", "NotebookEdit"]

    /// Whether the path names a secret file (SessionPolicy.secretNames), compared as
    /// APFS does. A read of one is refused without a card, wherever it lies.
    static func secret(_ path: String) -> Bool {
        let parts = lexical(path).map(folded)
        return SessionPolicy.secretNames.contains { pattern in
            let names = pattern.split(separator: "/").map { folded(String($0)) }
            guard parts.count >= names.count else { return false }
            return zip(names, parts.suffix(names.count)).allSatisfy { fnmatch($0, $1, 0) == 0 }
        }
    }

    /// The card for a permission prompt, or nil to deny it. `blockedPath` is the path
    /// Claude Code says it checked. A change is judged, and shown, by where it lands, so
    /// a link in the project cannot dress a protected or outside file as its own.
    public static func card(tool: String, input: JSONValue, project: Project?, blockedPath: String? = nil) -> Card? {
        let place = project?.name ?? ""
        var (title, action): (String, String?) = switch tool {
        case "Bash":
            (input["dangerouslyDisableSandbox"]?.bool == true ? "Run outside the sandbox?" : "Run this command?",
             input["command"]?.string)
        case "NotebookEdit":
            ("Change a file outside the project?", input["notebook_path"]?.string ?? input["file_path"]?.string)
        case "Edit", "Write", "MultiEdit":
            ("Change a file outside the project?", input["file_path"]?.string)
        case "Read":
            ("Read outside the project?", input["file_path"]?.string)
        case "Glob", "Grep":
            // What is searched for, where, and in which files: the whole search.
            ("Search outside the project?",
             [input["pattern"]?.string, input["path"]?.string.map { "under \($0)" }, input["glob"]?.string.map { "files \($0)" }]
                .compactMap { $0 }.joined(separator: "\n"))
        case "WebFetch":
            ("Read this web page?", input["url"]?.string)
        default:
            ("Allow \(tool)?", compact(input))
        }
        if editTools.contains(tool) {
            guard let path = action, !automounted(lexical(path)), let lands = landing(path) else { return nil }
            let paths = [path, lands] + (blockedPath.map { [$0] + (landing($0).map { [$0] } ?? []) } ?? [])
            if paths.contains(where: protected) { return nil }
            action = lands
        } else if !["Bash", "WebFetch", "WebSearch"].contains(tool),
                  readRefused(paths(in: input, tool: tool) + (blockedPath.map { [$0] } ?? []), project: project) {
            return nil  // ReadPolicy: secrets, private folders, and searches over them
        }
        guard let action, !action.isEmpty,
              let content = ApprovalCard.content(title: title, action: action, place: place)
        else { return nil }
        return Card(title: content.title, detail: content.detail)
    }

    /// A tool's input as one line of JSON, for tools without a known shape.
    static func compact(_ value: JSONValue) -> String? {
        let object = value.foundation
        guard JSONSerialization.isValidJSONObject(object),
              let data = try? JSONSerialization.data(withJSONObject: object, options: [.sortedKeys])
        else { return nil }
        return String(data: data, encoding: .utf8)
    }
}
