import Foundation
import Testing
@testable import LokiSessions

private func value(_ object: [String: Any], _ path: String) -> Any? {
    path.split(separator: ".").reduce(object as Any?) { ($0 as? [String: Any])?[String($1)] }
}

@Test func theSandboxIsOnStrictAndAutoAllowing() {
    let settings = SessionPolicy().settings
    #expect(value(settings, "sandbox.enabled") as? Bool == true)
    #expect(value(settings, "sandbox.failIfUnavailable") as? Bool == true)
    #expect(value(settings, "sandbox.autoAllowBashIfSandboxed") as? Bool == true)
    #expect(value(settings, "permissions.blockReadsOutsideWorkingDirectories") as? Bool == true)
    #expect((value(settings, "permissions.ask") as? [String]) == ["Bash(dangerouslyDisableSandbox:true)"])
    #expect(value(settings, "autoMemoryEnabled") as? Bool == false)
    #expect(value(settings, "sandbox.network.strictAllowlist") as? Bool == true)
    let hosts = value(settings, "sandbox.network.allowedDomains") as? [String] ?? []
    #expect(hosts.contains("github.com") && hosts.contains("registry.npmjs.org") && !hosts.contains("*"))
    let allows = value(settings, "permissions.allow") as? [String] ?? []
    #expect(allows.contains("WebFetch(domain:github.com)") && !allows.contains { $0.hasPrefix("Bash") })
}

@Test func secretsStayUnreadable() {
    let settings = SessionPolicy().settings
    let denied = value(settings, "sandbox.filesystem.denyRead") as? [String] ?? []
    #expect(denied.contains("~/Library/Application Support/Loki Bridge"))
    #expect(denied.contains("~/Library/Application Support/Chatter"))
    let reopened = value(settings, "sandbox.filesystem.allowRead") as? [String] ?? []
    for path in reopened {
        #expect(!path.contains(".ssh") && !path.contains(".aws") && !path.contains(".config/gh") && !path.contains("Keychains")
                && !path.contains("Application Support") && path != "~" && path != "~/", "\(path)")
    }
    let variables = (value(settings, "sandbox.credentials.envVars") as? [[String: String]] ?? []).map { $0["name"] }
    #expect(variables.contains("GITHUB_TOKEN") && variables.contains("ANTHROPIC_API_KEY"))
}

@Test func sessionsLoadNothingFromDiskAndNeverBypass() throws {
    let project = Project(name: "codergotchi", url: URL(fileURLWithPath: "/tmp/codergotchi"))
    let options = SessionPolicy().options(project: project, instructions: "Use make test.", operatorRules: "Speak briefly.")
    #expect(options["permissionMode"] as? String == "acceptEdits")
    #expect((options["settingSources"] as? [String]) == [])
    #expect(options["strictMcpConfig"] as? Bool == true)
    #expect(options["cwd"] as? String == "/tmp/codergotchi")
    let append = value(options, "systemPrompt.append") as? String ?? ""
    #expect(append.contains("Speak briefly.") && append.contains("Use make test."))
    let env = options["env"] as? [String: String] ?? [:]
    #expect(env["CLAUDE_CODE_DISABLE_AUTO_MEMORY"] == "1" && env["ENABLE_CLAUDEAI_MCP_SERVERS"] == "false")
    #expect(env["ENABLE_TOOL_SEARCH"] == "false")  // The device's tools load up front: no lookup turn.
    #expect(try JSONSerialization.data(withJSONObject: options).count > 100)  // it is JSON for the sidecar
}

@Test func projectInstructionsComeFromClaudeOrAgentsFiles() throws {
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("loki-instructions-\(UUID())")
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    let project = Project(name: "p", url: folder)
    #expect(SessionPolicy.instructions(in: project) == nil)
    try Data("agents".utf8).write(to: folder.appendingPathComponent("AGENTS.md"))
    #expect(SessionPolicy.instructions(in: project) == "agents")
    try Data("claude".utf8).write(to: folder.appendingPathComponent("CLAUDE.md"))
    #expect(SessionPolicy.instructions(in: project) == "claude")
    try Data(repeating: 0x61, count: 100_000).write(to: folder.appendingPathComponent("CLAUDE.md"))
    #expect(SessionPolicy.instructions(in: project)?.count == 65_536)
}
