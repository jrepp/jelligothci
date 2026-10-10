import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// Background jobs (RFC-003): requests sorted before Claude, jobs side by side in their
// places' sessions, and short spoken summaries that never talk over the user.

/// A projects folder with codergotchi, chatter and a secret-looking file beside them.
private func projectsFolder() -> URL {
    let root = FileManager.default.temporaryDirectory.appendingPathComponent("loki-jobs-\(UUID())")
    for name in ["codergotchi", "chatter"] {
        try! FileManager.default.createDirectory(at: root.appendingPathComponent(name), withIntermediateDirectories: true)
    }
    FileManager.default.createFile(atPath: root.appendingPathComponent("git-token.txt").path, contents: Data("x".utf8))
    FileManager.default.createFile(atPath: root.appendingPathComponent("notes.md").path, contents: Data("x".utf8))
    return root
}

final class TestClock: @unchecked Sendable {
    private let lock = NSLock()
    private var seconds = 1000.0
    var now: Double { lock.withLock { seconds } }
    func advance(_ by: Double) { lock.withLock { seconds += by } }
}

/// A sidecar that records each command it is sent, decoded.
final class RecordingSidecar: SidecarTransport, @unchecked Sendable {
    let lines: AsyncStream<Data>
    private let lock = NSLock()
    private var sent: [[String: Any]] = []
    init() { lines = AsyncStream { _ in } }
    func send(_ line: Data) {
        let object = (try? JSONSerialization.jsonObject(with: line)) as? [String: Any] ?? [:]
        lock.withLock { sent.append(object) }
    }
    var commands: [[String: Any]] { lock.withLock { sent } }
    func of(_ type: String) -> [[String: Any]] { commands.filter { $0["type"] as? String == type } }
}

private struct Desk {
    let root = projectsFolder()
    let board = Board()
    let speaker = FakeSpeaker()
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let conductor: Conductor
    var resolver: ProjectResolver { ProjectResolver(roots: [root]) }

    init(heard: String = "") {
        let clock = clock
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar),
                              transcriber: FakeTranscriber(text: heard), speaker: speaker,
                              resolver: ProjectResolver(roots: [root]), clock: { clock.now })
    }

    func project(_ name: String) -> Project { Project(name: name, url: root.appendingPathComponent(name).standardizedFileURL) }

    /// Says something and lets go, as the board would.
    func say(_ text: String) async {
        await conductor.route(text)
        await conductor.speakQueued()
    }
}

// MARK: Requests

@Test func requestsAreSortedBeforeAnythingGoesToClaude() {
    let desk = Desk()
    let resolver = desk.resolver
    let codergotchi = desk.project("codergotchi"), chatter = desk.project("chatter")
    func kind(_ text: String) -> Request { Requests.classify(text, resolver: resolver) }
    #expect(kind("What's running?") == .status(nil, "What's running?"))
    #expect(kind("how's it going") == .status(nil, "how's it going"))
    #expect(kind("How's codergotchi going?") == .status(codergotchi, "How's codergotchi going?"))
    #expect(kind("how is coder gotchi doing") == .status(codergotchi, "how is coder gotchi doing"))
    #expect(kind("status of chatter") == .status(chatter, "status of chatter"))
    #expect(kind("Stop.") == .stop(nil))
    #expect(kind("never mind") == .stop(nil))
    #expect(kind("cancel everything") == .stop(nil))
    #expect(kind("stop chatter") == .stop(chatter))
    #expect(kind("Say that again.") == .repeatLast(nil))
    #expect(kind("what did chatter say") == .repeatLast(chatter))
    for work in ["In codergotchi, run the tests.", "stop the dev server in chatter", "What's the latest Swift release?",
                 "Explain how the link reconnects", "now commit it"] {
        #expect(kind(work) == .work(work), "\(work)")
    }
}

// MARK: The job book

@Test func aPlaceKeepsItsSessionAndFollowUpsFindIt() {
    var book = JobBook()
    let place = Place.project(Project(name: "chatter", url: URL(fileURLWithPath: "/dev/chatter")))
    let first = book.start("run the tests", in: place, at: 0)
    let second = book.start("now commit it", in: place, at: 5)
    let general = book.start("what is a monad", in: .general, at: 6)
    #expect(first.newSession && !second.newSession && general.newSession)
    #expect(first.job.session == second.job.session && general.job.session != first.job.session)
    #expect(book.current(in: first.job.session)?.id == first.job.id)  // In the order given.
    _ = book.finished(ok: true, result: "", in: general.job.session, at: 7)
    #expect(book.followUp(at: 8) == .general)
    _ = book.finished(ok: true, result: "", in: first.job.session, at: 9)
    _ = book.finished(ok: true, result: "", in: first.job.session, at: 10)
    #expect(book.followUp(at: 10 + JobBook.followUpSeconds) == place)
    #expect(book.followUp(at: 10.1 + JobBook.followUpSeconds) == nil)
    // A closed session stops its jobs, and the place gets a new one next time.
    let third = book.start("again", in: place, at: 20)
    #expect(book.closed(third.job.session, at: 21).map(\.id) == [third.job.id])
    #expect(book.start("once more", in: place, at: 22).newSession)
}

@Test func summariesPreferWhatClaudeSaidElseTwoPlainSentences() {
    var book = JobBook()
    let a = book.start("a", in: .general, at: 0).job
    book.spoke("All 91 tests pass.", in: a.session)
    #expect(book.finished(ok: true, result: "# Long report\n...", in: a.session, at: 1)?.summary == "All 91 tests pass.")
    let b = book.start("b", in: .general, at: 2).job
    let summary = book.finished(ok: true, result: "\n**Done.** I ran `make test`. Then I fixed three things. More.",
                                in: b.session, at: 3)?.summary
    #expect(summary == "Done. I ran make test.")
    let c = book.start("c", in: .general, at: 4).job
    #expect(book.finished(ok: false, result: "", in: c.session, at: 5)?.summary == "That didn't work.")
}

@Test func statusIsShortAndSaysWhatIsRunningAndWhatFinished() {
    var book = JobBook()
    #expect(book.status(at: 0) == "Nothing is running.")
    let chatter = Place.project(Project(name: "chatter", url: URL(fileURLWithPath: "/dev/chatter")))
    let job = book.start("run the tests", in: chatter, at: 0).job
    book.progress("Running the test suite", in: job.session)
    #expect(book.status(at: 125) == "chatter is on: Running the test suite, for 2 minutes.")
    let other = book.start("why", in: .general, at: 100).job
    book.asking(true, in: other.session)
    #expect(book.status(at: 130).hasPrefix("2 jobs are running."))
    #expect(book.status(at: 130).contains("General is waiting for you on the device, for 30 seconds."))
    book.spoke("The tests pass.", in: job.session)
    _ = book.finished(ok: true, result: "", in: job.session, at: 140)
    #expect(book.status(of: chatter, at: 150) == "chatter finished: The tests pass.")
    #expect(book.status(at: 150).contains("chatter finished: The tests pass."))
    #expect(book.status(of: .project(Project(name: "x", url: URL(fileURLWithPath: "/x"))), at: 150) == nil)
    #expect(book.lastSummary(of: nil) == "chatter: The tests pass.")
    #expect(JobBook.fitted(String(repeating: "word ", count: 200)).utf8.count <= 400)
}

// MARK: The conductor

@Test func workGoesToTheNamedProjectAndFollowUpsStayThere() async throws {
    let desk = Desk()
    await desk.say("In codergotchi, run the tests.")
    await desk.say("now commit it")
    let starts = desk.sidecar.of("start"), prompts = desk.sidecar.of("prompt")
    #expect(starts.count == 1)
    let options = try #require(starts.first?["options"] as? [String: Any])
    #expect(options["cwd"] as? String == desk.project("codergotchi").url.path)
    #expect(prompts.map { $0["text"] as? String } == ["In codergotchi, run the tests.", "now commit it"])
    #expect(Set(prompts.map { $0["session"] as? String }).count == 1)
}

/// The general session reads the projects and nothing beside them.
@Test func otherQuestionsGoToTheGeneralSessionWhichCannotReadSecrets() async throws {
    let desk = Desk()
    await desk.say("What's the latest Swift release?")
    let start = try #require(desk.sidecar.of("start").first)
    let options = try #require(start["options"] as? [String: Any])
    #expect(options["cwd"] as? String == desk.root.path)
    let settings = try #require(options["settings"] as? [String: Any])
    let sandbox = try #require(settings["sandbox"] as? [String: Any])
    let denied = try #require((sandbox["filesystem"] as? [String: Any])?["denyRead"] as? [String])
    let token = desk.root.appendingPathComponent("git-token.txt").standardizedFileURL.path
    let notes = desk.root.appendingPathComponent("notes.md").standardizedFileURL.path
    #expect(denied.contains(token) && denied.contains(notes))
    #expect(!denied.contains { $0.hasSuffix("/codergotchi") || $0.hasSuffix("/chatter") })
    let deny = try #require((settings["permissions"] as? [String: Any])?["deny"] as? [String])
    #expect(deny.contains("Read(/\(token))"))
    #expect((start["options"] as? [String: Any])?["systemPrompt"].map { "\($0)" }?.contains("general session") == true)
}

@Test func jobsRunSideBySideAndEachAnnouncesItsSummary() async {
    let desk = Desk()
    await desk.say("in codergotchi, run the tests")
    desk.clock.advance(200)  // Past the follow-up window: chatter gets its own session.
    await desk.say("on chatter, check the build")
    let sessions = desk.sidecar.of("prompt").compactMap { $0["session"] as? String }
    #expect(sessions.count == 2 && sessions[0] != sessions[1])
    await desk.conductor.handle(.progress(session: sessions[1], message: "Building", fraction: nil))
    await desk.say("what's running")
    #expect(desk.speaker.lines.last?.hasPrefix("2 jobs are running.") == true)
    await desk.conductor.handle(.speak(session: sessions[1], text: "The build is clean.", mood: "happy"))
    await desk.conductor.handle(.result(session: sessions[1], ok: true, text: "long text", turns: 3, cost: 0.1))
    await desk.conductor.speakQueued()
    #expect(desk.speaker.lines.last == "chatter: The build is clean.")
    #expect(desk.board.messages.contains { if case .state(.success, _, _, _) = $0 { true } else { false } })
    #expect(await desk.conductor.jobs.filter(\.isActive).count == 1)
}

@Test func announcementsWaitWhileTheUserTalks() async {
    let desk = Desk(heard: "how's codergotchi going")
    await desk.say("in codergotchi, run the tests")
    let session = desk.sidecar.of("prompt")[0]["session"] as! String
    await desk.conductor.handle(.pttStart)
    await desk.conductor.handle(.speak(session: session, text: "All tests pass.", mood: "happy"))
    await desk.conductor.handle(.result(session: session, ok: true, text: "", turns: 1, cost: 0))
    try? await Task.sleep(for: .milliseconds(50))
    #expect(desk.speaker.announced.isEmpty)  // Holding to talk: nothing spoken.
    await desk.conductor.handle(.pttEnd)
    await desk.conductor.settled()
    // The answer to what was just asked first, then the waiting announcement.
    #expect(desk.speaker.announced == ["codergotchi finished: All tests pass.", "codergotchi: All tests pass."])
}

@Test func stopClosesThatPlacesSessionAndItsLateResultIsNotAnnounced() async {
    let desk = Desk()
    await desk.say("in chatter, refactor the client")
    let session = desk.sidecar.of("prompt")[0]["session"] as! String
    await desk.say("stop chatter")
    #expect(desk.sidecar.of("close").map { $0["session"] as? String } == [session])
    #expect(desk.speaker.lines.last == "Stopped chatter.")
    await desk.conductor.handle(.result(session: session, ok: false, text: "Interrupted", turns: 1, cost: 0))
    await desk.conductor.handle(.closed(session: session))
    await desk.conductor.speakQueued()
    #expect(desk.speaker.announced == ["Stopped chatter."])
    await desk.say("stop")
    #expect(desk.speaker.lines.last == "Nothing is running.")
}

@Test func statusOfAProjectWithNoJobAsksClaudeAndRepeatSaysTheLastSummary() async {
    let desk = Desk()
    await desk.say("say that again")
    #expect(desk.speaker.lines.last == "There's nothing to repeat yet.")
    await desk.say("status of chatter")
    let prompt = desk.sidecar.of("prompt").first
    #expect(prompt?["text"] as? String == "status of chatter")
    #expect((desk.sidecar.of("start").first?["options"] as? [String: Any])?["cwd"] as? String
            == desk.project("chatter").url.path)
    let session = prompt?["session"] as! String
    await desk.conductor.handle(.speak(session: session, text: "Two files changed.", mood: "neutral"))
    await desk.conductor.handle(.result(session: session, ok: true, text: "", turns: 1, cost: 0))
    await desk.conductor.speakQueued()
    await desk.say("what did chatter say")
    #expect(desk.speaker.lines.last == "chatter: Two files changed.")
}

@Test func aPermissionBecomesACardNamedForItsPlace() async throws {
    let desk = Desk()
    await desk.say("in codergotchi, deploy it")
    let session = desk.sidecar.of("prompt")[0]["session"] as! String
    await desk.conductor.handle(.permission(session: session, request: "r1", tool: "Bash",
                                            input: .object(["command": .string("make deploy"),
                                                            "dangerouslyDisableSandbox": .bool(true)]),
                                            reason: nil, blockedPath: nil))
    let ask = try #require(await desk.board.lastAsk())
    #expect(ask.title.contains("outside the sandbox"))
    #expect(ask.detail.contains("make deploy") && ask.detail.contains("codergotchi"))
    #expect(await desk.conductor.jobs.first?.status == .asking)
    await desk.conductor.handle(.answer(id: ask.id, nonce: ask.nonce, digest: ask.digest, choice: 0))
    await desk.conductor.settled()
    let decision = try #require(desk.sidecar.of("decision").first)
    #expect(decision["allow"] as? Bool == true && decision["request"] as? String == "r1")
    #expect(await desk.conductor.jobs.first?.status == .running)
}
