import Foundation

/// Reads outside the project (ADR-014): a tap allows one, but never of a secret, of
/// the configuration that runs code, or of a folder that holds credentials, and never
/// a search over the home folder or above it, where all of those lie. One tap must not
/// hand a private key or a token to an injected prompt.
extension PermissionCards {
    /// Places under the home folder that hold credentials or private data.
    static let privateHomePaths = [".ssh", ".aws", ".azure", ".docker", ".gnupg", ".config", ".kube", ".claude",
                                   ".claude.json", ".password-store", ".cargo/credentials", ".cargo/credentials.toml",
                                   ".gem/credentials", ".local/share/keyrings", "Library"]

    /// Those places, and the bridge's and Chatter's own, as folded path components.
    static var privatePaths: [[String]] {
        let home = lexical(FileManager.default.homeDirectoryForCurrentUser.path)
        return (privateHomePaths.map { home + $0.split(separator: "/").map(String.init) }
            + SessionPolicy.secretFolders.map(lexical)).map { $0.map(folded) }
    }

    /// The paths an input names: every field whose name ends in "path", and the
    /// patterns Glob and Grep search with.
    static func paths(in input: JSONValue, tool: String) -> [String] {
        guard case .object(let fields) = input else { return [] }
        var paths = fields.filter { $0.key.lowercased().hasSuffix("path") }.sorted { $0.key < $1.key }
            .compactMap { $0.value.string }
        if tool == "Glob", let pattern = input["pattern"]?.string { paths.append(pattern) }
        if tool == "Grep", let glob = input["glob"]?.string { paths.append(glob) }
        return paths
    }

    /// Whether a read or search of any of these paths is refused without a card. A
    /// relative path is the project's; without a project it is refused.
    static func readRefused(_ paths: [String], project: Project?) -> Bool {
        paths.contains { written in
            let expanded = (written as NSString).expandingTildeInPath
            guard expanded.hasPrefix("/") || project != nil else { return true }
            let absolute = expanded.hasPrefix("/") ? expanded : project!.url.path + "/" + expanded
            let parts = lexical(absolute)
            if automounted(parts) { return true }
            // A pattern searches from the folder before its first glob character.
            let root = Array(parts.prefix { !$0.contains(where: { "*?[{".contains($0) }) })
            let lands = landing("/" + root.joined(separator: "/")).map(lexical) ?? root
            return refused(parts) || refused(root) || refused(lands)
        }
    }

    /// A path that is, holds, or lies in a private place, or names a protected or
    /// secret file.
    static func refused(_ parts: [String]) -> Bool {
        let path = "/" + parts.joined(separator: "/")
        if protected(path) || secret(path) { return true }
        let folded = parts.map(folded)
        return privatePaths.contains { place in folded.starts(with: place) || place.starts(with: folded) }
    }

    /// Whether the path is under an automounter (/net, /Network, /home), which looks a
    /// host name up as soon as the path is touched: it is refused before any lookup.
    static func automounted(_ parts: [String]) -> Bool {
        guard let first = parts.first else { return false }
        return ["net", "network", "home"].contains(folded(first))
    }
}
