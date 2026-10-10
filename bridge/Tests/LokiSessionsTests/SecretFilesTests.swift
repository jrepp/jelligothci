import Foundation
import Testing
@testable import LokiSessions

// Secrets stay out of reach: the general session reads only the projects (nothing
// beside them, nothing changed), and every session is denied secret-named files under
// its folder, by sandboxed commands and by Claude Code's own tools alike.

private func value(_ object: [String: Any], _ path: String) -> Any? {
    path.split(separator: ".").reduce(object as Any?) { ($0 as? [String: Any])?[String($1)] }
}

private enum Entry { case file, folder, link(to: String), dangling }

/// A folder holding these entries (names to kinds), under the temp folder.
private func folder(_ entries: [(String, Entry)]) throws -> URL {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("loki-secrets-\(UUID())")
    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    for (name, entry) in entries {
        let path = url.appendingPathComponent(name)
        switch entry {
        case .file: try Data("x".utf8).write(to: path)
        case .folder: try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true)
        case .link(let target):
            try FileManager.default.createSymbolicLink(at: path, withDestinationURL: url.appendingPathComponent(target))
        case .dangling:
            try FileManager.default.createSymbolicLink(atPath: path.path, withDestinationPath: "/nonexistent/\(UUID())")
        }
    }
    return url
}

private func path(_ name: String, in folder: URL) -> String { folder.appendingPathComponent(name).standardizedFileURL.path }

/// The secret-name globs a session rooted at `root` denies, as absolute patterns.
private func globs(under root: URL) -> [String] {
    SessionPolicy.secretNames.map { "\(root.standardizedFileURL.path)/**/\($0)" }
}

private func rules(_ paths: [String]) -> [String] { paths.flatMap { ["Read(/\($0))", "Edit(/\($0))", "Write(/\($0))"] } }

/// The rules that keep files or folders (and anything under them) unchanged.
private func unwritable(_ paths: [String]) -> [String] {
    paths.flatMap { ["Edit(/\($0))", "Write(/\($0))", "Edit(/\($0)/**)", "Write(/\($0)/**)"] }
}

private func hooks(_ root: URL) -> [String] { SessionPolicy.hookPaths(in: root) }

@Test func aMissingOrEmptyFolderHasNothingBesideItsProjectsAndStillMakesValidOptions() throws {
    let missing = FileManager.default.temporaryDirectory.appendingPathComponent("loki-missing-\(UUID())")
    #expect(SessionPolicy.besideProjects(in: missing, projects: []).isEmpty)
    #expect(SessionPolicy.besideProjects(in: try folder([]), projects: []).isEmpty)
    let options = SessionPolicy().generalOptions(folder: missing, projects: [], operatorRules: "Speak.")
    let root = missing.standardizedFileURL.path
    #expect((value(options, "settings.permissions.deny") as? [String])
            == rules(globs(under: missing)) + unwritable(hooks(missing) + [root]))
    #expect((value(options, "settings.sandbox.filesystem.denyRead") as? [String])
            == SessionPolicy.secretFolders + globs(under: missing))
    #expect((value(options, "settings.sandbox.filesystem.denyWrite") as? [String]) == hooks(missing) + [root])
    #expect(JSONValue.from(options) != nil)
}

/// The general session reads only the projects: everything beside them is unreadable
/// both ways (sandboxed commands and Claude Code's own tools), whatever characters
/// its name holds, nothing under the folder can be changed, and its commands have no
/// network, so nothing it reads can leave through an allowed host.
@Test func theGeneralSessionReadsOnlyTheProjectsAndChangesNothing() throws {
    let names = [".env", "my token (old).txt", "clé-secret.pem", "token\u{1F984}.txt", "a,b;c credential", "api_key [1].txt",
                 "tab\tsecret", "quote\"token\"", "notes.md"]
    let root = try folder(names.map { ($0, .file) } + [("project", .folder), (".hidden", .folder)])
    let project = Project(name: "project", url: root.appendingPathComponent("project"))
    let beside = SessionPolicy.besideProjects(in: root, projects: [project])
    #expect(beside.count == names.count + 1)  // every file and the hidden folder, not the project
    #expect(!beside.contains { $0.hasSuffix("/project") })
    let options = SessionPolicy().generalOptions(folder: root, projects: [project], operatorRules: "Speak.")
    let denyRead = try #require(value(options, "settings.sandbox.filesystem.denyRead") as? [String])
    let deny = try #require(value(options, "settings.permissions.deny") as? [String])
    let base = root.standardizedFileURL.path
    #expect(denyRead == SessionPolicy.secretFolders + beside + globs(under: root))
    #expect(deny == rules(beside + globs(under: root)) + unwritable(hooks(root) + [base]))
    #expect((value(options, "settings.sandbox.filesystem.denyWrite") as? [String]) == hooks(root) + [base])
    #expect((value(options, "settings.sandbox.network.allowedDomains") as? [String]) == [])
    #expect((value(options, "settings.sandbox.network.deniedDomains") as? [String]) == ["*"])
    // The rest of the policy is the same as a project's, and it is JSON for the sidecar.
    let common = SessionPolicy().settings
    for key in ["sandbox.enabled", "sandbox.failIfUnavailable", "sandbox.network.strictAllowlist",
                "permissions.blockReadsOutsideWorkingDirectories", "autoMemoryEnabled"] {
        #expect(value(options, "settings." + key) as? Bool == value(common, key) as? Bool, "\(key)")
    }
    #expect((value(options, "settings.permissions.ask") as? [String]) == ["Bash(dangerouslyDisableSandbox:true)"])
    #expect(options["cwd"] as? String == root.path && options["permissionMode"] as? String == "acceptEdits")
    #expect((options["settingSources"] as? [String]) == [] && options["strictMcpConfig"] as? Bool == true)
    let append = value(options, "systemPrompt.append") as? String ?? ""
    #expect(append.hasPrefix("Speak.") && append.contains(SessionPolicy.generalRules) && !append.contains("Project instructions"))
    let json = try #require(JSONValue.from(options))
    let data = try JSONSerialization.data(withJSONObject: json.foundation)
    let back = try #require(try JSONSerialization.jsonObject(with: data) as? [String: Any])
    #expect((value(back, "settings.permissions.deny") as? [String]) == deny)
}

@Test func projectSessionsDenySecretsUnderThemAndTheProtectedFolders() {
    let project = Project(name: "codergotchi", url: URL(fileURLWithPath: "/tmp/codergotchi"))
    let policy = SessionPolicy(extraHosts: ["example.org"], protectedFolders: ["/tmp/codergotchi/bridge"])
    let options = policy.options(project: project, instructions: nil, operatorRules: "R")
    #expect((value(options, "settings.permissions.deny") as? [String])
            == rules(globs(under: project.url)) + unwritable(["/tmp/codergotchi/bridge"] + hooks(project.url)))
    #expect((value(options, "settings.sandbox.filesystem.denyRead") as? [String])
            == SessionPolicy.secretFolders + globs(under: project.url))
    #expect((value(options, "settings.sandbox.filesystem.denyWrite") as? [String])
            == ["/tmp/codergotchi/bridge"] + hooks(project.url))
    #expect(options["maxTurns"] as? Int == SessionPolicy.maxTurns && options["maxBudgetUsd"] as? Double == SessionPolicy.maxBudgetUsd)
    #expect((value(options, "settings.sandbox.network.allowedDomains") as? [String])?.last == "example.org")
    #expect((value(options, "settings.permissions.allow") as? [String])?.contains("WebFetch(domain:example.org)") == true)
    #expect(value(options, "systemPrompt.append") as? String == "R")
}

/// Conventional credential files, whose names carry no secret word, match a denied name.
@Test(arguments: [".npmrc", ".netrc", ".pypirc", "id_rsa", "id_rsa.pub", "id_ed25519", ".pgpass", ".git-credentials",
                  ".env", ".env.production", "server.pem", "AuthKey_ABC.p8", "credentials.json",
                  "service-account-prod.json"])
func conventionalCredentialFilesAreDenied(_ name: String) {
    #expect(SessionPolicy.secretNames.contains { fnmatch($0, name, 0) == 0 }, "\(name)")
}

/// CLAUDE.md is read up to 64 KB; a cut inside a multi-byte character still leaves
/// the instructions readable.
@Test func longInstructionsCutInsideACharacterAreStillRead() throws {
    let root = try folder([])
    try (Data(repeating: 0x61, count: 65_535) + Data("é and more".utf8)).write(to: root.appendingPathComponent("CLAUDE.md"))
    let instructions = SessionPolicy.instructions(in: Project(name: "p", url: root))
    #expect(instructions?.hasPrefix("aaaa") == true)
}
