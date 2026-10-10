import Foundation
import Testing
@testable import LokiSessions

// The session policy at its edges: project instructions are read only from a regular
// file in the project, without blocking and at most 64 KB; secret globs and protected
// folders cover the real paths however the root is written; and what lies beside the
// projects is covered whatever its name holds.

private func temporaryFolder(_ prefix: String = "loki-policy") throws -> URL {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("\(prefix)-\(UUID())")
    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    return url
}

private func value(_ object: [String: Any], _ path: String) -> Any? {
    path.split(separator: ".").reduce(object as Any?) { ($0 as? [String: Any])?[String($1)] }
}

/// Whether a glob, read the way Claude Code reads permission and sandbox paths
/// (gitignore style: "**/" is zero or more folders, "*" and "?" stay inside one, "[...]"
/// is a class, "{a,b}" a choice, "\" escapes), matches a path.
func globMatches(_ glob: String, _ path: String) -> Bool {
    var pattern = "^", chars = Array(glob), index = 0, braces = 0
    while index < chars.count {
        let c = chars[index]
        switch c {
        case "*" where index + 2 < chars.count && chars[index + 1] == "*" && chars[index + 2] == "/":
            pattern += "(.*/)?"
            index += 2
        case "*" where index + 1 < chars.count && chars[index + 1] == "*":
            pattern += ".*"
            index += 1
        case "*": pattern += "[^/]*"
        case "?": pattern += "[^/]"
        case "[":
            guard let close = chars[(index + 1)...].firstIndex(of: "]") else { pattern += "\\["; break }
            pattern += "[" + NSRegularExpression.escapedPattern(for: String(chars[(index + 1)..<close])) + "]"
            index = close
        case "{":
            pattern += "("
            braces += 1
        case "}" where braces > 0:
            pattern += ")"
            braces -= 1
        case "," where braces > 0: pattern += "|"
        case "\\" where index + 1 < chars.count:
            pattern += NSRegularExpression.escapedPattern(for: String(chars[index + 1]))
            index += 1
        default: pattern += NSRegularExpression.escapedPattern(for: String(c))
        }
        index += 1
    }
    guard let regex = try? NSRegularExpression(pattern: pattern + "$") else { return false }
    return regex.firstMatch(in: path, range: NSRange(path.startIndex..., in: path)) != nil
}

@Test func theGlobOracleReadsPatternsTheGitignoreWay() {
    #expect(globMatches("/r/**/.env", "/r/.env") && globMatches("/r/**/.env", "/r/a/b/.env"))
    #expect(!globMatches("/r/**/.env", "/r/.envrc") && !globMatches("/r/*.pem", "/r/a/x.pem"))
    #expect(globMatches("/r/a?b", "/r/a*b") && globMatches("/r/[x]", "/r/x") && !globMatches("/r/[x]", "/r/[x]"))
    #expect(globMatches("/r/{a,b}", "/r/b") && globMatches("/r/a\\*", "/r/a*") && !globMatches("/r/a\\*", "/r/ab"))
    #expect(globMatches("/r/a,b", "/r/a,b") && !globMatches("/r/a,b", "/r/a"))
}

// MARK: Project instructions

/// A link named CLAUDE.md (to a secret, or dangling) is never followed: AGENTS.md is
/// read instead, or nothing.
@Test func instructionsNeverFollowALink() throws {
    let project = try temporaryFolder(), outside = try temporaryFolder()
    let secret = outside.appendingPathComponent("id_rsa")
    try Data("SECRET-CANARY-0001".utf8).write(to: secret)
    try FileManager.default.createSymbolicLink(at: project.appendingPathComponent("CLAUDE.md"), withDestinationURL: secret)
    let place = Project(name: "p", url: project)
    #expect(SessionPolicy.instructions(in: place) == nil)
    try Data("Use make test.".utf8).write(to: project.appendingPathComponent("AGENTS.md"))
    #expect(SessionPolicy.instructions(in: place) == "Use make test.")
    // Both links: nothing, and never the secret.
    try FileManager.default.removeItem(at: project.appendingPathComponent("AGENTS.md"))
    try FileManager.default.createSymbolicLink(atPath: project.appendingPathComponent("AGENTS.md").path,
                                               withDestinationPath: "/nonexistent/\(UUID())")
    #expect(SessionPolicy.instructions(in: place) == nil)
    // A link to a regular file inside the project is refused too: links are never read.
    try FileManager.default.removeItem(at: project.appendingPathComponent("CLAUDE.md"))
    try Data("inside".utf8).write(to: project.appendingPathComponent("notes.md"))
    try FileManager.default.createSymbolicLink(atPath: project.appendingPathComponent("CLAUDE.md").path,
                                               withDestinationPath: "notes.md")
    #expect(SessionPolicy.instructions(in: place) == nil)
}

/// A FIFO named CLAUDE.md with no writer would block a plain open forever: it is
/// refused at once and AGENTS.md read instead.
@Test func instructionsDoNotBlockOnAFIFO() async throws {
    let project = try temporaryFolder()
    #expect(mkfifo(project.appendingPathComponent("CLAUDE.md").path, 0o600) == 0)
    try Data("agents".utf8).write(to: project.appendingPathComponent("AGENTS.md"))
    let result = Box()
    DispatchQueue.global().async { result.set(SessionPolicy.instructions(in: Project(name: "p", url: project))) }
    #expect(await eventually(5) { result.done }, "instructions(in:) blocked on a FIFO")
    #expect(result.value == "agents")
    // A FIFO that holds data (its writer still open) is not a file either: never read.
    let fifo = open(project.appendingPathComponent("CLAUDE.md").path, O_RDWR | O_NONBLOCK)
    try #require(fifo >= 0)
    defer { close(fifo) }
    #expect(Array("from a pipe".utf8).withUnsafeBytes { write(fifo, $0.baseAddress, $0.count) } == 11)
    #expect(SessionPolicy.instructions(in: Project(name: "p", url: project)) == "agents")
}

/// The answer from a background read, once it is in.
private final class Box: @unchecked Sendable {
    private let lock = NSLock()
    private var answer: String??
    func set(_ value: String?) { lock.withLock { answer = .some(value) } }
    var done: Bool { lock.withLock { answer != nil } }
    var value: String? { lock.withLock { answer ?? nil } }
}

/// A folder, an unreadable file or an empty file named CLAUDE.md does not count:
/// AGENTS.md is next.
@Test func instructionsSkipFoldersUnreadableAndEmptyFiles() throws {
    let project = try temporaryFolder(), place = Project(name: "p", url: project)
    let claude = project.appendingPathComponent("CLAUDE.md")
    try Data("agents".utf8).write(to: project.appendingPathComponent("AGENTS.md"))
    try FileManager.default.createDirectory(at: claude, withIntermediateDirectories: true)
    #expect(SessionPolicy.instructions(in: place) == "agents")
    try FileManager.default.removeItem(at: claude)
    try Data("secret".utf8).write(to: claude)
    try FileManager.default.setAttributes([.posixPermissions: 0o000], ofItemAtPath: claude.path)
    defer { try? FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: claude.path) }
    #expect(SessionPolicy.instructions(in: place) == "agents")
    try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: claude.path)
    try Data().write(to: claude)
    #expect(SessionPolicy.instructions(in: place) == "agents")
    // A missing project folder has none.
    #expect(SessionPolicy.instructions(in: Project(name: "x", url: project.appendingPathComponent("missing"))) == nil)
}

/// At most 64 KB: exactly 65,536 bytes whole, one more cut; bytes that are not UTF-8
/// become replacement characters instead of losing the file.
@Test func instructionsAreAtMost64KBAndDecodedLeniently() throws {
    let project = try temporaryFolder(), place = Project(name: "p", url: project)
    let claude = project.appendingPathComponent("CLAUDE.md")
    try Data(repeating: 0x62, count: 65_536).write(to: claude)
    #expect(SessionPolicy.instructions(in: place)?.utf8.count == 65_536)
    try (Data(repeating: 0x62, count: 65_536) + Data("SECRET-TAIL".utf8)).write(to: claude)
    let cut = try #require(SessionPolicy.instructions(in: place))
    #expect(cut.utf8.count == 65_536 && !cut.contains("SECRET"))
    try Data([0x41, 0xFF, 0xFE, 0x42, 0xC3]).write(to: claude)
    #expect(SessionPolicy.instructions(in: place) == "A\u{FFFD}\u{FFFD}B\u{FFFD}")
}

/// A hard link is a regular file, but like a symbolic link it can name a file outside
/// the project; the bridge reads it outside the sandbox, so it does not count either.
@Test func instructionsRefuseAHardLinkToAFileOutsideTheProject() throws {
    let project = try temporaryFolder(), outside = try temporaryFolder()
    let secret = outside.appendingPathComponent("credentials.json")
    try Data("{\"token\":\"hunter2\"}".utf8).write(to: secret)
    try #require(link(secret.path, project.appendingPathComponent("CLAUDE.md").path) == 0)
    #expect(SessionPolicy.instructions(in: Project(name: "p", url: project)) == nil)
}

// MARK: Settings

/// However the root is written ("..", ".", a trailing slash), the secret globs, the
/// read-only rules and the cwd use its standard form, and cover secrets at any depth.
@Test func aRootWrittenOddlyStillCoversItsSecrets() throws {
    let base = try temporaryFolder()
    let real = base.appendingPathComponent("project").standardizedFileURL.path
    for written in ["\(base.path)/project/", "\(base.path)/x/../project", "\(base.path)/./project"] {
        let settings = SessionPolicy().settings(root: URL(fileURLWithPath: written), readOnly: true, denying: [])
        let denyRead = try #require(value(settings, "sandbox.filesystem.denyRead") as? [String])
        let globs = denyRead.filter { $0.contains("/**/") }
        #expect(globs.count == SessionPolicy.secretNames.count, "\(written)")
        #expect(globs.allSatisfy { $0.hasPrefix(real + "/**/") }, "\(written)")
        #expect((value(settings, "sandbox.filesystem.denyWrite") as? [String])
                == SessionPolicy.hookPaths(in: URL(fileURLWithPath: real)) + [real], "\(written)")
        for secret in [".env", "a/b/.env.local", "keys/server.pem", "deep/er/id_ed25519.pub", "x/.npmrc",
                       "config/service-account-prod.json"] {
            #expect(globs.contains { globMatches($0, real + "/" + secret) }, "\(written): \(secret)")
        }
        for harmless in ["README.md", "src/env.swift", "docs/pem.md", "a/.environment"] {
            #expect(!globs.contains { globMatches($0, real + "/" + harmless) }, "\(written): \(harmless)")
        }
    }
}

/// A project folder whose name holds glob characters ("app[v2]") still has its secrets
/// covered: the root's own glob characters are loosened in each pattern.
@Test func aRootWithGlobCharactersStillCoversItsSecrets() throws {
    let root = try temporaryFolder().appendingPathComponent("app[v2]")
    let settings = SessionPolicy().settings(root: root, readOnly: true, denying: [])
    let globs = (value(settings, "sandbox.filesystem.denyRead") as? [String] ?? []).filter { $0.contains("/**/") }
    let deny = value(settings, "permissions.deny") as? [String] ?? []
    let secret = root.standardizedFileURL.path + "/config/.env"
    #expect(globs.contains { globMatches($0, secret) })
    #expect(deny.contains { $0.hasPrefix("Read(/") && globMatches(String($0.dropFirst(6).dropLast()), secret) })
}

/// Protected folders are kept in their standard, absolute form, and are unwritable by
/// both layers in every session, the general one included.
@Test func protectedFoldersAreStandardAndAppliedEverywhere() throws {
    let base = try temporaryFolder()
    let policy = SessionPolicy(protectedFolders: ["\(base.path)/bridge/sidecar/../", "\(base.path)//bridge/./sidecar/"])
    let sidecar = base.appendingPathComponent("bridge").standardizedFileURL.path
    let other = base.appendingPathComponent("bridge/sidecar").standardizedFileURL.path
    #expect(policy.protectedFolders == [sidecar, other])
    let relative = SessionPolicy(protectedFolders: ["relative/folder"]).protectedFolders
    #expect(relative.allSatisfy { $0.hasPrefix("/") && $0.hasSuffix("/relative/folder") })
    let project = Project(name: "p", url: base.appendingPathComponent("p"))
    let general = policy.generalOptions(folder: base, projects: [project], operatorRules: "R")
    let session = policy.options(project: project, instructions: nil, operatorRules: "R")
    for options in [general, session] {
        let denyWrite = value(options, "settings.sandbox.filesystem.denyWrite") as? [String] ?? []
        let deny = value(options, "settings.permissions.deny") as? [String] ?? []
        for folder in [sidecar, other] {
            #expect(denyWrite.contains(folder))
            #expect(deny.contains("Edit(/\(folder)/**)") && deny.contains("Write(/\(folder)/**)"))
        }
    }
    // Only the general session is read-only at its root.
    #expect((value(general, "settings.sandbox.filesystem.denyWrite") as? [String])?.last == base.standardizedFileURL.path)
    #expect((value(session, "settings.sandbox.filesystem.denyWrite") as? [String])
            == [sidecar, other] + SessionPolicy.hookPaths(in: project.url))
}

/// Instructions go to Claude with their length, after the operator's rules, and the
/// general session gets the general rules and never a project's instructions.
@Test func instructionsAreAppendedAfterTheOperatorsRules() throws {
    let project = Project(name: "p", url: URL(fileURLWithPath: "/tmp/p"))
    let options = SessionPolicy().options(project: project, instructions: "Use make test.", operatorRules: "Speak.")
    #expect(value(options, "systemPrompt.append") as? String == "Speak.\n\nProject instructions (14 characters):\nUse make test.")
    #expect(value(options, "systemPrompt.preset") as? String == "claude_code")
    let none = SessionPolicy().options(project: project, instructions: nil, operatorRules: "Speak.")
    #expect(value(none, "systemPrompt.append") as? String == "Speak.")
}

// MARK: What lies beside the projects

/// Property: every entry beside the projects is covered by its pattern, whatever its
/// name holds, and no project is.
@Test(arguments: 0..<40)
func everythingBesideTheProjectsIsCoveredWhateverItsName(_ seed: Int) throws {
    var random = Seeded(seed)
    let pieces = ["a", "b", "*", "?", "[", "]", "{", "}", ",", " ", "(", ")", "!", "+", "@", "-", "~", "#", "\u{00E9}",
                  "\u{1F984}", "x.pem", "token"]
    let root = try temporaryFolder("loki-beside")
    var names = Set<String>()
    for _ in 0..<(1 + random.below(6)) {
        let name = (0..<(1 + random.below(6))).map { _ in random.pick(pieces) }.joined()
        guard !name.hasPrefix(".") else { continue }
        names.insert(name)
        if random.chance(50) {
            try Data("x".utf8).write(to: root.appendingPathComponent(name))
        } else {
            try FileManager.default.createDirectory(at: root.appendingPathComponent(name), withIntermediateDirectories: true)
        }
    }
    let project = "project-\(seed)"
    try FileManager.default.createDirectory(at: root.appendingPathComponent(project), withIntermediateDirectories: true)
    let beside = SessionPolicy.besideProjects(in: root, projects: [Project(name: project, url: root.appendingPathComponent(project))])
    #expect(beside.count == names.count, "seed \(seed): \(names.sorted())")
    for name in names {
        let path = root.appendingPathComponent(name).standardizedFileURL.path
        #expect(beside.contains { globMatches($0, path) }, "seed \(seed): \(name) is not covered")
    }
    #expect(!beside.contains { globMatches($0, root.appendingPathComponent(project).standardizedFileURL.path) }, "seed \(seed)")
}

/// A backslash in a name would escape the next character in a glob, so it is loosened
/// like the glob characters and the name's pattern still matches it.
@Test func aNameWithABackslashBesideTheProjectsIsCovered() throws {
    let root = try temporaryFolder("loki-beside")
    try Data("x".utf8).write(to: root.appendingPathComponent("token\\backup.txt"))
    let beside = SessionPolicy.besideProjects(in: root, projects: [])
    let path = root.appendingPathComponent("token\\backup.txt").standardizedFileURL.path
    #expect(beside.count == 1)
    #expect(beside.contains { globMatches($0, path) })
}
