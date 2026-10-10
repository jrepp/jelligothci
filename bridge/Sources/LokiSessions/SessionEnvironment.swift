import Foundation

/// The environment Claude Code sessions get from the bridge (ADR-014): only what Claude
/// Code and builds need, never a token that happens to be set in the shell, and a PATH
/// a session cannot plant programs on. Claude Code runs some programs itself, outside
/// the sandbox (git, at the start of a session), so a PATH entry a session can write
/// to would let it out.
public enum SessionEnvironment {
    /// The only variables passed on, besides the locale's LC_* ones.
    public static let passedVariables: Set<String> = ["HOME", "PATH", "USER", "LOGNAME", "SHELL", "TMPDIR", "TERM",
                                                      "LANG", "__CF_USER_TEXT_ENCODING", "SSH_AUTH_SOCK"]

    /// `environment` cut down to the passed variables, with its PATH fenced.
    public static func filtered(_ environment: [String: String], outside folders: [URL]) -> [String: String] {
        var kept = environment.filter { passedVariables.contains($0.key) || $0.key.hasPrefix("LC_") }
        if let path = kept["PATH"] { kept["PATH"] = fencedPath(path, outside: folders) }
        return kept
    }

    /// PATH without the entries a session could write to: relative ones (including an
    /// unexpanded "~/..."), which resolve against the session's project, empty ones,
    /// which mean the project itself, ones holding control characters, and ones inside
    /// `folders` (the project roots). Order is kept and repeats are dropped.
    public static func fencedPath(_ path: String, outside folders: [URL]) -> String {
        let fenced = folders.map { $0.resolvingSymlinksInPath().standardizedFileURL.path }
        var seen = Set<String>()
        return path.split(separator: ":", omittingEmptySubsequences: true).map(String.init).filter { entry in
            guard entry.hasPrefix("/"), !entry.unicodeScalars.contains(where: { $0.properties.generalCategory == .control }),
                  !seen.contains(entry)
            else { return false }
            seen.insert(entry)
            let real = URL(fileURLWithPath: entry).resolvingSymlinksInPath().standardizedFileURL.path
            return !fenced.contains { real == $0 || real.hasPrefix($0 + "/") }
        }.joined(separator: ":")
    }
}
