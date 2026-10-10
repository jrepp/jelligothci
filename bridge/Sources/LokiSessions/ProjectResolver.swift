import Foundation

/// A project the voice agent may work in: a directory directly under one of the
/// user's project roots (ADR-014).
public struct Project: Equatable, Sendable {
    public let name: String
    public let url: URL
}

/// Finds the project the user named. Speech recognizers spell names their own way
/// ("Coder Gotchi"), so names are compared without case, spaces or punctuation, and
/// a near miss counts when it is the only one. A project is a real directory
/// directly under its root: symlinks are ignored, so none can lead out of it, and
/// neither a root itself nor the home folder is ever a project. Nor is a folder whose
/// path holds a glob character: Claude Code's read block does not hold there.
public struct ProjectResolver: Sendable {
    public enum Resolution: Equatable, Sendable {
        case found(Project)
        case ambiguous([Project])
        case none
    }

    public let roots: [URL]

    public init(roots: [URL] = [FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("dev")]) {
        self.roots = roots
    }

    public func projects() -> [Project] {
        let home = FileManager.default.homeDirectoryForCurrentUser.resolvingSymlinksInPath().standardizedFileURL.path
        var seen = Set<String>(), found: [Project] = []
        for root in roots {
            let real = root.resolvingSymlinksInPath().standardizedFileURL
            guard let names = try? FileManager.default.contentsOfDirectory(atPath: real.path) else { continue }
            for name in names.sorted() where !name.hasPrefix(".") {
                let url = real.appendingPathComponent(name).standardizedFileURL
                let kind = try? FileManager.default.attributesOfItem(atPath: url.path)[.type] as? FileAttributeType
                guard kind == .typeDirectory, url.path.hasPrefix(real.path + "/"), url.path != home,
                      Self.plain(url.path), !seen.contains(url.path)
                else { continue }
                seen.insert(url.path)
                found.append(Project(name: name, url: url))
            }
        }
        return found
    }

    /// Whether a path holds no glob character, so Claude Code's sandbox takes it as
    /// written.
    public static func plain(_ path: String) -> Bool {
        !path.contains { "*?[]{}\\".contains($0) }
    }

    /// Letters and digits only, lowercased: "Coder-Gotchi" and "coder gotchi" match.
    public static func key(_ text: String) -> String {
        String(text.lowercased().unicodeScalars.filter { CharacterSet.alphanumerics.contains($0) }.map(Character.init))
    }

    public func resolve(_ spoken: String) -> Resolution {
        let wanted = Self.key(spoken)
        guard !wanted.isEmpty else { return .none }
        let all = projects()
        let exact = all.filter { Self.key($0.name) == wanted }
        if exact.count == 1 { return .found(exact[0]) }
        if exact.count > 1 { return .ambiguous(exact) }
        let near = all.filter { Self.close(Self.key($0.name), wanted) }
        switch near.count {
        case 0: return .none
        case 1: return .found(near[0])
        default: return .ambiguous(near)
        }
    }

    /// Words after which a project's name is expected ("in codergotchi", "on the
    /// chatter project"). Only these phrases are matched: a project named like an
    /// ordinary word ("context") must not be picked out of "add context to the readme".
    static let pointers: Set<String> = ["in", "on", "for", "inside", "within", "to", "open", "at"]
    static let fillers: Set<String> = ["the", "my", "our", "a", "project", "repo", "repository", "folder"]

    /// The project an utterance names after a pointer word; .none when it names none.
    public func mentioned(in utterance: String) -> Resolution {
        let words = utterance.lowercased()
            .split(whereSeparator: { !$0.isLetter && !$0.isNumber && $0 != "-" && $0 != "_" }).map(String.init)
        let all = projects()
        var exact: [Project] = [], near: [Project] = []
        for (index, word) in words.enumerated() where Self.pointers.contains(word) {
            var start = index + 1
            while start < words.count, Self.fillers.contains(words[start]) { start += 1 }
            for length in 1...3 where start + length <= words.count {
                let key = Self.key(words[start..<(start + length)].joined())
                for project in all {
                    let name = Self.key(project.name)
                    if name == key {
                        if !exact.contains(project) { exact.append(project) }
                    } else if Self.close(name, key), !near.contains(project) {
                        near.append(project)
                    }
                }
            }
        }
        let pick = exact.isEmpty ? near : exact
        switch pick.count {
        case 0: return .none
        case 1: return .found(pick[0])
        default: return .ambiguous(pick)
        }
    }

    /// A near miss: one name starts the other and covers at least half of it, or they
    /// differ by a small edit.
    static func close(_ name: String, _ wanted: String) -> Bool {
        guard name.count >= 4, wanted.count >= 4 else { return false }
        if name.hasPrefix(wanted) || wanted.hasPrefix(name) { return min(name.count, wanted.count) * 2 >= max(name.count, wanted.count) }
        return misspelled(name, wanted)
    }

    /// A recognizer's misspelling ("chater", "codergotchy"): a small edit of a name of
    /// four or more letters, never a longer phrase that starts with it.
    static func misspelled(_ name: String, _ wanted: String) -> Bool {
        guard name.count >= 4, wanted.count >= 4 else { return false }
        return distance(Array(name), Array(wanted)) <= max(1, max(name.count, wanted.count) / 6)
    }

    static func distance(_ a: [Character], _ b: [Character]) -> Int {
        var previous = Array(0...b.count)
        for (i, x) in a.enumerated() {
            var current = [i + 1]
            for (j, y) in b.enumerated() {
                current.append(min(previous[j + 1] + 1, current[j] + 1, previous[j] + (x == y ? 0 : 1)))
            }
            previous = current
        }
        return previous[b.count]
    }
}
