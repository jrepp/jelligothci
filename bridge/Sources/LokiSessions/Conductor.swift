import Foundation
import LokiProtocol

/// The board as the conductor sees it: Loki Bridge's link, or a fake in tests.
public protocol BoardLink: Sendable {
    @discardableResult func send(_ message: BridgeMessage) -> Bool
}

/// Speech in: one utterance at a time (LiveTranscriber in use).
public protocol Transcribing: Sendable {
    func begin(partial: @escaping @Sendable (String) -> Void) async throws
    func feed(_ samples: [Int16]) async
    func finish() async throws -> String
    func cancel() async
}

/// Speech out: renders text in the chosen voice and plays it on the board.
public protocol Speaking: Sendable {
    func say(_ text: String) async
    /// The user started talking or cancelled: what is left of the line being said is
    /// not sent (the board already stopped playing it).
    func interrupt() async
}

public extension Speaking {
    func interrupt() async {}
}

/// What arrives from the board, as the conductor needs it.
public enum BoardInput: Sendable {
    case connected
    case pttStart
    case audio([Int16])
    case pttEnd
    case cancel
    case answer(id: String, nonce: String, digest: String, choice: UInt32)
    /// The user picked a default model in the board's Settings (ADR-018).
    case setModel(String)
    case gone
}

/// Voice requests to Claude Code, as background jobs (RFC-003): what the user says is
/// a status, stop or repeat the bridge answers itself (Requests), or work that goes to
/// the named project's session, to the last job's session as a follow-up, or to the
/// general session. Jobs run side by side; each ends with a short spoken summary,
/// queued so it never talks over the user. Claude's permission prompts and questions
/// become cards on the board, under ADR-014's policy.
public actor Conductor {
    public static let operatorRules = """
        You are being driven by voice through Loki, a small device on the user's desk. The user \\
        cannot see your text while they talk to you. When you finish a request, call \\
        mcp__device__speak once with one or two short, plain sentences that say what you did or \\
        found: no markdown, code, file paths, or lists. Use mcp__device__progress for a short status \\
        line during long work. Leave details in your normal reply for the Mac.
        """
    /// Like the board, at most 30 seconds of an utterance are kept, whatever a board sends.
    static let maxUtteranceSamples = Int(Loki.sampleRate) * 30

    let board: BoardLink
    let sidecar: Sidecar
    let transcriber: Transcribing
    let speaker: Speaking
    let approvals: ApprovalBroker
    let resolver: ProjectResolver
    let policy: SessionPolicy
    let clock: @Sendable () -> Double
    var book = JobBook()
    var listening = false
    /// From the release until what was said is routed: the queue waits, so a "stop"
    /// drops the stopped job's words before any of them starts.
    var routing = false
    var heard = 0
    /// Spoken lines waiting their turn, and whether one is being spoken.
    var queue: [Line] = []
    var speaking = false
    /// Work started off the event loops (a request being routed, a card being
    /// answered), so a card's answer can arrive while it waits; each leaves when done.
    var running: [UUID: Task<Void, Never>] = [:]
    /// The general session is reopened after this long idle, with a fresh view of
    /// what lies beside the projects.
    static let generalIdleSeconds = 600.0
    /// When a general job last started or ended.
    var generalActive: Double?
    /// Models Ollama runs on the Mac, when the bridge has Ollama (ADR-018).
    let localModels: LocalModels?
    let choices: ModelChoiceStore
    /// The user's default model and Ollama model.
    var choice: ModelChoice
    /// The names the board was last offered: a pick must be one of them.
    var offered: [String] = []
    /// Each Ollama conversation's runs, in the order asked, and when it was last used.
    var ollamaRuns: [String: [Task<Void, Never>]] = [:]
    var ollamaActive: [String: Double] = [:]
    /// The model last warmed up, and when.
    var warmed: (model: String, at: Double)?

    public init(board: BoardLink, sidecar: Sidecar, transcriber: Transcribing, speaker: Speaking,
                resolver: ProjectResolver = ProjectResolver(), policy: SessionPolicy = SessionPolicy(),
                models: LocalModels? = nil, choices: ModelChoiceStore = MemoryModelChoiceStore(),
                clock: @escaping @Sendable () -> Double = Conductor.uptime) {
        self.board = board
        self.sidecar = sidecar
        self.transcriber = transcriber
        self.speaker = speaker
        self.resolver = resolver
        self.policy = policy
        self.clock = clock
        localModels = models
        self.choices = choices
        choice = choices.load()
        approvals = ApprovalBroker { board.send($0) }
    }

    public static let uptime: @Sendable () -> Double = { Double(DispatchTime.now().uptimeNanoseconds) / 1e9 }

    public var jobs: [Job] { book.jobs }

    // MARK: The board

    public func handle(_ input: BoardInput) async {
        switch input {
        case .connected:
            state(book.active.isEmpty ? .idle : .working)
            prepareGeneral()
            detach { await self.offerModels() }
            Task { await self.speakQueued() }  // What waited while the board was away.
        case .pttStart:
            await listen()
        case .audio(let samples):
            let kept = samples.prefix(max(0, Self.maxUtteranceSamples - heard))
            guard listening, !kept.isEmpty else { return }
            heard += kept.count
            await transcriber.feed(Array(kept))
        case .pttEnd:
            guard listening else { return }
            routing = true
            listening = false
            let text = (try? await transcriber.finish()) ?? ""
            board.send(.caption(text: LokiText.fitted(text, maxBytes: 400), final: true))
            // Off the board's loop: routing may wait on a card whose answer comes from it.
            detach {
                await self.route(text)
                await self.speakQueued()
            }
        case .cancel:
            // Still listening until the speaker is stopped, so no waiting line starts first.
            let utterance = listening
            if utterance {
                await transcriber.cancel()
            } else if let latest = book.active.last {
                stop(sessions: [latest.session])
            }
            await speaker.interrupt()
            board.send(.stopAudio)
            if utterance {
                listening = false
                detach { await self.speakQueued() }  // What waited for the utterance.
            }
            state(book.active.isEmpty ? .idle : .working)
        case .answer(let id, let nonce, let digest, let choice):
            await approvals.answer(id: id, nonce: nonce, digest: digest, choice: choice)
        case .setModel(let model):
            pick(model)
        case .gone:
            if listening { await transcriber.cancel() }
            listening = false
            await approvals.boardGone()
        }
    }

    private func listen() async {
        // Listening before the interrupt is awaited, so no queued line starts meanwhile.
        listening = true
        heard = 0
        await speaker.interrupt()
        prepareGeneral()
        warmDefault()
        state(.listening)
        let board = board, gate = CaptionGate()
        do {
            try await transcriber.begin { text in
                if gate.due() { board.send(.caption(text: LokiText.fitted(text, maxBytes: 400), final: false)) }
            }
        } catch {
            listening = false
            Task { await self.speakQueued() }  // What waited for the utterance.
        }
    }

    /// Runs work without holding up the caller; `settled` waits for all of it.
    func detach(_ work: @escaping @Sendable () async -> Void) {
        let id = UUID()
        // The task runs on this actor, so it cannot finish before it is recorded.
        running[id] = Task {
            await work()
            self.done(id)
        }
    }

    private func done(_ id: UUID) { running[id] = nil }

    /// Waits for everything started with `detach` (and anything it started).
    public func settled() async {
        while let task = running.values.first { await task.value }
    }

    // MARK: Requests

    func route(_ heard: String) async {
        // Up to the stop below nothing suspends, so the queue may go on once this
        // turn ends: during a card's wait, or after the reply put first.
        if routing {
            routing = false
            Task { await self.speakQueued() }
        }
        let text = heard.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !ProjectResolver.key(text).isEmpty else {
            reply("Sorry, I didn't catch that.")
            return
        }
        state(.heard, caption: text)
        switch Requests.classify(text, resolver: resolver) {
        case .stop(let project):
            let targets = book.active.filter { project == nil || $0.place == .project(project!) }
            guard !targets.isEmpty else { return reply("Nothing is running.") }
            stop(sessions: Set(targets.map(\.session)))
            reply("Stopped \(Self.list(Set(targets.map(\.place.name)).sorted())).")
        case .status(nil, _):
            reply(book.status(at: clock()))
        case .status(let project?, let said):
            if let line = book.status(of: .project(project), at: clock()) { return reply(line) }
            // No job there: ask Claude about it, once a near miss is confirmed.
            if !Self.namesExactly(project, said), await confirm(project) == nil { return reply("Okay, never mind.") }
            await work(said, in: .project(project))
        case .repeatLast(let project):
            reply(book.lastSummary(of: project.map(Place.project)) ?? "There's nothing to repeat yet.")
        case .work(let words):
            guard let place = await place(for: words) else { return reply("Okay, never mind.") }
            await work(words, in: place)
        case .ask(.claude, let words):
            guard !words.isEmpty else { return reply("What should I ask Claude?") }
            guard let place = await place(for: words, claude: true) else { return reply("Okay, never mind.") }
            await work(words, in: place)
        case .ask(.ollama, let words):
            guard !words.isEmpty else { return reply("What should I ask Ollama?") }
            await work(words, in: .ollama(choice.ollamaModel))
        }
    }

    /// The named project (a near miss is confirmed on the board), else, when several
    /// places are busy, the one the user picks, else the last job's place as a
    /// follow-up, else the default: general, or Ollama when the user chose an Ollama
    /// model. For `claude`, never Ollama. Nil when the user declines.
    private func place(for words: String, claude: Bool = false) async -> Place? {
        switch resolver.mentioned(in: words) {
        case .found(let project):
            if Self.namesExactly(project, words) { return .project(project) }
            return await confirm(project).map(Place.project)
        case .ambiguous(let projects):
            return await choose(projects.map(Place.project), title: "Which project?")
        case .none:
            var busy: [Place] = []
            for job in book.active.reversed() where !busy.contains(job.place) { busy.append(job.place) }
            if claude { busy.removeAll { if case .ollama = $0 { true } else { false } } }
            if busy.count > 1 { return await choose(busy, title: "For which one?") }
            if let follow = book.followUp(at: clock()) {
                if case .ollama = follow, claude { return .general }
                return follow
            }
            return claude || choice.defaultAssistant == .claude ? .general : .ollama(choice.ollamaModel)
        }
    }

    /// Whether the project's name is in the words as said, not just near them.
    static func namesExactly(_ project: Project, _ words: String) -> Bool {
        let said = Requests.words(words), name = ProjectResolver.key(project.name)
        for length in 1...3 where length <= said.count {
            for start in 0...(said.count - length) where said[start..<(start + length)].joined() == name {
                return true
            }
        }
        return false
    }

    private func confirm(_ project: Project) async -> Project? {
        let outcome = await approvals.ask(kind: .choose, title: LokiText.fitted("Did you mean \(project.name)?", maxBytes: 120),
                                          options: ["Yes", "No"])
        return outcome == .chosen(0) ? project : nil
    }

    private func choose(_ places: [Place], title: String) async -> Place? {
        let options = Array(places.prefix(4))
        guard options.count >= 2 else { return options.first }
        let outcome = await approvals.ask(kind: .choose, title: title,
                                          options: options.map { LokiText.fitted($0.name, maxBytes: 40) })
        if case .chosen(let index) = outcome, index < options.count { return options[index] }
        return nil
    }

    /// The general session ready before a request needs it: opened, or reopened when it
    /// has idled too long, as the board connects and as the user starts talking, so
    /// Claude Code starts while they speak rather than after. A session that waits costs
    /// no model call.
    func prepareGeneral() {
        guard ProjectResolver.plain(generalFolder.path) else { return }
        if let session = book.session(of: .general), book.current(in: session) == nil,
           let last = generalActive, clock() - last > Self.generalIdleSeconds {
            stop(sessions: [session])
        }
        guard let session = book.open(.general) else { return }
        generalActive = clock()
        sidecar.send(.start(session: session, options: JSONValue.from(options(for: .general)) ?? .object([:])))
    }

    /// A job in place: its session opened first if the place has none. Work in a
    /// project is acknowledged aloud, so a misheard name is caught early.
    func work(_ words: String, in place: Place) async {
        if case .ollama(let model) = place { return askOllama(words, model: model) }
        if place == .general {
            guard ProjectResolver.plain(generalFolder.path) else {
                return reply("I can't work from \(generalFolder.lastPathComponent): its name has characters the sandbox can't hold.")
            }
            if let session = book.session(of: .general), book.current(in: session) == nil,
               let last = generalActive, clock() - last > Self.generalIdleSeconds {
                stop(sessions: [session])
            }
            generalActive = clock()
        }
        let (job, newSession) = book.start(words, in: place, at: clock())
        if newSession { sidecar.send(.start(session: job.session, options: JSONValue.from(options(for: place)) ?? .object([:]))) }
        sidecar.send(.prompt(session: job.session, text: words))
        state(.thinking, caption: place.name)
        if case .project(let project) = place { reply("Okay, in \(project.name).") }
    }

    private func options(for place: Place) -> [String: Any] {
        switch place {
        case .general:
            return policy.generalOptions(folder: generalFolder, projects: resolver.projects(),
                                         operatorRules: Self.operatorRules)
        case .project(let project):
            return policy.options(project: project, instructions: SessionPolicy.instructions(in: project),
                                  operatorRules: Self.operatorRules)
        case .ollama:
            return [:]  // Ollama has no Claude Code session.
        }
    }

    /// Where the general session works: the first projects folder.
    var generalFolder: URL {
        resolver.roots.first ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("dev")
    }

    /// Closes the sessions, so their running and queued requests all end; the next
    /// request to their places opens new ones.
    func stop(sessions: Set<String>) {
        for session in sessions {
            if case .ollama? = book.place(of: session) {
                endOllama(session)
            } else {
                sidecar.send(.close(session: session))
            }
            silence(book.closed(session, at: clock()))
        }
    }

    static func list(_ names: [String]) -> String {
        switch names.count {
        case 0: ""
        case 1: names[0]
        default: names.dropLast().joined(separator: ", ") + " and " + names.last!
        }
    }

    func state(_ state: LokiState, caption: String = "", progress: Int32 = -1) {
        guard !listening || state == .listening else { return }
        board.send(.state(state, caption: LokiText.fitted(caption, maxBytes: 200), progress: progress))
    }
}

/// Lets a partial caption through at most every 200 ms.
final class CaptionGate: @unchecked Sendable {
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
