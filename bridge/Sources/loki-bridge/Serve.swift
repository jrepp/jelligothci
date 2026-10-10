import Foundation
import LokiBridgeCore
import LokiProtocol
import LokiSpeech

/// `loki-bridge serve`: runs the link and transcribes what the board hears with live
/// captions on the board. With `--echo` it says back what it heard, in the chosen
/// Chatter voice; with `--recordings` it keeps the last 20 utterances as WAVs. Lines typed on standard input
/// send messages, for trying the board before the app exists:
///   state NAME [CAPTION]   caption TEXT   ask TITLE   choose A|B|C   play FILE   say TEXT   stop
enum Serve {
    struct Options {
        var port = Loki.defaultPort
        var echo = false
        var recordings = false
        var voice = "Scar"
    }

    static func run(identity: TLSIdentity, store: PairingStore, options: Options) async throws {
        setvbuf(stdout, nil, _IOLBF, 0)  // Line by line, even into a file.
        let server = LinkServer(identity: identity, pairing: store, port: options.port) {
            .welcome(bridge: "Loki Bridge (terminal)", voice: options.voice)
        }
        server.diagnostics = { print("[link] \($0)") }
        let bound = try await server.start()
        print("Listening on port \(bound); paired boards: \(store.all.map(\.name).joined(separator: ", "))")
        if !(await LiveTranscriber.isReady()) { print("The on-device speech model is not installed: no transcripts") }
        let speaker = Speaker(server: server, voice: options.voice)
        let asks = AskBook()
        Task.detached { readCommands(server, asks, speaker) }
        let listener = Listener(server: server, keeping: options.recordings ? Recordings() : nil)
        for await event in server.events {
            switch event {
            case .connected(let board):
                print("Connected: \(board.paired.name), firmware \(board.hello.fw), board \(board.hello.board)")
                server.send(.state(.idle))
            case .message(.pttStart(let stream)):
                await listener.begin(stream)
            case .audio(let frame):
                await listener.feed(frame)
            case .message(.pttEnd(let stream, let frames, let dropped)):
                let text = await listener.end(stream: stream, frames: frames, dropped: dropped)
                if options.echo, !text.isEmpty { Task { await speaker.say("You said: \(text)") } }
            case .message(.cancel):
                await listener.cancel()
                server.send(.stopAudio)
                server.send(.state(.idle))
                print("Board: cancel")
            case .message(.answer(let id, let nonce, let digest, let choice)):
                print("Answer to \(id): choice \(choice), \(await asks.check(id: id, nonce: nonce, digest: digest, choice: choice))")
            case .message(.played(let reply, _)):
                await speaker.played(reply)
            case .message(let message):
                print("Board: \(message)")
            case .disconnected(let reason):
                await listener.cancel()
                print("Disconnected: \(reason)")
            }
        }
    }

    static func readCommands(_ server: LinkServer, _ asks: AskBook, _ speaker: Speaker) {
        while let line = readLine() {
            let words = line.split(separator: " ", maxSplits: 1).map(String.init)
            guard let verb = words.first else { continue }
            let rest = words.count > 1 ? words[1] : ""
            let message: BridgeMessage?
            switch verb {
            case "state":
                let parts = rest.split(separator: " ", maxSplits: 1).map(String.init)
                guard let state = parts.first.flatMap(LokiState.init(rawValue:)) else {
                    print("states: \(LokiState.allCases.map(\.rawValue).joined(separator: " "))")
                    continue
                }
                message = .state(state, caption: LokiText.fitted(parts.count > 1 ? parts[1] : "", maxBytes: 200))
            case "caption": message = .caption(text: LokiText.fitted(rest, maxBytes: 400), final: true)
            case "ask", "choose":
                let ask = verb == "ask"
                    ? Ask(id: AskBook.newID(), nonce: AskBook.newNonce(), kind: .approve, title: LokiText.fitted(rest, maxBytes: 120),
                          options: ["Allow", "Deny"], timeoutMs: 60_000)
                    : Ask(id: AskBook.newID(), nonce: AskBook.newNonce(), kind: .choose, title: "Which one?",
                          options: rest.split(separator: "|").prefix(4).map { LokiText.fitted(String($0), maxBytes: 40) },
                          timeoutMs: 60_000)
                Task { await asks.open(ask) }
                message = .ask(ask)
            case "stop": message = .stopAudio
            case "play":
                Task { await speaker.play(URL(fileURLWithPath: rest)) }
                continue
            case "say":
                Task { await speaker.say(rest) }
                continue
            default:
                print("commands: state NAME [CAPTION] | caption TEXT | ask TITLE | choose A|B|C | play FILE | say TEXT | stop")
                continue
            }
            if let message { print(server.send(message) ? "sent" : "not sent (no board, or refused)") }
        }
    }
}

/// One held-to-talk utterance at a time: transcribed while it arrives, with live
/// captions on the board at most five times a second, and kept as a WAV when asked.
/// Like the board, it keeps at most 30 seconds of an utterance, whatever a board sends.
actor Listener {
    static let maxSamples = Int(LokiAudio.rate) * 30
    let server: LinkServer
    let transcriber = LiveTranscriber(contextualStrings: Listener.projectNames())
    let recordings: Recordings?
    private var heard: [Int16] = []
    private var transcribing = false
    private let throttle = CaptionThrottle()

    init(server: LinkServer, keeping recordings: Recordings?) {
        self.server = server
        self.recordings = recordings
    }

    /// The names of the user's projects help the recognizer hear them.
    static func projectNames() -> [String] {
        let root = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("dev")
        let names = (try? FileManager.default.contentsOfDirectory(atPath: root.path)) ?? []
        return Array(names.filter { !$0.hasPrefix(".") && !$0.contains(".") }.prefix(100)) + ["Loki", "Claude", "Ollama"]
    }

    func begin(_ stream: UInt32) async {
        heard = []
        server.send(.state(.listening))
        let server = server, throttle = throttle
        do {
            try await transcriber.begin { text in
                if throttle.due() { server.send(.caption(text: LokiText.fitted(text, maxBytes: 400), final: false)) }
            }
            transcribing = true
        } catch {
            transcribing = false
            print("No transcript for this utterance: \(error)")
        }
        print("Listening (utterance \(stream))")
    }

    func feed(_ frame: AudioFrame) async {
        let samples = LokiAudio.samples(of: frame).prefix(max(0, Self.maxSamples - heard.count))
        guard !samples.isEmpty else { return }
        heard += samples
        if transcribing { await transcriber.feed(Array(samples)) }
    }

    func end(stream: UInt32, frames: UInt32, dropped: UInt32) async -> String {
        let text = transcribing ? ((try? await transcriber.finish()) ?? "") : ""
        transcribing = false
        let kept = heard.isEmpty ? nil : recordings?.keep(heard, stream: stream)
        server.send(.caption(text: LokiText.fitted(text, maxBytes: 400), final: true))
        server.send(.state(.heard, caption: LokiText.fitted(text, maxBytes: 200)))
        print(String(format: "Heard %.2f s (%u frames, %u dropped): \"%@\"%@", Double(heard.count) / LokiAudio.rate,
                     frames, dropped, text, kept.map { " (\($0))" } ?? ""))
        return text
    }

    func cancel() async {
        if transcribing { await transcriber.cancel() }
        transcribing = false
    }
}

/// The user's last utterances as WAVs, only with `--recordings`: in a folder of this
/// user's that no one else can read, the newest `limit` kept and older ones deleted.
struct Recordings: Sendable {
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("loki-recordings")
    let limit = 20

    /// Writes one utterance and returns its file name, or nil when it cannot be kept.
    func keep(_ samples: [Int16], stream: UInt32) -> String? {
        guard privateFolder() else {
            print("Recordings are off: \(folder.path) is not a private folder of yours")
            return nil
        }
        let file = folder.appendingPathComponent("utterance-\(Int(Date().timeIntervalSince1970))-\(stream).wav")
        guard (try? LokiAudio.writeWAV(samples, to: file)) != nil else { return nil }
        _ = chmod(file.path, 0o600)
        prune()
        return file.lastPathComponent
    }

    private func privateFolder() -> Bool {
        _ = mkdir(folder.path, 0o700)
        var info = stat()
        return lstat(folder.path, &info) == 0 && info.st_mode & S_IFMT == S_IFDIR && info.st_uid == getuid()
            && info.st_mode & 0o077 == 0
    }

    private func prune() {
        let names = ((try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? [])
            .filter { $0.hasPrefix("utterance-") && $0.hasSuffix(".wav") }
        let dated = names.compactMap { name -> (Date, String)? in
            let path = folder.appendingPathComponent(name).path
            guard let date = (try? FileManager.default.attributesOfItem(atPath: path))?[.modificationDate] as? Date else {
                return nil
            }
            return (date, path)
        }
        for (_, path) in dated.sorted(by: { $0.0 > $1.0 }).dropFirst(limit) { try? FileManager.default.removeItem(atPath: path) }
    }
}

/// Lets a partial caption through at most every 200 ms.
final class CaptionThrottle: @unchecked Sendable {
    private let lock = NSLock()
    private var last = ContinuousClock.now - .seconds(1)

    func due() -> Bool {
        lock.withLock {
            guard ContinuousClock.now - last >= .milliseconds(200) else { return false }
            last = ContinuousClock.now
            return true
        }
    }
}

/// Speaks through Chatter in the chosen voice and streams the reply to the board.
actor Speaker {
    let server: LinkServer
    let voice: String
    let chatter = ChatterClient()
    private var nextReply: UInt32 = 1
    /// One reply at a time: one asked for while another renders or plays is dropped.
    private var busy = false
    /// Set when the user talks over the line being said: its remaining pieces are not sent.
    private var interrupted = false
    /// The piece being rendered, so an interrupt stops waiting for it.
    private var rendering: Task<Data, Error>?
    /// The reply the board is still playing, waited for before the next one begins:
    /// a reply that begins makes the board drop what it still holds of the last one.
    private var playing: (reply: UInt32, done: CheckedContinuation<Void, Never>)?
    /// Replies the board said it played before anything waited for them.
    private var heardPlayed: Set<UInt32> = []

    init(server: LinkServer, voice: String) {
        self.server = server
        self.voice = voice
    }

    /// Says text a piece at a time (SpokenChunks): the next piece renders while the one
    /// before plays, so the board starts talking after the first sentence's render, not
    /// the whole reply's.
    func say(_ text: String) async {
        guard start() else { return }
        defer {
            rendering?.cancel()
            rendering = nil
            busy = false
        }
        let pieces = SpokenChunks.split(text)
        rendering = pieces.first.map(render)
        for index in pieces.indices {
            guard let current = rendering, !interrupted else { break }
            let started = ContinuousClock.now
            let wav: Data
            do {
                wav = try await current.value
            } catch {
                if interrupted { break }  // The render was given up on, not failed.
                print("Cannot say that: \(error)")
                server.send(.state(.failure, caption: LokiText.fitted("Chatter: \(error)", maxBytes: 200)))
                return
            }
            guard !interrupted else { break }
            rendering = index + 1 < pieces.count ? render(pieces[index + 1]) : nil
            // Cleaned of control characters, so Claude's words cannot drive the terminal.
            print("Chatter rendered piece \(index + 1) of \(pieces.count), \"\(LokiText.loggable(pieces[index], maxBytes: 200))\" "
                  + "(waited \(ContinuousClock.now - started))")
            await stream(wav)
        }
    }

    /// Stops the line being said: the piece playing ends where it is, and the render
    /// under way is cancelled in Chatter, so the next reply need not wait for it (an
    /// older Chatter without render mode still finishes it).
    func interrupt() {
        interrupted = true
        rendering?.cancel()
        finish(playing?.reply)
    }

    /// The board played `reply` to its end, or cut it short. Only one of the last 64
    /// sent counts, so a board cannot make the bridge keep an ever longer list.
    func played(_ reply: UInt32) {
        guard reply != 0, reply < nextReply, nextReply - reply <= 64 else { return }
        if playing?.reply == reply {
            finish(reply)
        } else {
            heardPlayed.insert(reply)
            heardPlayed = heardPlayed.filter { $0 < nextReply && nextReply - $0 <= 64 }
        }
    }

    private func finish(_ reply: UInt32?) {
        guard let reply, let waiting = playing, waiting.reply == reply else { return }
        playing = nil
        waiting.done.resume()
    }

    /// Waits until the board has played `reply`, or `limit` passes (a board that went
    /// away, or one that never says).
    private func untilPlayed(_ reply: UInt32, limit: Duration) async {
        if heardPlayed.remove(reply) != nil || interrupted { return }
        await withCheckedContinuation { done in
            playing = (reply, done)
            Task {
                try? await Task.sleep(for: limit)
                self.finish(reply)
            }
        }
    }

    private func render(_ piece: String) -> Task<Data, Error> {
        let chatter = chatter, voice = voice
        return Task { try await chatter.render(piece, voice: voice) }
    }

    private func stream(_ wav: Data) async {
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("loki-reply-\(UUID()).wav")
        defer { try? FileManager.default.removeItem(at: file) }
        guard FileManager.default.createFile(atPath: file.path, contents: wav, attributes: [.posixPermissions: 0o600])
        else { return print("Cannot say that: cannot write \(file.lastPathComponent)") }
        await stream(file, pcmOnly: true)
    }

    func play(_ file: URL) async {
        guard start() else { return }
        defer { busy = false }
        await stream(file, pcmOnly: false)
    }

    private func start() -> Bool {
        guard !busy else {
            print("A reply is already on its way; this one is dropped")
            return false
        }
        busy = true
        interrupted = false
        return true
    }

    private func stream(_ file: URL, pcmOnly: Bool) async {
        do {
            let samples = try LokiAudio.read(file, pcmOnly: pcmOnly)
            let reply = nextReply
            nextReply = nextReply == UInt32.max ? 1 : nextReply + 1
            server.send(.state(.speaking))
            let started = ContinuousClock.now, length = Double(samples.count) / LokiAudio.rate
            let sent = await ReplyStreamer.stream(samples, reply: reply, through: server) { await self.interrupted }
            print(String(format: "Reply %u (%.2f s) %@", reply, length, !sent ? "cut off" : interrupted ? "stopped" : "sent"))
            // The board plays from its own buffer after the last frame is sent: the next
            // reply waits for it, or the board would drop the end of this one.
            if sent, !interrupted {
                await untilPlayed(reply, limit: started + .milliseconds(Int(length * 1000) + 2000) - ContinuousClock.now)
            }
        } catch {
            print("Cannot play \(file.path): \(error)")
        }
    }
}

/// The questions the terminal bridge asked, so an answer is checked like the app will:
/// the same id, nonce and card digest, a choice the card offered, and only once.
actor AskBook {
    private var open: [String: Ask] = [:]

    func open(_ ask: Ask) { open[ask.id] = ask }

    func check(id: String, nonce: String, digest: String, choice: UInt32) -> String {
        guard let ask = open[id] else { return "REJECTED: no open question \(id)" }
        guard ask.nonce == nonce, ask.digest == digest, Int(choice) < ask.options.count else {
            return "REJECTED: does not match the card that was sent"
        }
        open[id] = nil
        return "accepted (\(ask.options[Int(choice)]))"
    }

    static func newID() -> String { "q" + String(UInt32.random(in: 0...UInt32.max), radix: 36) }
    static func newNonce() -> String {
        (0..<16).map { _ in String(format: "%02x", UInt8.random(in: 0...255)) }.joined()
    }
}
