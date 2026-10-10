import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// Speaking as Claude speaks, and the general session opened before the request: what
// Claude says is spoken once, as it says it, and never again at the end; a failure is
// always said; a stopped job says nothing more; the general session is opened as the
// board connects or the user starts talking, refreshed only when idle, and never at
// the cost of a job running in it.

/// Records what is said and, on each interrupt, the board's messages so far, so the
/// order of an interrupt and the board's stopAudio can be checked.
final class InterruptRecorder: Speaking, @unchecked Sendable {
    private let lock = NSLock()
    private var said: [String] = []
    private var marks: [Int] = []
    let board: Board

    init(board: Board) { self.board = board }

    func say(_ text: String) async { lock.withLock { said.append(text) } }
    func interrupt() async {
        let count = board.messages.count
        lock.withLock { marks.append(count) }
    }

    var lines: [String] { lock.withLock { said } }
    /// The board's message count when each interrupt arrived.
    var interrupts: [Int] { lock.withLock { marks } }
}

/// A speaker written before interrupt() existed: the protocol's default is a no-op.
private struct OldSpeaker: Speaking {
    let speaker = FakeSpeaker()
    func say(_ text: String) async { await speaker.say(text) }
}

private struct Studio {
    let root: URL
    let board: Board
    let speaker: Speaking
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let conductor: Conductor

    /// `roots` defaults to one projects folder with codergotchi and chatter.
    init(heard: String = "", roots: [URL]? = nil, speaker: ((Board) -> Speaking)? = nil) {
        let roots = roots ?? [projectRoot(["codergotchi", "chatter"])]
        root = roots[0]
        let board = Board()
        self.board = board
        self.speaker = speaker?(board) ?? FakeSpeaker()
        let clock = clock
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar),
                              transcriber: FakeTranscriber(text: heard), speaker: self.speaker,
                              resolver: ProjectResolver(roots: roots), clock: { clock.now })
    }

    var fake: FakeSpeaker { speaker as! FakeSpeaker }
    var announced: [String] { fake.announced }

    func say(_ text: String) async {
        await conductor.route(text)
        await drained()
    }

    func event(_ event: SidecarEvent) async {
        await conductor.handle(event)
        await drained()
    }

    /// Speaks what waits and waits until nothing is queued or being spoken (announce
    /// speaks off a Task of its own).
    func drained() async {
        await conductor.speakQueued()  // replies wait for this; announcements start their own
        let done = await eventually {
            let queued = await conductor.queue.count, speaking = await conductor.speaking
            return queued == 0 && !speaking
        }
        #expect(done, "the spoken queue never drained")
    }

    var prompts: [String] { sidecar.of("prompt").compactMap { $0["session"] as? String } }
    var starts: [String] { sidecar.of("start").compactMap { $0["session"] as? String } }
    var closes: [String] { sidecar.of("close").compactMap { $0["session"] as? String } }
    func cwd(ofStart index: Int) -> String? {
        (sidecar.of("start")[index]["options"] as? [String: Any])?["cwd"] as? String
    }
    func path(_ name: String) -> String { root.appendingPathComponent(name).standardizedFileURL.path }
}

// MARK: Spoken once, as Claude says it

/// Two sessions interleave speak and result in every order: each job's words are said
/// exactly once, named for their place, and no result repeats them.
@Test(arguments: [[0, 1, 2, 3], [0, 2, 1, 3], [0, 2, 3, 1], [2, 0, 1, 3], [2, 0, 3, 1], [2, 3, 0, 1]])
func speakAndResultFromTwoSessionsAreEachSaidOnce(_ order: [Int]) async {
    let studio = Studio()
    await studio.say("in codergotchi, run the tests")
    studio.clock.advance(200)
    await studio.say("in chatter, check the build")
    let (a, b) = (studio.prompts[0], studio.prompts[1])
    // 0, 1: a speaks then ends; 2, 3: b speaks then ends: every interleaving of the two.
    let events: [SidecarEvent] = [.speak(session: a, text: "Tests pass.", mood: "happy"),
                                  .result(session: a, ok: true, text: "A long report.", turns: 1, cost: 0),
                                  .speak(session: b, text: "Build is clean.", mood: "happy"),
                                  .result(session: b, ok: true, text: "Another report.", turns: 1, cost: 0)]
    for index in order { await studio.event(events[index]) }
    #expect(studio.announced.sorted() == ["chatter: Build is clean.", "codergotchi: Tests pass."], "\(order)")
    #expect(Set(studio.announced).count == studio.announced.count, "\(order): said twice")
    #expect(await studio.conductor.jobs.map(\.summary) == ["Tests pass.", "Build is clean."])
    #expect(await studio.conductor.jobs.allSatisfy { $0.status == .done(ok: true) && $0.spokeAloud })
}

@Test func twoSpeaksInOneJobAreBothSaidInOrderAndTheResultAddsNothing() async {
    let studio = Studio()
    await studio.say("in chatter, check the build")
    let session = studio.prompts[0]
    await studio.event(.speak(session: session, text: "Building now.", mood: "neutral"))
    await studio.event(.speak(session: session, text: "The build is clean.", mood: "happy"))
    await studio.event(.result(session: session, ok: true, text: "Report.", turns: 2, cost: 0))
    #expect(studio.announced == ["chatter: Building now.", "chatter: The build is clean."])
    // The summary kept for status and "say that again" is the last thing Claude said.
    #expect(await studio.conductor.jobs.first?.summary == "The build is clean.")
    await studio.say("say that again")
    #expect(studio.fake.lines.last == "chatter: The build is clean.")
}

@Test func aFailureIsAlwaysSaidEvenAfterClaudeSpoke() async {
    let studio = Studio()
    await studio.say("what is a monad")  // general: no place name before its lines
    let session = studio.prompts[0]
    await studio.event(.speak(session: session, text: "A monad wraps a value.", mood: "neutral"))
    await studio.event(.result(session: session, ok: false, text: "API Error: 500 {\"x\":1}", turns: 1, cost: 0))
    #expect(studio.announced == ["A monad wraps a value.", "That didn't work: API Error: 500."])
    #expect(studio.closes == [session])  // the failed session is closed as before
    // A session that dies after Claude spoke says that too.
    await studio.say("in chatter, refactor")
    let chatter = studio.prompts[1]
    await studio.event(.speak(session: chatter, text: "Half done.", mood: "neutral"))
    await studio.event(.closed(session: chatter))
    #expect(Array(studio.announced.suffix(2)) == ["chatter: Half done.", "chatter stopped before it finished."])
}

@Test func aResultWithoutSpeakIsSaidOnceAndALateSpeakAfterItIsNot() async {
    let studio = Studio()
    await studio.say("in chatter, check the build")
    let session = studio.prompts[0]
    await studio.event(.result(session: session, ok: true, text: "It builds. Fine. More.", turns: 1, cost: 0))
    await studio.event(.speak(session: session, text: "It builds.", mood: "happy"))
    #expect(studio.announced == ["chatter: It builds. Fine."])
    // The late words do not rewrite the ended job.
    let job = await studio.conductor.jobs[0]
    #expect(job.summary == "It builds. Fine." && !job.spokeAloud)
}

/// A second request waits in the same session: the first spoke, the second did not.
/// Each is said once, and the second's result is said because it never spoke.
@Test func spokeAloudBelongsToOneJobNotToItsSession() async {
    let studio = Studio()
    await studio.say("in chatter, run the tests")
    await studio.say("in chatter, then fix the lint")
    let session = studio.prompts[0]
    #expect(Set(studio.prompts).count == 1)
    await studio.event(.speak(session: session, text: "Tests pass.", mood: "happy"))
    #expect(await studio.conductor.jobs.map(\.spokeAloud) == [true, false])
    await studio.event(.result(session: session, ok: true, text: "", turns: 1, cost: 0))
    await studio.event(.result(session: session, ok: true, text: "Lint is clean.", turns: 1, cost: 0))
    #expect(studio.announced == ["chatter: Tests pass.", "chatter: Lint is clean."])
}

@Test func aJobWaitingOnACardStillSpeaks() async throws {
    let studio = Studio()
    await studio.say("in codergotchi, deploy it")
    let session = studio.prompts[0]
    await studio.conductor.handle(.permission(session: session, request: "r1", tool: "Bash",
                                              input: .object(["command": .string("make deploy")]),
                                              reason: nil, blockedPath: nil))
    #expect(await eventually { await studio.conductor.jobs.first?.status == .asking })
    await studio.event(.speak(session: session, text: "I need your approval.", mood: "neutral"))
    #expect(studio.announced == ["codergotchi: I need your approval."])
    let ask = try #require(await studio.board.lastAsk())
    await studio.conductor.handle(.answer(id: ask.id, nonce: ask.nonce, digest: ask.digest, choice: 1))
    await studio.conductor.settled()
}

// MARK: Never for a stopped job

@Test func wordsFromAStoppedJobAreNeverSaid() async {
    let studio = Studio()
    await studio.say("in codergotchi, run the tests")
    studio.clock.advance(200)
    await studio.say("in chatter, check the build")
    let (a, b) = (studio.prompts[0], studio.prompts[1])
    await studio.say("stop chatter")
    // After the user's stop: the speak, the result and the sidecar's close say nothing.
    await studio.event(.speak(session: b, text: "The build is clean.", mood: "happy"))
    await studio.event(.result(session: b, ok: true, text: "Done.", turns: 1, cost: 0))
    await studio.event(.closed(session: b))
    // The board's cancel stops the latest job left; its words and failure say nothing.
    await studio.conductor.handle(.cancel)
    await studio.event(.speak(session: a, text: "Tests pass.", mood: "happy"))
    await studio.event(.result(session: a, ok: false, text: "Interrupted", turns: 1, cost: 0))
    await studio.event(.error(session: a, message: "closed"))
    #expect(studio.announced == ["Stopped chatter."])
    #expect(studio.closes == [b, a])
    #expect(await studio.conductor.jobs.map(\.status) == [.stopped, .stopped])
    #expect(await studio.conductor.jobs.allSatisfy { !$0.spokeAloud && $0.summary.isEmpty })
}

/// Claude spoke while the user held to talk, and what the user said was "stop chatter":
/// the job is stopped, and its words, queued before the stop, are never said. Before
/// speaking moved to the speak event nothing of a stopped job was ever said, because
/// only the result announced; this keeps it so.
@Test func wordsQueuedWhileTheUserTalksAreDroppedWhenTheyStopThatJob() async {
    let studio = Studio(heard: "stop chatter")
    await studio.say("in chatter, refactor the client")
    let session = studio.prompts[0]
    await studio.conductor.handle(.pttStart)
    await studio.conductor.handle(.speak(session: session, text: "I renamed the client.", mood: "neutral"))
    try? await Task.sleep(for: .milliseconds(50))  // nothing is said while the user talks
    #expect(studio.announced.isEmpty)
    await studio.conductor.handle(.pttEnd)
    await studio.conductor.settled()
    await studio.drained()
    #expect(studio.closes.contains(session))
    #expect(studio.announced == ["Stopped chatter."])
}

/// The board's cancel stops the latest job: its words still waiting behind another
/// line are dropped, and the other job's are not.
@Test func cancellingDropsTheStoppedJobsWaitingWordsOnly() async {
    let gate = GatedSpeaker()
    let studio = Studio(speaker: { _ in gate })
    await studio.say("in codergotchi, run the tests")
    studio.clock.advance(200)
    await studio.say("in chatter, check the build")
    let (a, b) = (studio.prompts[0], studio.prompts[1])
    gate.hold()
    await studio.conductor.handle(.speak(session: a, text: "Tests pass.", mood: "happy"))
    #expect(await eventually { gate.announced == ["codergotchi: Tests pass."] })  // being said, held
    await studio.conductor.handle(.speak(session: b, text: "Building.", mood: "neutral"))
    await studio.conductor.handle(.speak(session: b, text: "Still building.", mood: "neutral"))
    // A job's newer words replace its words still waiting.
    #expect(await studio.conductor.queue.map(\.text) == ["chatter: Still building."])
    await studio.conductor.handle(.cancel)  // stops chatter, the latest job
    #expect(studio.closes == [b])
    #expect(await studio.conductor.queue.isEmpty)
    await studio.conductor.handle(.speak(session: a, text: "And lint is clean.", mood: "happy"))
    #expect(await studio.conductor.queue.map(\.text) == ["codergotchi: And lint is clean."])
    gate.release()
    #expect(await eventually { gate.announced == ["codergotchi: Tests pass.", "codergotchi: And lint is clean."] })
}

/// A session that closes or errors, or a Claude Code that exits, drops its stopped
/// jobs' waiting words, and says that it stopped.
@Test(arguments: ["closed", "error", "exited"])
func anEndedSessionDropsItsJobsWaitingWords(_ ending: String) async {
    let gate = GatedSpeaker()
    let studio = Studio(speaker: { _ in gate })
    await studio.say("in codergotchi, run the tests")
    studio.clock.advance(200)
    await studio.say("in chatter, check the build")
    let (a, b) = (studio.prompts[0], studio.prompts[1])
    gate.hold()
    await studio.conductor.handle(.speak(session: b, text: "Building.", mood: "neutral"))
    #expect(await eventually { gate.announced == ["chatter: Building."] })
    await studio.conductor.handle(.speak(session: a, text: "Tests pass.", mood: "happy"))
    #expect(await studio.conductor.queue.map(\.text) == ["codergotchi: Tests pass."])
    switch ending {
    case "closed": await studio.conductor.handle(.closed(session: a))
    case "error": await studio.conductor.handle(.error(session: a, message: "gone"))
    default: await studio.conductor.handle(.exited(status: 1))
    }
    let queued = await studio.conductor.queue.map(\.text)
    #expect(queued.count == 1 && queued[0].contains("stopped") && !queued[0].contains("Tests pass"), "\(queued)")
    gate.release()
}

// MARK: Interrupting the line being said

@Test func everyPressAndCancelInterruptsAndNothingElseDoes() async {
    let studio = Studio(heard: "what is a monad")
    await studio.conductor.handle(.connected)
    await studio.conductor.handle(.gone)
    #expect(studio.fake.interrupts == 0)
    for press in 1...3 {
        await studio.conductor.handle(.pttStart)
        await studio.conductor.handle(.audio([1, 2, 3]))
        #expect(studio.fake.interrupts == 2 * press - 1)
        await studio.conductor.handle(.pttEnd)
        await studio.conductor.settled()
        await studio.drained()
        #expect(studio.fake.interrupts == 2 * press - 1)  // releasing does not interrupt the reply
        await studio.conductor.handle(.cancel)  // not listening: stops the latest job
        #expect(studio.fake.interrupts == 2 * press)
    }
    // Claude's events never interrupt.
    let before = studio.fake.interrupts
    await studio.event(.speak(session: "s1", text: "Hi.", mood: "neutral"))
    await studio.event(.exited(status: 0))
    #expect(studio.fake.interrupts == before)
}

@Test func aCancelInterruptsTheSpeakerBeforeTheBoardIsToldToStopAudio() async throws {
    let studio = Studio(speaker: { InterruptRecorder(board: $0) })
    let recorder = try #require(studio.speaker as? InterruptRecorder)
    await studio.conductor.handle(.cancel)
    let messages = studio.board.messages
    let stop = try #require(messages.firstIndex(of: .stopAudio))
    // Interrupted with fewer messages sent than the index of stopAudio: before it.
    #expect(recorder.interrupts == [stop])
    // A press interrupts before the board hears it is listening.
    await studio.conductor.handle(.pttStart)
    let listening = try #require(studio.board.messages.lastIndex { if case .state(.listening, _, _, _) = $0 { true } else { false } })
    #expect(recorder.interrupts.count == 2 && recorder.interrupts[1] <= listening)
}

@Test func aSpeakerWithoutInterruptStillWorks() async {
    let studio = Studio(speaker: { _ in OldSpeaker() })
    await studio.conductor.handle(.pttStart)
    await studio.conductor.handle(.cancel)
    await studio.conductor.route("in chatter, check the build")
    await studio.conductor.speakQueued()
    #expect((studio.speaker as! OldSpeaker).speaker.lines == ["Okay, in chatter."])
    #expect((studio.speaker as! OldSpeaker).speaker.interrupts == 0)
}

// MARK: The general session, opened ahead

@Test func repeatedConnectsAndPressesOpenTheGeneralSessionOnce() async throws {
    let studio = Studio()
    for _ in 0..<5 {
        await studio.conductor.handle(.connected)
        await studio.conductor.handle(.pttStart)
        await studio.conductor.handle(.cancel)
        studio.clock.advance(Conductor.generalIdleSeconds / 10)
    }
    #expect(studio.starts.count == 1 && studio.closes.isEmpty && studio.prompts.isEmpty)
    #expect(studio.cwd(ofStart: 0) == studio.root.path)
    // Opened with the general session's own options, tool search off like every session.
    let options = try #require(studio.sidecar.of("start").first?["options"] as? [String: Any])
    #expect((options["env"] as? [String: String])?["ENABLE_TOOL_SEARCH"] == "false")
    #expect(options["systemPrompt"].map { "\($0)" }?.contains("general session") == true)
    let general = await studio.conductor.book.session(of: .general)
    #expect(general == studio.starts[0])
    // The general session waits: nothing runs, cancel stops nothing, and stop says so.
    #expect(await studio.conductor.jobs.isEmpty)
    await studio.say("stop")
    #expect(studio.fake.lines.last == "Nothing is running." && studio.closes.isEmpty)
    await studio.say("what's running")
    #expect(studio.fake.lines.last == "Nothing is running.")
}

@Test func theIdleLimitIsStrictlyLongerThanGeneralIdleSeconds() async {
    let studio = Studio()
    await studio.conductor.handle(.connected)
    studio.clock.advance(Conductor.generalIdleSeconds)
    await studio.conductor.handle(.pttStart)
    #expect(studio.closes.isEmpty && studio.starts.count == 1)
    await studio.conductor.handle(.cancel)
    studio.clock.advance(0.001)
    await studio.conductor.handle(.connected)  // a reconnect refreshes too
    #expect(studio.closes == [studio.starts[0]] && studio.starts.count == 2)
    // The refreshed session is fresh again: no second refresh straight away.
    await studio.conductor.handle(.pttStart)
    #expect(studio.starts.count == 2)
}

@Test func aGeneralJobRunningIsNeverClosedForBeingIdle() async {
    let studio = Studio()
    await studio.say("what is a monad")
    let session = studio.prompts[0]
    #expect(studio.starts == [session])
    studio.clock.advance(Conductor.generalIdleSeconds * 3)
    await studio.conductor.handle(.connected)
    await studio.conductor.handle(.pttStart)
    await studio.conductor.handle(.cancel)  // listening: only the utterance is cancelled
    await studio.conductor.handle(.connected)
    #expect(studio.closes.isEmpty && studio.starts.count == 1)
    #expect(await studio.conductor.jobs.first?.isActive == true)
    // Its result marks the session active: still not idle just short of the limit after.
    await studio.event(.result(session: session, ok: true, text: "A monad wraps.", turns: 1, cost: 0))
    studio.clock.advance(Conductor.generalIdleSeconds - 1)
    await studio.conductor.handle(.pttStart)
    #expect(studio.closes.isEmpty && studio.starts.count == 1)
    await studio.conductor.handle(.cancel)
    studio.clock.advance(2)
    await studio.conductor.handle(.pttStart)
    #expect(studio.closes == [session] && studio.starts.count == 2 && studio.starts[1] != session)
}

/// A general job waiting on a card is active too: refreshing would lose the card.
@Test func aGeneralJobWaitingOnACardIsNotClosedEither() async throws {
    let studio = Studio()
    await studio.say("what is a monad")
    let session = studio.prompts[0]
    await studio.conductor.handle(.permission(session: session, request: "r1", tool: "Bash",
                                              input: .object(["command": .string("make deploy")]), reason: nil,
                                              blockedPath: nil))
    #expect(await eventually { await studio.conductor.jobs.first?.status == .asking })
    studio.clock.advance(Conductor.generalIdleSeconds * 2)
    await studio.conductor.handle(.connected)
    #expect(studio.closes.isEmpty && studio.starts.count == 1)
    let ask = try #require(await studio.board.lastAsk())
    await studio.conductor.handle(.answer(id: ask.id, nonce: ask.nonce, digest: ask.digest, choice: 1))
    await studio.conductor.settled()
}

@Test func aRefreshedSessionsLateCloseSaysNothing() async throws {
    let studio = Studio()
    await studio.conductor.handle(.connected)
    let first = try #require(studio.starts.first)
    studio.clock.advance(Conductor.generalIdleSeconds + 1)
    await studio.conductor.handle(.connected)
    await studio.event(.closed(session: first))
    await studio.event(.error(session: first, message: "gone"))
    #expect(studio.fake.lines.isEmpty)
    let refreshed = try #require(studio.starts.dropFirst().first)
    #expect(await studio.conductor.book.session(of: .general) == refreshed)
}

@Test func anUnusableGeneralFolderOpensNothingAndProjectsStillWork() async throws {
    let odd = FileManager.default.temporaryDirectory.appendingPathComponent("loki-[general]-\(UUID())")
    try FileManager.default.createDirectory(at: odd, withIntermediateDirectories: true)
    let projects = projectRoot(["chatter"])
    let studio = Studio(heard: "in chatter, run the tests", roots: [odd, projects])
    #expect(!ProjectResolver.plain(studio.root.path))
    for _ in 0..<3 {
        await studio.conductor.handle(.connected)
        await studio.conductor.handle(.pttStart)
        await studio.conductor.handle(.cancel)
    }
    #expect(studio.sidecar.commands.isEmpty)
    #expect(await studio.conductor.book.session(of: .general) == nil)
    // A project request still opens its own session.
    await studio.conductor.handle(.pttStart)
    await studio.conductor.handle(.pttEnd)
    await studio.conductor.settled()
    #expect(studio.starts.count == 1 && studio.prompts == studio.starts)
    let cwd = (studio.sidecar.of("start")[0]["options"] as? [String: Any])?["cwd"] as? String
    #expect(cwd == projects.appendingPathComponent("chatter").standardizedFileURL.path)
    // A general request is refused aloud, and nothing is sent for it.
    studio.clock.advance(JobBook.followUpSeconds + 1)
    await studio.conductor.handle(.result(session: studio.prompts[0], ok: true, text: "Done.", turns: 1, cost: 0))
    studio.clock.advance(JobBook.followUpSeconds + 1)
    await studio.say("what is a monad")
    #expect(studio.fake.lines.last?.hasPrefix("I can't work from loki-[general]-") == true)
    #expect(studio.starts.count == 1 && studio.prompts.count == 1)
}

/// The general session opened as the user pressed waits while the request they made
/// goes to a project, in a session of its own with that project's options.
@Test func aProjectRequestAfterAPressGetsItsOwnSessionWhileTheGeneralOneWaits() async throws {
    let studio = Studio(heard: "in chatter, run the tests")
    await studio.conductor.handle(.pttStart)
    let general = try #require(studio.starts.first)
    await studio.conductor.handle(.pttEnd)
    await studio.conductor.settled()
    #expect(studio.starts.count == 2 && studio.starts[1] != general)
    #expect(studio.prompts == [studio.starts[1]])
    #expect(studio.cwd(ofStart: 0) == studio.root.path)
    #expect(studio.cwd(ofStart: 1) == studio.path("chatter"))
    let prompt = try #require((studio.sidecar.of("start")[1]["options"] as? [String: Any])?["systemPrompt"]
        .map { "\($0)" })
    #expect(!prompt.contains("general session"))
    #expect(studio.closes.isEmpty)
    #expect(await studio.conductor.book.session(of: .general) == general)
    // Once chatter is done and past its follow-up window, a general request uses the
    // session that waited.
    await studio.event(.result(session: studio.prompts[0], ok: true, text: "Pass.", turns: 1, cost: 0))
    studio.clock.advance(JobBook.followUpSeconds + 1)
    await studio.say("what is a monad")
    #expect(studio.prompts.last == general && studio.starts.count == 2)
}

@Test func afterClaudeCodeExitsTheNextPressOpensANewGeneralSession() async {
    let studio = Studio()
    await studio.conductor.handle(.connected)
    await studio.event(.exited(status: 1))
    #expect(studio.fake.lines.isEmpty)  // nothing was running, so nothing is said
    #expect(await studio.conductor.book.session(of: .general) == nil)
    await studio.conductor.handle(.pttStart)
    #expect(studio.starts.count == 2 && studio.starts[0] != studio.starts[1])
}

// MARK: JobBook.open

@Test func openReservesASessionAheadOfItsFirstJob() {
    var book = JobBook()
    let chatter = Place.project(Project(name: "chatter", url: URL(fileURLWithPath: "/dev/chatter")))
    let general = book.open(.general)
    #expect(general == "s1" && book.open(.general) == nil)
    #expect(book.session(of: .general) == "s1" && book.place(of: "s1") == .general)
    #expect(book.current(in: "s1") == nil && book.active.isEmpty && book.jobs.isEmpty)
    #expect(book.status(at: 0) == "Nothing is running." && book.followUp(at: 0) == nil)
    // Sessions number on from each other, whichever way they are opened.
    let started = book.start("x", in: chatter, at: 0)
    #expect(started.newSession && started.job.session == "s2")
    #expect(book.open(chatter) == nil)  // a place with a session gets none
    let first = book.start("what", in: .general, at: 1)
    #expect(!first.newSession && first.job.session == "s1")
    // Closing an opened session with no job stops nothing; the next open is new.
    #expect(book.closed("s1", at: 2).map(\.id) == [first.job.id])
    #expect(book.open(.general) == "s3")
    #expect(book.closed("s3", at: 3).isEmpty)
    #expect(book.session(of: .general) == nil && book.open(.general) == "s4")
    _ = book.closedAll(at: 4)
    #expect(book.open(chatter) == "s5" && book.open(.general) == "s6")
}

@Test func spokeMarksTheCurrentJobOnlyAndAFailureKeepsTheMark() {
    var book = JobBook()
    let a = book.start("a", in: .general, at: 0).job
    _ = book.start("b", in: .general, at: 1)
    #expect(book.spoke("x", in: "s404") == nil)
    let spoken = book.spoke("First.", in: a.session)
    #expect(spoken?.id == a.id && spoken?.spokeAloud == true && spoken?.summary == "First.")
    #expect(book.spoke("Second.", in: a.session)?.id == a.id)  // still the oldest active
    #expect(book.jobs.map(\.spokeAloud) == [true, false])
    let failed = book.finished(ok: false, result: "Boom", in: a.session, at: 2)
    #expect(failed?.summary == "That didn't work: Boom." && failed?.spokeAloud == true)
    // Now the second job is current; asking still counts as running.
    book.asking(true, in: a.session)
    #expect(book.spoke("Third.", in: a.session)?.id == a.id + 1)
    _ = book.finished(ok: true, result: "", in: a.session, at: 3)
    #expect(book.spoke("Late.", in: a.session) == nil)
    #expect(book.jobs.map(\.summary) == ["That didn't work: Boom.", "Third."])
}
