import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// The general session reads any web page without a card (Michael's decision,
// 2026-10-10, ADR-014); nothing else about it changes. A project session still reads
// only the listed hosts without one, and the policy with its defaults is what it was.
// Allowing a page is the SDK's job: the bridge itself only ever shows a card or says no.

private func value(_ object: [String: Any], _ path: String) -> Any? {
    path.split(separator: ".").reduce(object as Any?) { ($0 as? [String: Any])?[String($1)] }
}

/// The JSON the sidecar receives, with sorted keys, so two settings compare exactly.
private func jsonText(_ object: Any) throws -> String {
    String(decoding: try JSONSerialization.data(withJSONObject: object, options: [.sortedKeys]), as: UTF8.self)
}

/// Settings without their allow list: what must not differ between the two web rules.
private func withoutAllow(_ settings: [String: Any]) -> [String: Any] {
    var settings = settings
    var permissions = settings["permissions"] as? [String: Any] ?? [:]
    permissions["allow"] = nil
    settings["permissions"] = permissions
    return settings
}

/// The allow list a session had before the change: WebSearch, and WebFetch per host.
private func listedHosts(_ policy: SessionPolicy) -> [String] {
    ["WebSearch"] + (SessionPolicy.developerHosts + policy.extraHosts).map { "WebFetch(domain:\($0))" }
}

private func temporaryFolder() throws -> URL {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("loki-web-\(UUID())")
    try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    return url
}

// MARK: The policy

/// With its defaults the policy is what it was before the general session read any
/// page: WebSearch and WebFetch to each developer host, in order, and no bare WebFetch.
@Test func thePolicyWithItsDefaultsStillListsTheHosts() throws {
    let settings = SessionPolicy().settings
    let allows = try #require(value(settings, "permissions.allow") as? [String])
    #expect(allows == ["WebSearch", "WebFetch(domain:github.com)", "WebFetch(domain:*.github.com)",
                       "WebFetch(domain:*.githubusercontent.com)", "WebFetch(domain:registry.npmjs.org)",
                       "WebFetch(domain:registry.yarnpkg.com)", "WebFetch(domain:pypi.org)",
                       "WebFetch(domain:files.pythonhosted.org)", "WebFetch(domain:crates.io)",
                       "WebFetch(domain:index.crates.io)", "WebFetch(domain:static.crates.io)",
                       "WebFetch(domain:proxy.golang.org)", "WebFetch(domain:sum.golang.org)",
                       "WebFetch(domain:components.espressif.com)", "WebFetch(domain:components-file.espressif.com)",
                       "WebFetch(domain:dl.espressif.com)"])
    #expect(!allows.contains("WebFetch"))
    // The default is the listed hosts, written out or not, read-only or not, with a root or not.
    let policy = SessionPolicy(extraHosts: ["example.org"])
    let root = URL(fileURLWithPath: "/tmp/loki-web-defaults")
    for (readOnly, root) in [(false, nil), (true, nil), (false, root), (true, root)] as [(Bool, URL?)] {
        let implicit = policy.settings(root: root, readOnly: readOnly, denying: ["/tmp/x"])
        let explicit = policy.settings(root: root, readOnly: readOnly, anyWebPage: false, denying: ["/tmp/x"])
        #expect(try jsonText(implicit) == jsonText(explicit))
        #expect(value(implicit, "permissions.allow") as? [String] == listedHosts(policy))
    }
    #expect(try jsonText(policy.settings) == jsonText(policy.settings(anyWebPage: false, denying: [])))
}

/// Property, seeded: whatever the extra hosts (globs, a bare "*", empty, tool-like
/// names), the protected folders and the odd names beside the projects, the general
/// session allows exactly WebSearch and bare WebFetch, never a command or a per-host
/// rule; its commands have no network; and everything but the allow list is what it
/// was before the change. A project session never gets bare WebFetch.
@Test(arguments: 0..<60)
func theGeneralSessionReadsAnyPageAndNothingElseChanges(_ seed: Int) throws {
    var random = Seeded(seed)
    let pieces = ["a", "B", "-", ".", " ", "*", "?", "[", "]", "{", "}", ",", "\\", "\u{00E9}", "\u{1F984}", "(", ")",
                  ":", "WebFetch", "domain:", "Bash", "x.pem", "token"]
    func text(_ most: Int) -> String { (0..<(1 + random.below(most))).map { _ in random.pick(pieces) }.joined() }
    let root = try temporaryFolder()
    for _ in 0..<random.below(6) {
        let name = text(6)
        guard !name.hasPrefix("."), !name.contains("/") else { continue }
        if random.chance(50) {
            try Data("x".utf8).write(to: root.appendingPathComponent(name))
        } else {
            try FileManager.default.createDirectory(at: root.appendingPathComponent(name), withIntermediateDirectories: true)
        }
    }
    let projects = (0..<(1 + random.below(3))).map { Project(name: "p\($0)", url: root.appendingPathComponent("p\($0)")) }
    for project in projects { try FileManager.default.createDirectory(at: project.url, withIntermediateDirectories: true) }
    let hostChoices = ["example.org", "*", "*.example.com", "", "WebFetch", "domain:x", "a b", "h\(seed).test"]
    let hosts = (0..<random.below(4)).map { _ in random.chance(70) ? random.pick(hostChoices) : text(5) }
    let protected = (0..<random.below(3)).map { "/tmp/guard-\(seed)-\($0)/\(text(5))" }
    let policy = SessionPolicy(extraHosts: hosts, protectedFolders: protected)
    let rules = text(10)
    let general = policy.generalOptions(folder: root, projects: projects, operatorRules: rules)
    let settings = try #require(general["settings"] as? [String: Any], "seed \(seed)")
    let allows = try #require(value(settings, "permissions.allow") as? [String], "seed \(seed)")

    #expect(allows == ["WebSearch", "WebFetch"], "seed \(seed): \(hosts)")
    #expect(!allows.contains { $0.hasPrefix("Bash") || $0.hasPrefix("WebFetch(") }, "seed \(seed)")
    #expect(value(settings, "sandbox.network.allowedDomains") as? [String] == [], "seed \(seed)")
    #expect(value(settings, "sandbox.network.deniedDomains") as? [String] == ["*"], "seed \(seed)")
    #expect(value(settings, "sandbox.network.strictAllowlist") as? Bool == true, "seed \(seed)")
    #expect(value(settings, "permissions.ask") as? [String] == ["Bash(dangerouslyDisableSandbox:true)"], "seed \(seed)")
    let deny = try #require(value(settings, "permissions.deny") as? [String], "seed \(seed)")
    #expect(!deny.isEmpty && deny.allSatisfy { ["Read(/", "Edit(/", "Write(/"].contains(where: $0.hasPrefix) }, "seed \(seed)")

    // Metamorphic: the read-only settings the general session had before the change,
    // which listed the hosts, differ from today's only in the allow list.
    let before = policy.settings(root: root, readOnly: true,
                                 denying: SessionPolicy.besideProjects(in: root, projects: projects))
    #expect(value(before, "permissions.allow") as? [String] == listedHosts(policy), "seed \(seed)")
    #expect(try jsonText(withoutAllow(settings)) == jsonText(withoutAllow(before)), "seed \(seed)")
    // Deterministic: the same inputs give the same JSON.
    #expect(try jsonText(general) == jsonText(policy.generalOptions(folder: root, projects: projects, operatorRules: rules)),
            "seed \(seed)")

    for project in projects {
        let options = policy.options(project: project, instructions: random.chance(50) ? text(20) : nil, operatorRules: "R")
        let projectAllows = try #require(value(options, "settings.permissions.allow") as? [String], "seed \(seed)")
        #expect(projectAllows == listedHosts(policy), "seed \(seed): \(hosts)")
        #expect(!projectAllows.contains("WebFetch"), "seed \(seed)")
    }
}

/// Property, seeded: for any root, read-only or not, and any denied paths, turning on
/// any web page changes the allow list to exactly WebSearch and WebFetch, and nothing
/// else: not the sandbox, the network, the ask rule or the deny rules.
@Test(arguments: 0..<40)
func anyWebPageChangesOnlyTheAllowList(_ seed: Int) throws {
    var random = Seeded(seed &+ 1000)
    let pieces = ["a", "/", "*", "[", "]", "{", "}", "\\", " ", "..", ".", "\u{00E9}"]
    func text(_ most: Int) -> String { (0..<(1 + random.below(most))).map { _ in random.pick(pieces) }.joined() }
    let policy = SessionPolicy(extraHosts: (0..<random.below(3)).map { "h\($0)\(text(3)).example" },
                               protectedFolders: (0..<random.below(3)).map { "/tmp/guard-\($0)/\(text(4))" })
    let root: URL? = random.chance(70) ? URL(fileURLWithPath: "/tmp/loki-web-\(seed)/\(text(6))") : nil
    let readOnly = random.chance(50)
    let paths = (0..<random.below(4)).map { "/tmp/beside-\($0)/\(text(5))" }
    let any = policy.settings(root: root, readOnly: readOnly, anyWebPage: true, denying: paths)
    let listed = policy.settings(root: root, readOnly: readOnly, anyWebPage: false, denying: paths)
    #expect(value(any, "permissions.allow") as? [String] == ["WebSearch", "WebFetch"], "seed \(seed)")
    #expect(value(listed, "permissions.allow") as? [String] == listedHosts(policy), "seed \(seed)")
    #expect(try jsonText(withoutAllow(any)) == jsonText(withoutAllow(listed)), "seed \(seed)")
}

// MARK: The conductor

private struct Desk {
    let root = projectRoot(["codergotchi", "chatter"])
    let board = Board()
    let speaker = FakeSpeaker()
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let conductor: Conductor

    init() {
        let clock = clock
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar), transcriber: FakeTranscriber(),
                              speaker: speaker, resolver: ProjectResolver(roots: [root]),
                              policy: SessionPolicy(extraHosts: ["example.org"]), clock: { clock.now })
    }

    func say(_ text: String) async {
        await conductor.route(text)
        await conductor.speakQueued()
    }

    var asks: [Ask] { board.messages.compactMap { if case .ask(let ask) = $0 { ask } else { nil } } }
    var starts: [[String: Any]] { sidecar.of("start") }
    var decisions: [[String: Any]] { sidecar.of("decision") }

    func general() async -> String? { await conductor.book.session(of: .general) }

    func answer(_ ask: Ask, _ choice: UInt32) async {
        await conductor.handle(.answer(id: ask.id, nonce: ask.nonce, digest: ask.digest, choice: choice))
    }

    func permission(_ session: String, _ request: String, _ input: JSONValue) async {
        await conductor.handle(.permission(session: session, request: request, tool: "WebFetch", input: input,
                                           reason: nil, blockedPath: nil))
    }
}

/// A question that names no project starts the general session with WebFetch allowed
/// for any page (as the sidecar receives it), so the SDK reads the page itself: no card
/// is shown and the bridge decides nothing. A project's session still lists the hosts.
@Test func aGeneralQuestionReadsAPageWithoutACard() async throws {
    let desk = Desk()
    await desk.say("what is the weather in Paris")
    let session = try #require(await desk.general())
    let start = try #require(desk.starts.first)
    let options = try #require(start["options"] as? [String: Any])
    #expect(options["cwd"] as? String == desk.root.path)
    #expect(value(options, "settings.permissions.allow") as? [String] == ["WebSearch", "WebFetch"])
    #expect(value(options, "settings.sandbox.network.deniedDomains") as? [String] == ["*"])
    #expect(value(options, "settings.permissions.ask") as? [String] == ["Bash(dangerouslyDisableSandbox:true)"])
    // The SDK allowed the page by its settings: the bridge hears only the tool and the answer.
    await desk.conductor.handle(.tool(session: session, tool: "WebFetch", summary: "Reading weather.example"))
    await desk.conductor.handle(.result(session: session, ok: true, text: "Sunny, 21 degrees.", turns: 2, cost: 0.01))
    #expect(await eventually { desk.speaker.lines.contains("Sunny, 21 degrees.") })
    await desk.conductor.settled()
    #expect(desk.asks.isEmpty && desk.decisions.isEmpty)
    // A project's session, started by the same conductor, keeps the listed hosts only.
    await desk.say("in chatter, read the docs")
    let project = try #require(desk.starts.last?["options"] as? [String: Any])
    let allows = try #require(value(project, "settings.permissions.allow") as? [String])
    #expect(!allows.contains("WebFetch") && allows.contains("WebFetch(domain:example.org)"))
    #expect(allows == listedHosts(SessionPolicy(extraHosts: ["example.org"])))
}

/// Should the SDK still ask about a page in the general session, the bridge never
/// allows it on its own: it shows the card and waits, and only the user's tap allows.
@Test func aWebFetchPromptFromTheGeneralSessionIsACardNeverAnAllow() async throws {
    let desk = Desk()
    await desk.say("what is the weather in Paris")
    let session = try #require(await desk.general())
    await desk.permission(session, "r1", .object(["url": .string("https://weather.example/paris"),
                                                  "prompt": .string("today's forecast")]))
    #expect(await eventually { desk.asks.count == 1 })
    let card = try #require(desk.asks.first)
    #expect(card.kind == .approve && card.title == "Read this web page?")
    #expect(card.detail == "https://weather.example/paris\nin General" && card.options == ["Allow", "Deny"])
    for _ in 0..<20 { await Task.yield() }
    #expect(desk.decisions.isEmpty)  // nothing is decided while the card waits
    #expect(await desk.conductor.jobs.first?.status == .asking)
    await desk.answer(card, 1)
    await desk.conductor.settled()
    #expect(desk.decisions.map { $0["allow"] as? Bool } == [false])
    #expect(desk.decisions.first?["message"] as? String == "The user did not allow this on the Loki device.")
    // Allowed only by the tap.
    await desk.permission(session, "r2", .object(["url": .string("https://weather.example/lyon")]))
    #expect(await eventually { desk.asks.count == 2 })
    await desk.answer(try #require(desk.asks.last), 0)
    await desk.conductor.settled()
    #expect(desk.decisions.map { $0["allow"] as? Bool } == [false, true])
    #expect(desk.decisions.map { $0["request"] as? String } == ["r1", "r2"])
    #expect(await desk.conductor.jobs.first?.status == .running)
}

/// A page that cannot be shown whole (no address, an empty one, one that hides text or
/// is too long for the card) is refused by the policy without a card.
@Test func aWebFetchPromptThatCannotBeShownIsRefusedWithoutACard() async throws {
    let desk = Desk()
    await desk.say("what is the weather in Paris")
    let session = try #require(await desk.general())
    let inputs: [JSONValue] = [.object([:]), .object(["url": .string("")]), .object(["url": .number(1)]), .null,
                               .object(["url": .string("https://a.example/\u{202E}gpj.exe")]),
                               .object(["url": .string("https://a.example/\u{200B}x")]),
                               .object(["url": .string("https://a.example/" + String(repeating: "x", count: 700))])]
    for (index, input) in inputs.enumerated() { await desk.permission(session, "r\(index)", input) }
    await desk.conductor.settled()
    #expect(desk.asks.isEmpty)
    #expect(desk.decisions.count == inputs.count)
    #expect(desk.decisions.allSatisfy {
        $0["allow"] as? Bool == false
            && ($0["message"] as? String)?.hasPrefix("Loki's voice policy refuses this without asking the user.") == true
    })
}

/// Property, seeded: whatever a WebFetch prompt from the general session holds, the
/// bridge either shows a card and decides nothing until it is answered, or refuses at
/// once; a Deny on every card leaves every decision a refusal, one per prompt.
@Test(arguments: 0..<25)
func aWebFetchPromptIsNeverAllowedByTheBridgeAlone(_ seed: Int) async throws {
    var random = Seeded(seed)
    let pieces = ["https://", "http://", "javascript:", "example.org", "/", "?q=", "&", "#", " ", "   ", "\n", "\t",
                  "\u{202E}", "\u{00E9}", "\u{1F984}", "%20", "..", String(repeating: "x", count: 150)]
    let desk = Desk()
    await desk.say("what is the weather in Paris")
    let session = try #require(await desk.general())
    let count = 1 + random.below(4)
    for request in 0..<count {
        let url = (0..<random.below(8)).map { _ in random.pick(pieces) }.joined()
        let input: JSONValue = switch random.below(10) {
        case 0: .object([:])
        case 1: .object(["url": .bool(true)])
        default: .object(["url": .string(url), "prompt": .string("read it")])
        }
        let (asked, decided) = (desk.asks.count, desk.decisions.count)
        await desk.permission(session, "r\(request)", input)
        #expect(await eventually { desk.asks.count > asked || desk.decisions.count > decided }, "seed \(seed): \(input)")
        if desk.asks.count > asked {
            #expect(desk.decisions.count == decided, "seed \(seed): decided before the card was answered: \(input)")
            await desk.answer(desk.asks[asked], 1)
        }
        await desk.conductor.settled()
        #expect(desk.decisions.count == decided + 1, "seed \(seed): \(input)")
    }
    #expect(desk.decisions.allSatisfy { $0["allow"] as? Bool == false }, "seed \(seed)")
    #expect(desk.decisions.map { $0["request"] as? String } == (0..<count).map { "r\($0)" }, "seed \(seed)")
}

/// The general session reads any page, so a page could talk Claude into asking to run a
/// command outside the sandbox, to change a file or to use any tool but its reading
/// ones: each is refused at once with no card, while a project session still gets its
/// card. Its settings also stop Claude Code from leaving the sandbox at all.
@Test func theGeneralSessionRefusesToLeaveItsSandboxWithoutACard() async throws {
    let desk = Desk()
    await desk.say("what is the price of Apple stock")
    let session = try #require(await desk.general())
    let outside = FileManager.default.temporaryDirectory.appendingPathComponent("loki-outside.txt").path
    let asks: [(String, JSONValue)] = [
        ("Bash", .object(["command": .string("curl -s https://quotes.example/AAPL"), "dangerouslyDisableSandbox": .bool(true)])),
        ("Write", .object(["file_path": .string(outside), "content": .string("x")])),
        ("Edit", .object(["file_path": .string(outside), "old_string": .string("a"), "new_string": .string("b")])),
        ("MultiEdit", .object(["file_path": .string(outside)])),
        ("NotebookEdit", .object(["notebook_path": .string(outside + ".ipynb")])),
        // Any sign of leaving counts, and a tool outside the reading ones is refused too.
        ("Bash", .object(["command": .string("curl -s https://quotes.example"), "dangerouslyDisableSandbox": .string("true")])),
        ("Bash", .object(["command": .string("curl -s https://quotes.example"), "dangerouslyDisableSandbox": .number(1)])),
        ("Monitor", .object(["command": .string("tail -f /var/log/system.log")])),
        ("EnterWorktree", .object(["name": .string("w")])),
        ("CronCreate", .object(["cron": .string("* * * * *"), "prompt": .string("x"), "durable": .bool(true)])),
    ]
    for (index, (tool, input)) in asks.enumerated() {
        await desk.conductor.handle(.permission(session: session, request: "r\(index)", tool: tool, input: input,
                                                reason: nil, blockedPath: nil))
    }
    await desk.conductor.settled()
    #expect(desk.asks.isEmpty)
    #expect(desk.decisions.map { $0["allow"] as? Bool } == Array(repeating: false, count: asks.count))
    #expect(desk.decisions.allSatisfy { $0["message"] as? String == Conductor.generalRefusal })
    #expect(await desk.conductor.jobs.first?.status == .running)  // never left waiting on a card
    let start = try #require(desk.starts.first?["options"] as? [String: Any])
    #expect(value(start, "settings.sandbox.allowUnsandboxedCommands") as? Bool == false)
    // A sandboxed command that Claude Code still asks about keeps its card.
    await desk.conductor.handle(.permission(session: session, request: "sandboxed", tool: "Bash",
                                            input: .object(["command": .string("ls"), "dangerouslyDisableSandbox": .bool(false)]),
                                            reason: nil, blockedPath: nil))
    #expect(await eventually { desk.asks.count == 1 })
    #expect(desk.asks.first?.title == "Run this command?")
    // A project session asking to leave its sandbox still gets the card.
    await desk.say("in chatter, deploy it")
    let project = try #require(desk.sidecar.of("prompt").last?["session"] as? String)
    #expect(project != session)
    await desk.answer(try #require(desk.asks.first), 1)
    await desk.conductor.handle(.permission(session: project, request: "p1", tool: "Bash",
                                            input: .object(["command": .string("git push"), "dangerouslyDisableSandbox": .bool(true)]),
                                            reason: nil, blockedPath: nil))
    #expect(await eventually { desk.asks.count == 2 })
    #expect(desk.asks.last?.title == "Run outside the sandbox?")
}
