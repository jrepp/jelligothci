import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// The conductor with several jobs at once: events interleaved across sessions, cards
// from two jobs, stops that leave other places running, a sidecar that exits, and a
// spoken queue that never talks while the user holds to talk and never drops a line.

/// Speaks by recording; `hold()` keeps each line "playing" until `release()`, and
/// `holding` is set by a test while it holds to talk, so a line begun then is caught.
final class GatedSpeaker: Speaking, @unchecked Sendable {
    private let lock = NSLock()
    private var said: [(line: String, whileHolding: Bool)] = []
    private var held = false, userHolding = false
    private var waiters: [CheckedContinuation<Void, Never>] = []
    let yields: Int

    init(yields: Int = 0) { self.yields = yields }

    func say(_ text: String) async {
        let wait = lock.withLock {
            said.append((text, userHolding))
            return held
        }
        for _ in 0..<yields { await Task.yield() }
        if wait {
            await withCheckedContinuation { resume in
                let now = lock.withLock { () -> Bool in
                    if held { waiters.append(resume) }
                    return !held
                }
                if now { resume.resume() }
            }
        }
    }

    var holding: Bool {
        get { lock.withLock { userHolding } }
        set { lock.withLock { userHolding = newValue } }
    }

    func hold() { lock.withLock { held = true } }

    func release() {
        let waiting = lock.withLock {
            held = false
            defer { waiters = [] }
            return waiters
        }
        waiting.forEach { $0.resume() }
    }

    var lines: [String] { lock.withLock { said.map(\.line) } }
    var announced: [String] { lines.filter { !isAcknowledgement($0) } }
    var spokenWhileHolding: [String] { lock.withLock { said.filter(\.whileHolding).map(\.line) } }
}

/// Polls until `condition` holds or about `seconds` pass.
func eventually(_ seconds: Double = 2, _ condition: () async -> Bool) async -> Bool {
    for _ in 0..<Int(seconds * 200) {
        if await condition() { return true }
        try? await Task.sleep(for: .milliseconds(5))
    }
    return await condition()
}

private struct Office {
    let root: URL
    let board = Board()
    let speaker: GatedSpeaker
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let conductor: Conductor

    init(heard: String = "", speaker: GatedSpeaker = GatedSpeaker()) {
        root = projectRoot(["codergotchi", "chatter", "elysium-main", "elysium-lan"])
        self.speaker = speaker
        let clock = clock
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar),
                              transcriber: FakeTranscriber(text: heard), speaker: speaker,
                              resolver: ProjectResolver(roots: [root]), clock: { clock.now })
    }

    func project(_ name: String) -> Project { Project(name: name, url: root.appendingPathComponent(name).standardizedFileURL) }

    func say(_ text: String) async {
        await conductor.route(text)
        await conductor.speakQueued()
    }

    /// The session each prompt went to, in order.
    var sessions: [String] { sidecar.of("prompt").compactMap { $0["session"] as? String } }

    var asks: [Ask] { board.messages.compactMap { if case .ask(let ask) = $0 { ask } else { nil } } }

    var states: [(state: LokiState, caption: String, progress: Int32)] {
        board.messages.compactMap { if case .state(let s, let c, let p, _) = $0 { (s, c, p) } else { nil } }
    }

    /// Waits until nothing is queued or being spoken.
    func drained() async -> Bool {
        await eventually {
            let queued = await conductor.queue.count, speaking = await conductor.speaking
            return queued == 0 && !speaking
        }
    }
}

// MARK: Jobs side by side

@Test func interleavedEventsFromTwoJobsLandOnTheirOwnJobs() async throws {
    let office = Office()
    await office.say("in codergotchi, run the tests")
    office.clock.advance(200)
    await office.say("on chatter, check the build")
    let (a, b) = (office.sessions[0], office.sessions[1])
    #expect(a != b && office.sidecar.of("start").count == 2)
    await office.conductor.handle(.progress(session: a, message: "Testing", fraction: 0.5))
    await office.conductor.handle(.tool(session: b, tool: "Bash", summary: "make"))
    await office.conductor.handle(.tool(session: b, tool: "mcp__device__speak", summary: ""))  // no caption
    await office.conductor.handle(.speak(session: b, text: "The build is clean.", mood: "happy"))
    await office.conductor.handle(.result(session: a, ok: true, text: "All pass. Yes. More.", turns: 2, cost: 0))
    await office.conductor.handle(.result(session: b, ok: false, text: "API Error: 529 {\"type\":\"overloaded\"}\nmore",
                                          turns: 2, cost: 0))
    #expect(await office.drained())
    let jobs = await office.conductor.jobs
    #expect(jobs.map(\.status) == [.done(ok: true), .done(ok: false)])
    #expect(jobs.map(\.progress) == ["Testing", "make"])
    // A failure is said as its error's first plain words, never its JSON. What Claude
    // spoke before it failed was already said as it spoke, and the failure follows.
    #expect(jobs.map(\.summary) == ["All pass. Yes.", "That didn't work: API Error: 529."])
    #expect(office.speaker.lines.prefix(2) == ["Okay, in codergotchi.", "Okay, in chatter."])
    #expect(office.speaker.announced == ["chatter: The build is clean.", "codergotchi: All pass. Yes.",
                                         "chatter: That didn't work: API Error: 529."])
    // The failed run's session is closed, so chatter's next request starts afresh.
    #expect(office.sidecar.of("close").map { $0["session"] as? String } == [b])
    let working = office.states.filter { $0.state == .working && !$0.caption.isEmpty }
    #expect(working.map(\.caption) == ["codergotchi: Testing", "chatter: make"])
    #expect(working.map(\.progress) == [500, -1])
    #expect(office.states.contains { $0.state == .failure && $0.caption == "chatter" })
}

@Test func aSecondRequestToABusyPlaceWaitsInItsSessionAndBothAreAnnounced() async {
    let office = Office()
    await office.say("in chatter, run the tests")
    await office.say("in chatter, then fix the lint")
    #expect(office.sidecar.of("start").count == 1 && Set(office.sessions).count == 1)
    let session = office.sessions[0]
    await office.say("what's running")
    #expect(office.speaker.lines.last?.hasPrefix("2 jobs are running.") == true)
    await office.conductor.handle(.speak(session: session, text: "Tests pass.", mood: "happy"))
    await office.conductor.handle(.result(session: session, ok: true, text: "", turns: 1, cost: 0))
    #expect(await office.conductor.jobs.map(\.isActive) == [false, true])
    await office.conductor.handle(.result(session: session, ok: true, text: "Lint is clean.", turns: 1, cost: 0))
    #expect(await office.drained())
    #expect(Array(office.speaker.lines.suffix(2)) == ["chatter: Tests pass.", "chatter: Lint is clean."])
}

@Test func stoppingOnePlaceLeavesTheOthersRunning() async {
    let office = Office()
    await office.say("what is a monad")  // nothing running yet: the general session
    await office.say("in codergotchi, run the tests")
    await office.say("in chatter, check the build")
    let (general, codergotchi, chatter) = (office.sessions[0], office.sessions[1], office.sessions[2])
    #expect(Set(office.sessions).count == 3)
    await office.say("stop chatter")
    #expect(office.sidecar.of("close").map { $0["session"] as? String } == [chatter])
    #expect(office.speaker.lines.last == "Stopped chatter.")
    #expect(await office.conductor.jobs.filter(\.isActive).map(\.session) == [general, codergotchi])
    // The others still report and finish.
    await office.conductor.handle(.result(session: codergotchi, ok: true, text: "Tests pass.", turns: 1, cost: 0))
    #expect(await office.drained())
    #expect(office.speaker.lines.last == "codergotchi: Tests pass.")
    await office.say("how's chatter going")
    #expect(office.speaker.lines.last == "chatter was stopped.")
    await office.say("stop everything")
    #expect(office.speaker.lines.last == "Stopped General.")
    #expect(office.sidecar.of("close").map { $0["session"] as? String } == [chatter, general])
    // A new request to a stopped place opens a new session there.
    await office.say("in chatter, try again")
    #expect(office.sidecar.of("start").count == 4 && !office.sessions.dropLast().contains(office.sessions.last!))
}

@Test func theBoardsCancelStopsOnlyTheLatestJob() async {
    let office = Office()
    await office.say("in codergotchi, run the tests")
    office.clock.advance(200)
    await office.say("in chatter, check the build")
    await office.conductor.handle(.cancel)
    #expect(office.sidecar.of("close").map { $0["session"] as? String } == [office.sessions[1]])
    #expect(office.board.messages.contains(.stopAudio))
    #expect(office.states.last?.state == .working)  // codergotchi still runs
    #expect(await office.conductor.jobs.map(\.status) == [.running, .stopped])
}

@Test func statesFromClaudeWaitWhileTheUserHoldsToTalk() async {
    let office = Office()
    await office.say("in codergotchi, run the tests")
    await office.conductor.handle(.pttStart)
    let before = office.states.count
    await office.conductor.handle(.progress(session: office.sessions[0], message: "Testing", fraction: 0.2))
    await office.conductor.handle(.tool(session: office.sessions[0], tool: "Bash", summary: "make test"))
    #expect(office.states.count == before && office.states.last?.state == .listening)
    #expect(await office.conductor.jobs.first?.progress == "make test")  // still recorded for status
    await office.conductor.handle(.connected)
    #expect(office.states.count == before)
    await office.conductor.handle(.gone)
    await office.conductor.handle(.connected)
    #expect(office.states.last?.state == .working)
}

// MARK: Cards

/// Two jobs ask at once: the board holds one card, so the second waits until the first
/// is settled, and each answer decides only its own job.
@Test func cardsFromTwoJobsAtOnceAreShownInTurnAndAnsweredSeparately() async throws {
    let office = Office()
    await office.say("in codergotchi, deploy it")
    office.clock.advance(200)
    await office.say("in chatter, read the docs")
    let (a, b) = (office.sessions[0], office.sessions[1])
    // Each card waits off the sidecar's loop, so handling the event returns at once.
    await office.conductor.handle(.permission(session: a, request: "r1", tool: "Bash",
                                              input: .object(["command": .string("make deploy")]),
                                              reason: nil, blockedPath: nil))
    #expect(await eventually { office.asks.count == 1 })
    await office.conductor.handle(.permission(session: b, request: "r1", tool: "WebFetch",
                                              input: .object(["url": .string("https://example.com/x")]),
                                              reason: nil, blockedPath: nil))
    #expect(await eventually { await office.conductor.approvals.waitingCount == 1 })
    #expect(office.asks.count == 1 && office.asks[0].detail == "make deploy\nin codergotchi")
    #expect(await office.conductor.jobs.map(\.status) == [.asking, .asking])
    // An answer with another card's nonce settles nothing.
    let cardA = office.asks[0]
    await office.conductor.handle(.answer(id: cardA.id, nonce: String(repeating: "0", count: 32), digest: cardA.digest,
                                          choice: 0))
    #expect(office.sidecar.of("decision").isEmpty)
    await office.conductor.handle(.answer(id: cardA.id, nonce: cardA.nonce, digest: cardA.digest, choice: 0))
    #expect(await eventually { office.asks.count == 2 })
    let cardB = office.asks[1]
    #expect(cardB.detail == "https://example.com/x\nin chatter")
    await office.conductor.handle(.answer(id: cardB.id, nonce: cardA.nonce, digest: cardB.digest, choice: 0))
    await office.conductor.handle(.answer(id: cardB.id, nonce: cardB.nonce, digest: cardB.digest, choice: 1))
    await office.conductor.settled()
    let decisions = office.sidecar.of("decision")
    try #require(decisions.count == 2)
    #expect(decisions.map { $0["session"] as? String } == [a, b])
    #expect(decisions.map { $0["allow"] as? Bool } == [true, false])
    #expect(decisions[0]["message"] == nil)
    #expect(decisions[1]["message"] as? String == "The user did not allow this on the Loki device.")
    #expect(decisions.allSatisfy { $0["request"] as? String == "r1" })
    #expect(await office.conductor.jobs.map(\.status) == [.running, .running])
}

@Test func aPermissionThatCannotBeShownWholeIsDeniedWithoutACard() async {
    let office = Office()
    await office.say("in codergotchi, clean up")
    let session = office.sessions[0]
    for input: JSONValue in [.object(["command": .string("echo hi\trm -rf ~")]), .object(["command": .string("")]),
                             .object([:]), .object(["command": .string(String(repeating: "x", count: 700))])] {
        await office.conductor.handle(.permission(session: session, request: "r1", tool: "Bash", input: input,
                                                  reason: nil, blockedPath: nil))
    }
    await office.conductor.settled()
    #expect(office.asks.isEmpty)
    #expect(office.sidecar.of("decision").map { $0["allow"] as? Bool } == [false, false, false, false])
    // Refused by the policy, not by the user, and Claude is told so.
    #expect(office.sidecar.of("decision").allSatisfy {
        ($0["message"] as? String)?.hasPrefix("Loki's voice policy refuses this without asking the user.") == true
    })
    #expect(await office.conductor.jobs.first?.status == .running)
}

@Test func questionsBecomeChoiceCardsInOrderAndAllMustBeAnswered() async throws {
    let office = Office()
    await office.say("in chatter, set it up")
    let session = office.sessions[0]
    let questions = [SidecarQuestion(question: "Which database?", options: ["sqlite", "postgres", "mysql", "duckdb", "x"],
                                     multiSelect: false),
                     SidecarQuestion(question: "Add tests?", options: ["Yes", "No"], multiSelect: false)]
    await office.conductor.handle(.question(session: session, request: "r7", questions: questions))
    #expect(await eventually { office.asks.count == 1 })
    let first = office.asks[0]
    #expect(first.kind == .choose && first.options == ["sqlite", "postgres", "mysql", "duckdb"])
    await office.conductor.handle(.answer(id: first.id, nonce: first.nonce, digest: first.digest, choice: 3))
    #expect(await eventually { office.asks.count == 2 })
    let second = office.asks[1]
    await office.conductor.handle(.answer(id: second.id, nonce: second.nonce, digest: second.digest, choice: 0))
    await office.conductor.settled()
    let decision = try #require(office.sidecar.of("decision").first)
    #expect(decision["allow"] as? Bool == true && decision["request"] as? String == "r7")
    #expect(decision["answers"] as? [String: String] == ["Which database?": "duckdb", "Add tests?": "Yes"])
    // A question with fewer than two options cannot be a card: nothing is answered.
    await office.conductor.handle(.question(session: session, request: "r8",
                                            questions: [SidecarQuestion(question: "Sure?", options: ["Yes"], multiSelect: false)]))
    await office.conductor.handle(.question(session: session, request: "r9", questions: []))
    await office.conductor.settled()
    let later = office.sidecar.of("decision").dropFirst()
    #expect(later.map { $0["allow"] as? Bool } == [false, false])
    #expect(later.allSatisfy { $0["message"] as? String == "The user did not answer on the Loki device." && $0["answers"] == nil })
    #expect(office.asks.count == 2)
}

@Test func anAmbiguousProjectAsksWhichAndTheWorkGoesWhereChosen() async throws {
    let office = Office()
    let routing = Task { await office.say("in elysium, run the tests") }
    #expect(await eventually { office.asks.count == 1 })
    let card = office.asks[0]
    #expect(card.title == "Which project?" && card.options == ["elysium-lan", "elysium-main"])
    await office.conductor.handle(.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 1))
    await routing.value
    let start = try #require(office.sidecar.of("start").first)
    #expect((start["options"] as? [String: Any])?["cwd"] as? String == office.project("elysium-main").url.path)
    #expect(office.sidecar.of("prompt").first?["text"] as? String == "in elysium, run the tests")
    // Unanswered (here the board leaves): nothing goes to Claude.
    let again = Task { await office.say("on elysium, build it") }
    #expect(await eventually { office.asks.count == 2 })
    await office.conductor.handle(.gone)
    await again.value
    #expect(office.sidecar.of("prompt").count == 1 && office.speaker.lines.last == "Okay, never mind.")
}

// MARK: Claude Code stopping

@Test func whenTheSidecarExitsEveryJobStopsAndOpenCardsAreDenied() async throws {
    let office = Office()
    await office.say("in codergotchi, deploy it")
    office.clock.advance(200)
    await office.say("in chatter, check the build")
    await office.conductor.handle(.permission(session: office.sessions[0], request: "r1", tool: "Bash",
                                              input: .object(["command": .string("make deploy")]),
                                              reason: nil, blockedPath: nil))
    #expect(await eventually { office.asks.count == 1 })
    await office.conductor.handle(.exited(status: 1))
    await office.conductor.settled()
    #expect(office.sidecar.of("decision").first?["allow"] as? Bool == false)
    #expect(await office.conductor.approvals.openCount == 0)
    #expect(await office.conductor.jobs.allSatisfy { $0.status == .stopped })
    #expect(office.states.contains { $0.state == .failure && $0.caption == "Claude Code stopped" })
    #expect(await office.drained())
    // The names come in the sessions dictionary's order, which varies between runs.
    #expect(["Claude Code stopped, and with it codergotchi and chatter.",
             "Claude Code stopped, and with it chatter and codergotchi."].contains(office.speaker.announced.first ?? "")
            && office.speaker.announced.count == 1)
    // With nothing running, an exit says nothing.
    let idle = Office()
    await idle.conductor.handle(.exited(status: 0))
    #expect(await idle.drained())
    #expect(idle.speaker.lines.isEmpty)
}

@Test func aSessionThatErrsThenClosesIsAnnouncedOnceAndStrayErrorsAreIgnored() async {
    let office = Office()
    await office.say("in chatter, refactor")
    let session = office.sessions[0]
    await office.conductor.handle(.error(session: nil, message: "no session s9"))
    #expect(await office.conductor.jobs.first?.isActive == true)
    await office.conductor.handle(.error(session: session, message: "API overloaded"))
    await office.conductor.handle(.closed(session: session))
    await office.conductor.handle(.closed(session: "s404"))
    #expect(await office.drained())
    #expect(office.speaker.announced == ["chatter stopped before it finished."])
    // The place gets a new session next time.
    await office.say("in chatter, try again")
    #expect(office.sidecar.of("start").count == 2 && office.sessions.last != session)
}

/// The sidecar's progress fraction becomes the board's 0 to 1000, whatever number
/// arrives. Before the fix a fraction above about 2,147 trapped the bridge, and one
/// above 1 made a state message the codec refuses.
@Test func progressFromTheSidecarIsClampedToTheBoardsRange() async {
    let office = Office()
    await office.say("in chatter, build")
    let session = office.sessions[0]
    let fractions: [Double?] = [nil, 0, 0.5, 1, 1.5, -0.2, 3e6, 1e300, -1e300]
    for fraction in fractions {
        await office.conductor.handle(.progress(session: session, message: "Building", fraction: fraction))
    }
    #expect(office.states.filter { $0.caption == "chatter: Building" }.map(\.progress)
            == [-1, 0, 500, 1000, 1000, 0, 1000, 1000, 0])
    for message in office.board.messages {
        #expect((try? LokiCodec.encode(message, seq: 1)) != nil, "\(message)")
    }
}

@Test func theSidecarsStreamEndingBecomesAnExitAfterItsEvents() async {
    let (lines, feed) = AsyncStream.makeStream(of: Data.self)
    struct LinePipe: SidecarTransport { let lines: AsyncStream<Data>; func send(_ line: Data) {} }
    let sidecar = Sidecar(transport: LinePipe(lines: lines))
    for line in [#"{"type":"ready","sdk":"x"}"#, "not json", #"{"type":"speak","session":"s1","text":"Hi."}"#,
                 #"{"type":"result","session":"s1"}"#, #"{"type":"tool"}"#, #"{"type":"error","message":"boom"}"#] {
        feed.yield(Data(line.utf8))
    }
    feed.finish()
    var events: [SidecarEvent] = []
    for await event in sidecar.events { events.append(event) }
    #expect(events == [.ready, .speak(session: "s1", text: "Hi.", mood: "neutral"),
                       .result(session: "s1", ok: false, text: "", turns: 0, cost: 0),
                       .error(session: nil, message: "boom"), .exited(status: 0)])
}

// MARK: The spoken queue

@Test func aLineBeingSpokenFinishesButTheRestWaitForTheUser() async {
    let speaker = GatedSpeaker()
    let office = Office(speaker: speaker)
    await office.say("in codergotchi, run the tests")
    office.clock.advance(200)
    await office.say("in chatter, check the build")
    let (a, b) = (office.sessions[0], office.sessions[1])
    speaker.hold()
    await office.conductor.handle(.result(session: a, ok: true, text: "Tests pass.", turns: 1, cost: 0))
    #expect(await eventually { speaker.announced == ["codergotchi: Tests pass."] })
    await office.conductor.handle(.pttStart)  // while the first line still plays
    await office.conductor.handle(.result(session: b, ok: true, text: "Build clean.", turns: 1, cost: 0))
    speaker.release()
    #expect(await eventually { let speaking = await office.conductor.speaking; return !speaking })
    try? await Task.sleep(for: .milliseconds(30))
    #expect(speaker.announced == ["codergotchi: Tests pass."])
    await office.conductor.handle(.pttEnd)  // heard nothing
    await office.conductor.settled()
    #expect(await office.drained())
    #expect(speaker.announced == ["codergotchi: Tests pass.", "Sorry, I didn't catch that.", "chatter: Build clean."])
}

/// Property: under any seeded mix of results, presses and releases, no line starts
/// while the user holds to talk, and every announcement is spoken once, in order.
@Test(arguments: 0..<24)
func announcementsAreNeverSpokenWhileHoldingAndNeverLost(_ seed: Int) async {
    var random = Seeded(seed)
    let speaker = GatedSpeaker(yields: 3)
    let office = Office(speaker: speaker)
    let places: [Place] = [.general, .project(office.project("codergotchi")), .project(office.project("chatter"))]
    for index in 0..<8 { await office.conductor.work("job \(index)", in: random.pick(places)) }
    var expected: [String] = [], presses = 0, holding = false
    for step in 0..<60 {
        switch random.below(5) {
        case 0, 1:
            let active = await office.conductor.jobs.filter(\.isActive)
            guard let session = active.isEmpty ? nil : random.pick(active).session,
                  let job = active.first(where: { $0.session == session }) else { break }
            await office.conductor.handle(.speak(session: session, text: "Line \(step).", mood: "neutral"))
            await office.conductor.handle(.result(session: session, ok: true, text: "", turns: 1, cost: 0))
            expected.append(job.place == .general ? "Line \(step)." : "\(job.place.name): Line \(step).")
        case 2 where !holding:
            await office.conductor.handle(.pttStart)
            speaker.holding = true
            holding = true
        case 3 where holding:
            speaker.holding = false
            holding = false
            if random.chance(30) {
                await office.conductor.handle(.cancel)
            } else {
                await office.conductor.handle(.pttEnd)
                presses += 1
            }
        default:
            for _ in 0..<random.below(4) { await Task.yield() }
        }
    }
    speaker.holding = false
    await office.conductor.handle(.pttStart)
    await office.conductor.handle(.pttEnd)
    presses += 1
    await office.conductor.settled()
    #expect(await office.drained(), "seed \(seed)")
    #expect(speaker.spokenWhileHolding.isEmpty, "seed \(seed): \(speaker.spokenWhileHolding)")
    let spoken = speaker.announced
    #expect(spoken.filter { $0 != "Sorry, I didn't catch that." } == expected, "seed \(seed)")
    #expect(spoken.filter { $0 == "Sorry, I didn't catch that." }.count == presses, "seed \(seed)")
}

// MARK: Nothing waits on the user or on a card

/// Announcements that arrived while the user held to talk are spoken when the
/// utterance is cancelled, as when it ends.
@Test func anAnnouncementThatWaitedForACancelledUtteranceIsStillSpoken() async {
    let office = Office()
    await office.say("in chatter, check the build")
    await office.conductor.handle(.pttStart)
    await office.conductor.handle(.result(session: office.sessions[0], ok: true, text: "Build clean.", turns: 1, cost: 0))
    try? await Task.sleep(for: .milliseconds(30))
    #expect(!office.speaker.lines.contains("chatter: Build clean."))
    await office.conductor.handle(.cancel)
    #expect(await eventually { office.speaker.lines.contains("chatter: Build clean.") })
}

/// A failed run leaves a request queued behind it in that session to run; the
/// session is closed only when nothing waits in it.
@Test func aRequestQueuedBehindAFailedOneStillRuns() async {
    let office = Office()
    await office.say("in chatter, run the tests")
    await office.say("in chatter, then fix the lint")
    let session = office.sessions[0]
    await office.conductor.handle(.result(session: session, ok: false, text: "API Error: 500", turns: 1, cost: 0))
    #expect(await office.drained())
    #expect(office.speaker.announced == ["chatter: That didn't work: API Error: 500."])
    #expect(await office.conductor.jobs.last?.isActive == true && office.sidecar.of("close").isEmpty)
    await office.conductor.handle(.result(session: session, ok: false, text: "API Error: 500", turns: 1, cost: 0))
    #expect(office.sidecar.of("close").map { $0["session"] as? String } == [session])
}

/// Mirrors Run.relayBoard, which awaits each board event in turn: "Which project?"
/// is answered through that same loop.
@Test func aChoiceCardCanBeAnsweredThroughTheBoardRelay() async throws {
    let office = Office(heard: "in elysium, run the tests")
    let (inputs, feed) = AsyncStream.makeStream(of: BoardInput.self)
    let relay = Task { for await input in inputs { await office.conductor.handle(input) } }
    feed.yield(.pttStart)
    feed.yield(.pttEnd)
    #expect(await eventually { office.asks.count == 1 })
    let card = try #require(office.asks.first)
    feed.yield(.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 0))
    #expect(await eventually { office.sidecar.of("prompt").count == 1 })
    feed.finish()
    await relay.value
    await office.conductor.settled()
}

/// Mirrors Run.relayClaude, which awaits each sidecar event in turn: while one job's
/// card waits, another job's result is still announced.
@Test func oneJobsCardDoesNotHoldBackAnotherJobsResult() async throws {
    let office = Office()
    await office.say("in codergotchi, deploy it")
    office.clock.advance(200)
    await office.say("in chatter, check the build")
    let (events, feed) = AsyncStream.makeStream(of: SidecarEvent.self)
    let relay = Task { for await event in events { await office.conductor.handle(event) } }
    feed.yield(.permission(session: office.sessions[0], request: "r1", tool: "Bash",
                           input: .object(["command": .string("make deploy")]), reason: nil, blockedPath: nil))
    #expect(await eventually { office.asks.count == 1 })
    feed.yield(.result(session: office.sessions[1], ok: true, text: "Build clean.", turns: 1, cost: 0))
    #expect(await eventually { office.speaker.lines.contains("chatter: Build clean.") })
    #expect(office.sidecar.of("decision").isEmpty)  // The card still waits.
    let card = try #require(office.asks.first)
    await office.conductor.handle(.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 1))
    feed.finish()
    await relay.value
    await office.conductor.settled()
    #expect(office.sidecar.of("decision").first?["allow"] as? Bool == false)
}
