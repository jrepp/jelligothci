import Foundation
import LokiBridgeCore
import LokiProtocol
import LokiSessions
import LokiSpeech

extension LinkServer: BoardLink {}
extension LiveTranscriber: Transcribing {}
extension Speaker: Speaking {}

/// `loki-bridge run`: Loki for real (RFC-003). What the board hears is transcribed on
/// the Mac and handed to the conductor, which answers status, stop and repeat itself
/// and runs everything else as Claude Code jobs through the Agent SDK sidecar, side by
/// side, each ending in a short spoken summary in the chosen Chatter voice.
enum Run {
    struct Options {
        var port = Loki.defaultPort
        var voice = "Scar"
        var node: String?
        var sidecar: String?
        var claude: String?
    }

    /// bridge/sidecar/src/main.ts in this checkout.
    static let defaultSidecar = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../sidecar/src/main.ts").standardizedFileURL
    /// The oldest Claude Code that knows every setting SessionPolicy relies on, and the
    /// one its enforcement was probed on (ADR-014); an older one could ignore some of
    /// them and leave the session less contained.
    static let oldestClaude = [2, 1, 296]
    /// What in the checkout around the bridge runs outside the sandbox later without
    /// anyone reading it first: its commit hooks and the tooling they and `make` run.
    static let checkoutTooling = [".githooks", "scripts", "Makefile", "toolchain.env", ".pre-commit-config.yaml",
                                  ".tools"]

    static func run(identity: TLSIdentity, store: PairingStore, options: Options) async throws {
        setvbuf(stdout, nil, _IOLBF, 0)
        signal(SIGPIPE, SIG_IGN)  // A sidecar that died fails its next write, not the bridge.
        let roots = ProjectResolver().roots
        let node = try findNode(options.node, outside: roots)
        let script = options.sidecar.map { URL(fileURLWithPath: $0) } ?? defaultSidecar
        guard FileManager.default.fileExists(atPath: script.path) else { throw RunError("no sidecar at \(script.path)") }
        // The sidecar's package (its code, packages and Claude Code), the bridge, and
        // the checkout's hooks and tooling run outside the sandbox, so no session may
        // change them.
        let package = script.deletingLastPathComponent().deletingLastPathComponent().standardizedFileURL
        let bridge = package.deletingLastPathComponent(), checkout = bridge.deletingLastPathComponent()
        let protected = [package.path, bridge.path] + checkoutTooling.map { checkout.appendingPathComponent($0).path }
        var environment = SessionEnvironment.filtered(ProcessInfo.processInfo.environment, outside: roots)
        if let claude = options.claude {
            try checkClaude(claude)
            environment["LOKI_CLAUDE_PATH"] = claude
        }
        let process = try SidecarProcess(node: node, script: script, environment: environment)
        let sidecar = Sidecar(transport: process)
        let server = LinkServer(identity: identity, pairing: store, port: options.port) {
            .welcome(bridge: "Loki Bridge", voice: options.voice)
        }
        server.diagnostics = { print("[link] \($0)") }
        let bound = try await server.start()
        print("Loki is listening on port \(bound) with Claude Code \(options.claude ?? "(the SDK's own)"), node \(node.path)")
        if !(await LiveTranscriber.isReady()) { print("The on-device speech model is not installed: no transcripts") }
        let speaker = Speaker(server: server, voice: options.voice)
        let conductor = Conductor(board: server, sidecar: sidecar,
                                  transcriber: LoggedTranscriber(LiveTranscriber(contextualStrings: Listener.projectNames())),
                                  speaker: speaker,
                                  policy: SessionPolicy(protectedFolders: protected),
                                  models: OllamaRunner(), choices: FileModelChoiceStore(file: BridgeFiles.models))
        await withTaskGroup(of: Void.self) { group in
            group.addTask { await relayClaude(sidecar.events, to: conductor) }
            group.addTask { await relayBoard(server.events, to: conductor, speaker: speaker) }
            await group.next()
            group.cancelAll()
        }
        process.stop()
        server.stop()
    }

    static func relayBoard(_ events: AsyncStream<LinkEvent>, to conductor: Conductor, speaker: Speaker) async {
        for await event in events {
            switch event {
            case .connected(let board):
                print("Connected: \(board.paired.name), firmware \(board.hello.fw)")
                await conductor.handle(.connected)
            case .message(.pttStart): await conductor.handle(.pttStart)
            case .audio(let frame): await conductor.handle(.audio(LokiAudio.samples(of: frame)))
            case .message(.pttEnd): await conductor.handle(.pttEnd)
            case .message(.cancel): await conductor.handle(.cancel)
            case .message(.answer(let id, let nonce, let digest, let choice)):
                await conductor.handle(.answer(id: id, nonce: nonce, digest: digest, choice: choice))
            case .message(.played(let reply, _)):
                await speaker.played(reply)
            case .message(.setModel(let model)):
                print("Default model: \(model)")
                await conductor.handle(.setModel(model))
            case .message: break
            case .disconnected(let reason):
                print("Disconnected: \(reason)")
                await conductor.handle(.gone)
            }
        }
    }

    static func relayClaude(_ events: AsyncStream<SidecarEvent>, to conductor: Conductor) async {
        for await event in events {
            log(event)
            await conductor.handle(event)
            if case .exited(let status) = event {
                print("Claude Code's sidecar ended (status \(status)); restart loki-bridge run")
                return
            }
        }
    }

    /// One line per event worth seeing in the terminal; never tool inputs in full.
    /// Claude's text is cleaned of control characters first, so it cannot drive the
    /// terminal with escape sequences.
    static func log(_ event: SidecarEvent) {
        func clean(_ text: String) -> String { LokiText.loggable(text, maxBytes: 600) }
        switch event {
        case .ready: print("[claude] ready")
        case .tool(let session, let tool, let summary): print("[\(session)] \(clean(tool)): \(clean(summary))")
        case .progress(let session, let message, _): print("[\(session)] progress: \(clean(message))")
        case .permission(let session, _, let tool, _, _, _):
            print("[\(session)] asks to use \(clean(tool)) (card on the board)")
        case .question(let session, _, _): print("[\(session)] has a question (card on the board)")
        case .speak(let session, let text, _): print("[\(session)] says: \(clean(text))")
        case .result(let session, let ok, _, let turns, let cost):
            print(String(format: "[%@] %@ after %d turns ($%.3f)", session, ok ? "done" : "failed", turns, cost))
        case .error(let session, let message): print("[\(session ?? "claude")] error: \(clean(message))")
        case .closed(let session): print("[\(session)] closed")
        case .state, .exited: break
        }
    }

    /// The first executable of that name on the fenced PATH: no relative entries and
    /// none inside the given folders (a session could have put one there).
    static func onPath(_ name: String, outside folders: [URL] = []) -> String? {
        let fenced = folders.map { $0.resolvingSymlinksInPath().standardizedFileURL.path + "/" }
        return SessionEnvironment.fencedPath(ProcessInfo.processInfo.environment["PATH"] ?? "", outside: folders)
            .split(separator: ":").map { "\($0)/\(name)" }
            .first { path in
                let real = URL(fileURLWithPath: path).resolvingSymlinksInPath().path
                return FileManager.default.isExecutableFile(atPath: path) && !fenced.contains { real.hasPrefix($0) }
            }
    }

    /// A Claude Code given with --claude must be new enough for every setting the
    /// policy relies on.
    static func checkClaude(_ path: String) throws {
        let version = try probe(path, "--version")
        let numbers = version.split(whereSeparator: { !$0.isNumber && $0 != "." }).first?
            .split(separator: ".").compactMap { Int($0) } ?? []
        guard numbers.count >= 3, numbers.lexicographicallyPrecedes(oldestClaude) == false else {
            throw RunError("Claude Code \(version) at \(path) is older than "
                           + "\(oldestClaude.map(String.init).joined(separator: ".")); leave out --claude to use the SDK's own")
        }
    }

    static func probe(_ path: String, _ argument: String) throws -> String {
        let probe = Process()
        probe.executableURL = URL(fileURLWithPath: path)
        probe.arguments = [argument]
        let output = Pipe()
        probe.standardOutput = output
        try probe.run()
        probe.waitUntilExit()
        return String(decoding: output.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    /// node 24 or later (the sidecar runs TypeScript by type stripping): the one given,
    /// else the first on PATH outside the project folders.
    static func findNode(_ given: String?, outside folders: [URL]) throws -> URL {
        guard let path = given ?? onPath("node", outside: folders), FileManager.default.isExecutableFile(atPath: path) else {
            throw RunError("node is not on PATH; install Node 24 or pass --node PATH")
        }
        let version = try probe(path, "--version")
        guard let major = Int(version.dropFirst().prefix { $0.isNumber }), major >= 24 else {
            throw RunError("node \(version.trimmingCharacters(in: .whitespacesAndNewlines)) is too old; Loki needs 24 or later")
        }
        return URL(fileURLWithPath: path)
    }
}

struct RunError: Error, CustomStringConvertible {
    let description: String
    init(_ description: String) { self.description = description }
}

/// The transcriber, with each utterance's text printed when recognition ends, cleaned
/// for the terminal, so a misheard request can be seen.
struct LoggedTranscriber: Transcribing {
    let inner: LiveTranscriber
    init(_ inner: LiveTranscriber) { self.inner = inner }
    func begin(partial: @escaping @Sendable (String) -> Void) async throws { try await inner.begin(partial: partial) }
    func feed(_ samples: [Int16]) async { await inner.feed(samples) }
    func cancel() async { await inner.cancel() }
    func finish() async throws -> String {
        let text = try await inner.finish()
        print("Heard: \"\(LokiText.loggable(text, maxBytes: 300))\"")
        return text
    }
}
