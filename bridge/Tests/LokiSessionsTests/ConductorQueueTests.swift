import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// The spoken queue as jobs stop and the user talks: a stop drops only the stopped job's
// words still waiting, never a line being said, a reply, an announcement or another
// job's words; a job's newer words replace only its own; what waited while the user held
// is said after the reply to what they said; and no line starts while a press is still
// interrupting the speaker.

/// Lets callers of `wait()` through once opened, and at once while open.
final class Latch: @unchecked Sendable {
    private let lock = NSLock()
    private var isOpen: Bool
    private var waiters: [CheckedContinuation<Void, Never>] = []

    init(open: Bool) { isOpen = open }

    func close() { lock.withLock { isOpen = false } }

    func open() {
        let waiting = lock.withLock {
            isOpen = true
            defer { waiters = [] }
            return waiters
        }
        waiting.forEach { $0.resume() }
    }

    func wait() async {
        await withCheckedContinuation { resume in
            let now = lock.withLock { () -> Bool in
                if !isOpen { waiters.append(resume) }
                return isOpen
            }
            if now { resume.resume() }
        }
    }
}

/// A speaker whose lines play until `playing` opens and whose interrupt() suspends until
/// `stopping` opens; each line records whether an interrupt was in flight as it began.
final class SuspendingSpeaker: Speaking, @unchecked Sendable {
    let playing = Latch(open: true), stopping = Latch(open: true)
    private let lock = NSLock()
    private var said: [(line: String, duringInterrupt: Bool)] = []
    private var inFlight = 0

    func say(_ text: String) async {
        lock.withLock { said.append((text, inFlight > 0)) }
        await playing.wait()
    }

    func interrupt() async {
        lock.withLock { inFlight += 1 }
        await stopping.wait()
        lock.withLock { inFlight -= 1 }
    }

    var interrupting: Bool { lock.withLock { inFlight > 0 } }
    var lines: [String] { lock.withLock { said.map(\.line) } }
    var announced: [String] { lines.filter { !isAcknowledgement($0) } }
    var saidDuringInterrupt: [String] { lock.withLock { said.filter(\.duringInterrupt).map(\.line) } }
}

/// A recognizer whose begin waits until `starting` opens and then fails, as when speech
/// recognition is unavailable.
final class StalledTranscriber: Transcribing, @unchecked Sendable {
    let starting = Latch(open: false)
    func begin(partial: @escaping @Sendable (String) -> Void) async throws {
        await starting.wait()
        throw CancellationError()
    }
    func feed(_ samples: [Int16]) async {}
    func finish() async throws -> String { "" }
    func cancel() async {}
}

/// A recognizer that hears `text` and whose finish waits until `finishing` opens: the
/// gap between the release and routing what was said.
final class SlowFinishTranscriber: Transcribing, @unchecked Sendable {
    let finishing = Latch(open: false)
    let text: String
    init(text: String) { self.text = text }
    func begin(partial: @escaping @Sendable (String) -> Void) async throws {}
    func feed(_ samples: [Int16]) async {}
    func finish() async throws -> String {
        await finishing.wait()
        return text
    }
    func cancel() async {}
}

private struct Stage {
    let board = Board()
    let speaker: Speaking
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let conductor: Conductor

    init(heard: String = "", speaker: Speaking = FakeSpeaker(), transcriber: Transcribing? = nil) {
        self.speaker = speaker
        let clock = clock
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar),
                              transcriber: transcriber ?? FakeTranscriber(text: heard), speaker: speaker,
                              resolver: ProjectResolver(roots: [projectRoot(["codergotchi", "chatter"])]),
                              clock: { clock.now })
    }

    /// codergotchi's job, then chatter's, each acknowledged aloud; their sessions.
    func twoJobs() async -> (codergotchi: String, chatter: String) {
        await conductor.route("in codergotchi, run the tests")
        await drained()
        clock.advance(200)
        await conductor.route("in chatter, check the build")
        await drained()
        return (prompts[0], prompts[1])
    }

    func drained() async {
        await conductor.speakQueued()
        let done = await eventually {
            let queued = await conductor.queue.count, speaking = await conductor.speaking
            return queued == 0 && !speaking
        }
        #expect(done, "the spoken queue never drained")
    }

    var queued: [String] { get async { await conductor.queue.map(\.text) } }
    var prompts: [String] { sidecar.of("prompt").compactMap { $0["session"] as? String } }
    var closes: [String] { sidecar.of("close").compactMap { $0["session"] as? String } }
}

// MARK: A stop drops only the stopped job's waiting words

/// "stop codergotchi" while both jobs have words waiting: codergotchi's are dropped,
/// chatter's stay, and the reply goes first; the line being said is not said again.
@Test func aVoiceStopDropsOnlyThatJobsWaitingWords() async {
    let gate = GatedSpeaker()
    let stage = Stage(speaker: gate)
    let (a, b) = await stage.twoJobs()
    gate.hold()
    await stage.conductor.handle(.speak(session: b, text: "Building.", mood: "neutral"))
    #expect(await eventually { gate.announced == ["chatter: Building."] })  // being said, held
    await stage.conductor.handle(.speak(session: a, text: "Tests pass.", mood: "happy"))
    await stage.conductor.handle(.speak(session: b, text: "Still building.", mood: "neutral"))
    #expect(await stage.queued == ["codergotchi: Tests pass.", "chatter: Still building."])
    await stage.conductor.route("stop codergotchi")
    #expect(stage.closes == [a])
    #expect(await stage.queued == ["Stopped codergotchi.", "chatter: Still building."])
    // A stopped job's late words are not queued at all.
    await stage.conductor.handle(.speak(session: a, text: "Lint is clean.", mood: "happy"))
    #expect(await stage.queued == ["Stopped codergotchi.", "chatter: Still building."])
    gate.release()
    await stage.drained()
    #expect(gate.announced == ["chatter: Building.", "Stopped codergotchi.", "chatter: Still building."])
}

/// A failed result closes its session (no request waits there): its failure is still
/// said, after its own earlier words, and the other job's waiting words are kept.
@Test func aFailedResultSaysItsFailureAndDropsNoOtherJobsWords() async {
    let gate = GatedSpeaker()
    let stage = Stage(speaker: gate)
    let (a, b) = await stage.twoJobs()
    gate.hold()
    await stage.conductor.handle(.speak(session: b, text: "Building.", mood: "neutral"))
    #expect(await eventually { gate.announced == ["chatter: Building."] })
    await stage.conductor.handle(.speak(session: a, text: "Half done.", mood: "neutral"))
    await stage.conductor.handle(.speak(session: b, text: "Still building.", mood: "neutral"))
    await stage.conductor.handle(.result(session: a, ok: false, text: "API Error: 500 {\"x\":1}", turns: 1, cost: 0))
    #expect(stage.closes == [a])
    #expect(await stage.queued == ["codergotchi: Half done.", "chatter: Still building.",
                                   "codergotchi: That didn't work: API Error: 500."])
    #expect(await stage.conductor.jobs.map(\.status) == [.done(ok: false), .running])
    gate.release()
    await stage.drained()
    #expect(gate.announced == ["chatter: Building.", "codergotchi: Half done.", "chatter: Still building.",
                               "codergotchi: That didn't work: API Error: 500."])
}

/// Stopping a job whose words are being said neither takes them back nor says them
/// again, whether the user, the board's cancel or the sidecar stops it.
@Test(arguments: ["voice", "cancel", "closed"])
func theWordsBeingSaidAreNotTakenBackWhenTheirJobStops(_ how: String) async {
    let gate = GatedSpeaker()
    let stage = Stage(speaker: gate)
    let (_, b) = await stage.twoJobs()
    gate.hold()
    await stage.conductor.handle(.speak(session: b, text: "Building.", mood: "neutral"))
    #expect(await eventually { gate.announced == ["chatter: Building."] })
    switch how {
    case "voice": await stage.conductor.route("stop chatter")
    case "cancel": await stage.conductor.handle(.cancel)  // stops chatter, the latest job
    default: await stage.conductor.handle(.closed(session: b))
    }
    #expect(await stage.conductor.jobs.last?.status == .stopped, "\(how)")
    gate.release()
    await stage.drained()
    let after: [String] = switch how {
    case "voice": ["Stopped chatter."]
    case "cancel": []  // the board's cancel says nothing; it stops the sound
    default: ["chatter stopped before it finished."]
    }
    #expect(gate.announced == ["chatter: Building."] + after, "\(how)")
}

// MARK: Coalescing

/// A job's newer words replace only its own waiting words; replies, announcements and
/// other jobs' lines keep their places, and the newer words go to the back.
@Test func coalescingNeverDropsAnotherJobsLineOrAReply() async {
    let stage = Stage()
    let conductor = stage.conductor
    await conductor.handle(.pttStart)  // listening: the queue waits, so it can be read
    await conductor.announce("a1", of: 1)
    await conductor.announce("b1", of: 2)
    await conductor.announce("General stopped before it finished.")
    await conductor.reply("Okay.")
    await conductor.announce("a2", of: 1)
    #expect(await stage.queued == ["Okay.", "b1", "General stopped before it finished.", "a2"])
    await conductor.announce("b2", of: 2)
    await conductor.announce("a3", of: 1)
    #expect(await stage.queued == ["Okay.", "General stopped before it finished.", "b2", "a3"])
    #expect(await conductor.queue.map(\.job) == [nil, nil, 2, 1])
    await conductor.handle(.cancel)  // only the utterance: what waited is now said
    await conductor.settled()
    await stage.drained()
    #expect((stage.speaker as! FakeSpeaker).lines == ["Okay.", "General stopped before it finished.", "b2", "a3"])
}

private func job(_ id: Int) -> Job {
    Job(id: id, place: .general, session: "s1", request: "", started: 0)
}

/// Random replies, announcements, job words and stops against a model of the queue: the
/// queue matches after every step, a job never has two lines waiting, a stop is
/// idempotent, and once the user lets go every line is said in the model's order.
@Test(arguments: 0..<48)
func theSpokenQueueFollowsItsModelUnderAnyOrder(_ seed: Int) async {
    var random = Seeded(seed)
    let stage = Stage()
    let conductor = stage.conductor
    await conductor.handle(.pttStart)
    var model: [(text: String, job: Int?)] = []
    for step in 0..<(20 + random.below(80)) {
        let context = "seed \(seed) step \(step)"
        let text = "line \(step)"
        switch random.below(20) {
        case 0, 1, 2:
            await conductor.reply(text)
            model.insert((text, nil), at: 0)
        case 3, 4, 5, 6:
            await conductor.announce(text)
            model.append((text, nil))
        case 7...15:
            let id = 1 + random.below(4)
            await conductor.announce(text, of: id)
            model.removeAll { $0.job == id }
            model.append((text, id))
        default:
            let ids = (1...5).filter { _ in random.chance(35) }
            await conductor.silence(ids.map(job))
            model.removeAll { $0.job.map(ids.contains) ?? false }
            let once = await conductor.queue
            await conductor.silence(ids.map(job))
            #expect(await conductor.queue == once, "\(context): a second stop changed the queue")
        }
        let queue = await conductor.queue
        #expect(queue.map(\.text) == model.map(\.text) && queue.map(\.job) == model.map(\.job), "\(context)")
        let jobs = queue.compactMap(\.job)
        #expect(Set(jobs).count == jobs.count, "\(context): a job has two lines waiting")
    }
    #expect((stage.speaker as! FakeSpeaker).lines.isEmpty, "seed \(seed): said while the user held")
    await conductor.handle(.cancel)
    await conductor.settled()
    await stage.drained()
    #expect((stage.speaker as! FakeSpeaker).lines == model.map(\.text), "seed \(seed)")
}

// MARK: While the user holds to talk

/// Words, a replacement and a failure arrive while the user holds; on release the reply
/// to what they said comes first, then everything that waited, in order, and nothing was
/// said while they held.
@Test func wordsQueuedWhileTheUserHoldsAreSaidAfterTheReplyInOrder() async {
    let gate = GatedSpeaker()
    let stage = Stage(heard: "what's running", speaker: gate)
    let (a, b) = await stage.twoJobs()
    await stage.conductor.handle(.pttStart)
    gate.holding = true
    await stage.conductor.handle(.speak(session: a, text: "Tests pass.", mood: "happy"))
    await stage.conductor.handle(.speak(session: b, text: "Building.", mood: "neutral"))
    await stage.conductor.handle(.speak(session: b, text: "Still building.", mood: "neutral"))
    await stage.conductor.handle(.result(session: a, ok: false, text: "Boom", turns: 1, cost: 0))
    try? await Task.sleep(for: .milliseconds(50))  // a line begun now would be caught
    let waiting = ["codergotchi: Tests pass.", "chatter: Still building.", "codergotchi: That didn't work: Boom."]
    #expect(await stage.queued == waiting)
    #expect(gate.announced.isEmpty)
    let status = await stage.conductor.book.status(at: stage.clock.now)
    gate.holding = false
    await stage.conductor.handle(.pttEnd)
    await stage.conductor.settled()
    await stage.drained()
    #expect(gate.spokenWhileHolding.isEmpty)
    #expect(gate.announced == [status] + waiting)
}

/// "stop chatter" said while both jobs' words waited: chatter's are dropped, and
/// codergotchi's are said after the reply.
@Test func aStopSaidWhileHoldingKeepsTheOtherJobsWords() async {
    let stage = Stage(heard: "stop chatter")
    let (a, b) = await stage.twoJobs()
    await stage.conductor.handle(.pttStart)
    await stage.conductor.handle(.speak(session: a, text: "Tests pass.", mood: "happy"))
    await stage.conductor.handle(.speak(session: b, text: "Building.", mood: "neutral"))
    await stage.conductor.handle(.pttEnd)
    await stage.conductor.settled()
    await stage.drained()
    #expect(stage.closes == [b])
    #expect((stage.speaker as! FakeSpeaker).announced == ["Stopped chatter.", "codergotchi: Tests pass."])
}

// MARK: No line starts while a press interrupts

/// The press's interrupt suspends: the line being said ends meanwhile, and neither the
/// line waiting behind it nor words arriving now may start before the press listens.
@Test func noLineStartsWhileAPressIsInterrupting() async throws {
    let speaker = SuspendingSpeaker()
    let stage = Stage(speaker: speaker)
    let (a, b) = await stage.twoJobs()
    speaker.playing.close()
    await stage.conductor.handle(.speak(session: b, text: "One.", mood: "neutral"))
    #expect(await eventually { speaker.announced == ["chatter: One."] })
    await stage.conductor.handle(.speak(session: a, text: "Two.", mood: "neutral"))
    speaker.stopping.close()
    let press = Task { await stage.conductor.handle(.pttStart) }
    #expect(await eventually { speaker.interrupting })
    speaker.playing.open()  // the line being said ends while the interrupt is in flight
    await stage.conductor.handle(.speak(session: a, text: "Three.", mood: "neutral"))
    #expect(await eventually { await !stage.conductor.speaking })
    try? await Task.sleep(for: .milliseconds(50))
    #expect(speaker.saidDuringInterrupt.isEmpty, "\(speaker.saidDuringInterrupt)")
    #expect(speaker.announced == ["chatter: One."])
    #expect(await stage.queued == ["codergotchi: Three."])
    speaker.stopping.open()
    await press.value
    // Cancelling the utterance says what waited.
    await stage.conductor.handle(.cancel)
    await stage.conductor.settled()
    await stage.drained()
    #expect(speaker.announced == ["chatter: One.", "codergotchi: Three."])
}

/// A cancel while the user holds ends the utterance and lets the waiting lines go: none
/// should start before its interrupt and stopAudio, or the stop may cut the first one.
@Test func noLineStartsWhileACancelOfTheUtteranceIsInterrupting() async throws {
    let speaker = SuspendingSpeaker()
    let stage = Stage(speaker: speaker)
    let (a, _) = await stage.twoJobs()
    await stage.conductor.handle(.pttStart)
    await stage.conductor.handle(.speak(session: a, text: "Tests pass.", mood: "happy"))
    speaker.stopping.close()
    let cancel = Task { await stage.conductor.handle(.cancel) }
    #expect(await eventually { speaker.interrupting })
    try? await Task.sleep(for: .milliseconds(50))
    let early = speaker.saidDuringInterrupt
    speaker.stopping.open()
    await cancel.value
    await stage.conductor.settled()
    await stage.drained()
    #expect(speaker.announced == ["codergotchi: Tests pass."])
    #expect(early.isEmpty, "said while the cancel's interrupt was in flight: \(early)")
}

/// The recognizer fails to start after the press stopped the speaking loop: the line
/// that waited must still be said, though no release follows a failed press.
@Test func aPressWhoseRecognizerFailsLeavesNoLineStranded() async {
    let gate = GatedSpeaker()
    let transcriber = StalledTranscriber()
    let stage = Stage(speaker: gate, transcriber: transcriber)
    gate.hold()
    await stage.conductor.announce("One.")
    #expect(await eventually { gate.lines == ["One."] })
    await stage.conductor.announce("Two.")
    let press = Task { await stage.conductor.handle(.pttStart) }
    let listening = await eventually {
        stage.board.messages.contains { if case .state(.listening, _, _, _) = $0 { true } else { false } }
    }
    #expect(listening)
    gate.release()  // "One." ends while listening: the loop stops before "Two."
    #expect(await eventually { await !stage.conductor.speaking })
    transcriber.starting.open()
    await press.value
    await stage.conductor.handle(.pttEnd)  // ignored: not listening
    await stage.conductor.settled()
    let said = await eventually(0.5) { gate.lines == ["One.", "Two."] }
    let stranded = await stage.queued
    #expect(said, "stranded: \(stranded)")
}

// MARK: The release waits for what was said

/// Words and a summary arrive after the release while what the user said is still being
/// recognised: nothing is said until it is routed, so "stop codergotchi" drops
/// codergotchi's words before any start, and chatter's summary follows the reply.
@Test func nothingIsSaidBetweenTheReleaseAndRoutingWhatWasSaid() async {
    let transcriber = SlowFinishTranscriber(text: "stop codergotchi")
    let speaker = SuspendingSpeaker()
    let stage = Stage(speaker: speaker, transcriber: transcriber)
    let (a, b) = await stage.twoJobs()
    await stage.conductor.handle(.pttStart)
    let release = Task { await stage.conductor.handle(.pttEnd) }
    #expect(await eventually { await !stage.conductor.listening })
    await stage.conductor.handle(.speak(session: a, text: "Tests pass.", mood: "happy"))
    await stage.conductor.handle(.result(session: b, ok: true, text: "The build is clean.", turns: 1, cost: 0))
    try? await Task.sleep(for: .milliseconds(50))
    #expect(speaker.announced.isEmpty, "said before routing: \(speaker.announced)")
    transcriber.finishing.open()
    await release.value
    await stage.conductor.settled()
    await stage.drained()
    #expect(speaker.announced == ["Stopped codergotchi.", "chatter: The build is clean."])
    #expect(await !stage.conductor.routing)
}

/// Routing that waits on a card lets what waited be said meanwhile, as before.
@Test func aCardWhileRoutingDoesNotHoldTheQueue() async {
    let transcriber = SlowFinishTranscriber(text: "in codergotchy, lint it")  // a near miss: a card
    let speaker = SuspendingSpeaker()
    let stage = Stage(speaker: speaker, transcriber: transcriber)
    let (_, b) = await stage.twoJobs()
    await stage.conductor.handle(.pttStart)
    let release = Task { await stage.conductor.handle(.pttEnd) }
    #expect(await eventually { await !stage.conductor.listening })
    await stage.conductor.handle(.result(session: b, ok: true, text: "The build is clean.", turns: 1, cost: 0))
    transcriber.finishing.open()
    await release.value
    // The card is up and unanswered; the summary is said all the same.
    #expect(await eventually { speaker.announced == ["chatter: The build is clean."] })
    #expect(await !stage.conductor.routing)
}

// MARK: Job ids are never reused

/// Ids keep counting after old jobs are forgotten and sessions close, so a stop can
/// never name a newer job.
@Test func jobIdsAreNeverReusedAfterOldJobsAreForgotten() {
    var book = JobBook()
    let chatter = Place.project(Project(name: "chatter", url: URL(fileURLWithPath: "/dev/chatter")))
    var seen = Set<Int>(), last = 0
    for step in 0..<(JobBook.remembered * 4) {
        let job = book.start("job \(step)", in: step.isMultiple(of: 2) ? .general : chatter, at: Double(step)).job
        #expect(job.id > last && !seen.contains(job.id), "step \(step)")
        seen.insert(job.id)
        last = job.id
        if step.isMultiple(of: 7) {
            _ = book.closedAll(at: Double(step))
        } else {
            _ = book.finished(ok: !step.isMultiple(of: 3), result: "Done.", in: job.session, at: Double(step))
        }
    }
    #expect(book.jobs.count == JobBook.remembered && book.jobs.first!.id > 1)
    #expect(book.start("next", in: .general, at: 999).job.id == last + 1)
}

/// A stop naming a job long forgotten leaves a newer job's waiting words alone.
@Test func aStopOfAForgottenJobLeavesNewerWordsAlone() async throws {
    let stage = Stage()
    let conductor = stage.conductor
    await conductor.work("first", in: .general)
    let stale = try #require(await conductor.jobs.first)
    for index in 0...JobBook.remembered {
        if index > 0 { await conductor.work("job \(index)", in: .general) }
        await conductor.handle(.result(session: stale.session, ok: true, text: "Done.", turns: 1, cost: 0))
    }
    #expect(await !conductor.jobs.contains { $0.id == stale.id })
    await stage.drained()
    await conductor.work("latest", in: .general)
    let latest = try #require(await conductor.jobs.last)
    #expect(latest.id > stale.id)
    await conductor.handle(.pttStart)
    await conductor.handle(.speak(session: latest.session, text: "Still here.", mood: "neutral"))
    await conductor.silence([stale])
    #expect(await conductor.queue == [Conductor.Line(text: "Still here.", job: latest.id)])
}
