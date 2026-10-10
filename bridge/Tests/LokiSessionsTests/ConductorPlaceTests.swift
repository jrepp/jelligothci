import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// Where work goes: a project named exactly, a near miss confirmed on the board, a pick
// among busy places, a follow-up, or the general session, which is reopened after it
// idles. And what happens off the event loops: cards for stopped jobs, late answers,
// and many cards from many jobs answered in any order.

private struct Bench {
    let root = projectRoot(["codergotchi", "chatter", "atrium", "elysium-main", "elysium-lan"])
    let board = Board()
    let speaker = FakeSpeaker()
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let transcriber: FakeTranscriber
    let conductor: Conductor

    init(heard: String = "") {
        let clock = clock
        transcriber = FakeTranscriber(text: heard)
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar), transcriber: transcriber,
                              speaker: speaker, resolver: ProjectResolver(roots: [root]), clock: { clock.now })
    }

    func project(_ name: String) -> Project { Project(name: name, url: root.appendingPathComponent(name).standardizedFileURL) }

    func say(_ text: String) async {
        await conductor.route(text)
        await conductor.speakQueued()
    }

    /// Says something that waits on a card; the caller answers it.
    func sayAndWait(_ text: String) -> Task<Void, Never> {
        Task { await say(text) }
    }

    var asks: [Ask] { board.messages.compactMap { if case .ask(let ask) = $0 { ask } else { nil } } }
    var sessions: [String] { sidecar.of("prompt").compactMap { $0["session"] as? String } }
    var starts: [[String: Any]] { sidecar.of("start") }
    var closes: [String] { sidecar.of("close").compactMap { $0["session"] as? String } }

    func cwd(_ start: [String: Any]) -> String? { (start["options"] as? [String: Any])?["cwd"] as? String }

    /// The session a job in `name` (or the general session) runs in.
    func session(of place: Place) async -> String? { await conductor.book.session(of: place) }

    func answer(_ ask: Ask, _ choice: UInt32) async {
        await conductor.handle(.answer(id: ask.id, nonce: ask.nonce, digest: ask.digest, choice: choice))
    }

    func finish(_ session: String, _ text: String = "Done.") async {
        await conductor.handle(.result(session: session, ok: true, text: text, turns: 1, cost: 0))
    }
}

// MARK: A named project

/// Named exactly, in any spelling the recognizer gives, the work goes there without a
/// card and is acknowledged aloud.
@Test(arguments: [("in codergotchi, run the tests", "codergotchi"), ("In Coder Gotchi run the tests", "codergotchi"),
                  ("on the chatter project, check the build", "chatter"), ("open atrium and list the todos", "atrium"),
                  ("in elysium-main, rebase", "elysium-main"), ("in elysium main, rebase", "elysium-main")])
func workNamingAProjectExactlyGoesThereWithoutACard(_ text: String, _ name: String) async {
    let bench = Bench()
    await bench.say(text)
    #expect(bench.asks.isEmpty, "\(text)")
    #expect(bench.starts.count == 1 && bench.cwd(bench.starts[0]) == bench.project(name).url.path, "\(text)")
    #expect(bench.sidecar.of("prompt").first?["text"] as? String == text)
    #expect(bench.speaker.lines == ["Okay, in \(name)."])
}

/// A near miss asks "Did you mean X?"; Yes sends the work there, as said.
@Test func aNearMissIsConfirmedAndYesSendsTheWorkThere() async throws {
    let bench = Bench()
    let routing = bench.sayAndWait("in chater, run the tests")
    #expect(await eventually { bench.asks.count == 1 })
    let card = try #require(bench.asks.first)
    #expect(card.kind == .choose && card.title == "Did you mean chatter?" && card.options == ["Yes", "No"])
    #expect(bench.sidecar.commands.isEmpty)  // Nothing goes to Claude while the card waits.
    await bench.answer(card, 0)
    await routing.value
    #expect(bench.cwd(try #require(bench.starts.first)) == bench.project("chatter").url.path)
    #expect(bench.sidecar.of("prompt").map { $0["text"] as? String } == ["in chater, run the tests"])
    #expect(bench.speaker.lines == ["Okay, in chatter."])
}

/// No, or no answer at all, sends nothing anywhere and says so.
@Test(arguments: ["no", "gone", "cancelledByBoard"])
func aNearMissDeclinedOrUnansweredSendsNothing(_ how: String) async throws {
    let bench = Bench()
    let routing = bench.sayAndWait("on codergotchy, deploy it")
    #expect(await eventually { bench.asks.count == 1 })
    let card = try #require(bench.asks.first)
    #expect(card.title == "Did you mean codergotchi?")
    switch how {
    case "no": await bench.answer(card, 1)
    case "gone": await bench.conductor.handle(.gone)
    default: await bench.conductor.approvals.boardGone()
    }
    await routing.value
    #expect(bench.sidecar.commands.isEmpty)
    #expect(bench.speaker.lines == ["Okay, never mind."])
    #expect(await bench.conductor.jobs.isEmpty)
}

/// A wrong answer (another card's nonce, a choice not offered) changes nothing; the
/// right one still decides.
@Test func aNearMissCardIgnoresAnswersThatDoNotMatch() async throws {
    let bench = Bench()
    let routing = bench.sayAndWait("in atrum, list the todos")
    #expect(await eventually { bench.asks.count == 1 })
    let card = try #require(bench.asks.first)
    await bench.conductor.handle(.answer(id: card.id, nonce: String(repeating: "0", count: 32), digest: card.digest, choice: 0))
    await bench.answer(card, 2)
    try await Task.sleep(for: .milliseconds(20))
    #expect(bench.sidecar.commands.isEmpty && bench.speaker.lines.isEmpty)
    await bench.answer(card, 0)
    await routing.value
    #expect(bench.cwd(try #require(bench.starts.first)) == bench.project("atrium").url.path)
}

// MARK: Busy places

/// Two places busy and no name: "For which one?", newest first; the work joins the
/// chosen place's session, with no new session opened.
@Test func severalBusyPlacesAskForWhichOne() async throws {
    let bench = Bench()
    await bench.say("what is a monad")
    await bench.say("in chatter, run the tests")
    await bench.say("in codergotchi, check the build")
    let before = bench.starts.count
    let routing = bench.sayAndWait("and then commit it")
    #expect(await eventually { bench.asks.count == 1 })
    let card = try #require(bench.asks.first)
    #expect(card.title == "For which one?" && card.options == ["codergotchi", "chatter", "General"])
    await bench.answer(card, 1)
    await routing.value
    let chatter = try #require(await bench.session(of: .project(bench.project("chatter"))))
    #expect(bench.sessions.last == chatter && bench.starts.count == before)
    #expect(bench.speaker.lines.last == "Okay, in chatter.")
    // Choosing General is not acknowledged: only work in a project is.
    let again = bench.sayAndWait("what does it print")
    #expect(await eventually { bench.asks.count == 2 })
    await bench.answer(bench.asks[1], 2)
    await again.value
    #expect(bench.sessions.last == (await bench.session(of: .general)) && bench.speaker.lines.last == "Okay, in chatter.")
}

/// Exactly two busy places are already several: the card is shown.
@Test func twoBusyPlacesAreEnoughToAskForWhichOne() async throws {
    let bench = Bench()
    await bench.say("in chatter, run the tests")
    await bench.say("in codergotchi, check the build")
    let routing = bench.sayAndWait("and commit it")
    #expect(await eventually { bench.asks.count == 1 })
    #expect(bench.asks.first?.options == ["codergotchi", "chatter"])
    await bench.answer(try #require(bench.asks.first), 0)
    await routing.value
    #expect(bench.sessions.last == (await bench.session(of: .project(bench.project("codergotchi")))))
}

/// At most four places are offered, the four newest; unanswered sends nothing.
@Test func atMostFourBusyPlacesAreOfferedAndSilenceSendsNothing() async throws {
    let bench = Bench()
    for name in ["codergotchi", "chatter", "atrium", "elysium-main", "elysium-lan"] {
        await bench.say("in \(name), run the tests")
    }
    let prompts = bench.sessions.count
    let routing = bench.sayAndWait("and commit")
    #expect(await eventually { bench.asks.count == 1 })
    #expect(bench.asks[0].options == ["elysium-lan", "elysium-main", "atrium", "chatter"])
    await bench.conductor.handle(.gone)
    await routing.value
    #expect(bench.sessions.count == prompts && bench.speaker.lines.last == "Okay, never mind.")
}

/// One busy place, however many jobs it has, needs no card: the work follows it.
@Test func oneBusyPlaceIsAFollowUpWithoutACard() async {
    let bench = Bench()
    await bench.say("in chatter, run the tests")
    await bench.say("in chatter, then fix the lint")
    await bench.say("and update the changelog")
    #expect(bench.asks.isEmpty && Set(bench.sessions).count == 1 && bench.starts.count == 1)
}

// MARK: Follow-ups and the general session

/// A follow-up goes to the place of the last job that was not stopped, for two minutes
/// after it ended; after that, to the general session.
@Test func followUpsSkipStoppedJobsAndExpire() async throws {
    let bench = Bench()
    await bench.say("in chatter, run the tests")
    let chatter = bench.sessions[0]
    await bench.finish(chatter, "Tests pass.")
    bench.clock.advance(10)
    await bench.say("in codergotchi, deploy it")
    await bench.say("stop codergotchi")
    bench.clock.advance(10)
    await bench.say("now commit it")
    #expect(bench.sessions.last == chatter)
    await bench.finish(chatter)
    bench.clock.advance(JobBook.followUpSeconds + 0.5)
    await bench.say("what is a monad")
    #expect(bench.cwd(try #require(bench.starts.last)) == bench.root.path)
}

/// After ten idle minutes the general session is closed and reopened, so it sees what
/// now lies beside the projects; at exactly ten minutes, or while it works, it is kept.
@Test func theGeneralSessionReopensAfterItIdlesWithAFreshView() async throws {
    let bench = Bench()
    await bench.say("what is a monad")
    let first = try #require(await bench.session(of: .general))
    await bench.finish(first)
    bench.clock.advance(Conductor.generalIdleSeconds)
    await bench.say("and a functor")
    #expect(bench.sessions.last == first && bench.closes.isEmpty)
    await bench.finish(first)
    try Data("x".utf8).write(to: bench.root.appendingPathComponent("new-token.txt"))
    bench.clock.advance(Conductor.generalIdleSeconds + 0.5)
    await bench.say("what is a monoid")
    let second = try #require(await bench.session(of: .general))
    #expect(second != first && bench.closes == [first] && bench.sessions.last == second)
    let denyRead = ((bench.starts.last?["options"] as? [String: Any]).flatMap { value($0, "settings.sandbox.filesystem.denyRead") }
                    as? [String]) ?? []
    #expect(denyRead.contains(bench.root.appendingPathComponent("new-token.txt").standardizedFileURL.path))
    // Still working long after it started: kept, and the request queues behind it.
    bench.clock.advance(Conductor.generalIdleSeconds * 3)
    await bench.say("and a group")
    #expect(bench.sessions.last == second && bench.closes == [first])
}

/// The general session is reopened after idling even when its jobs have been forgotten
/// behind twenty newer ones elsewhere.
@Test func theGeneralSessionReopensEvenWhenItsJobsWereForgotten() async throws {
    let bench = Bench()
    await bench.say("what is a monad")
    let general = try #require(await bench.session(of: .general))
    await bench.finish(general)
    for index in 0..<JobBook.remembered {
        await bench.say("in chatter, task \(index)")
        await bench.finish(bench.sessions.last!)
    }
    #expect(await bench.conductor.jobs.contains { $0.place == .general } == false)
    bench.clock.advance(Conductor.generalIdleSeconds + JobBook.followUpSeconds + 1)
    await bench.say("what is a functor")
    #expect(bench.closes.contains(general))
}

// MARK: Stops by voice

@Test func stopsNameEveryPlaceTheyStopAndLeaveIdlePlacesAlone() async {
    let bench = Bench()
    await bench.say("what is a monad")
    await bench.say("in chatter, run the tests")
    await bench.say("in codergotchi, check the build")
    await bench.say("stop atrium")  // nothing runs there
    #expect(bench.closes.isEmpty && bench.speaker.lines.last == "Nothing is running.")
    await bench.say("Loki, cancel everything")
    #expect(bench.speaker.lines.last == "Stopped General, chatter and codergotchi.")
    #expect(Set(bench.closes).count == 3)
    #expect(await bench.conductor.jobs.allSatisfy { $0.status == .stopped })
}

// MARK: Off the event loops

/// A permission or question from a session that is unknown or already stopped is
/// denied at once, without a card, so Claude Code never waits on it.
@Test func cardsForUnknownOrStoppedSessionsAreDeniedAtOnce() async {
    let bench = Bench()
    await bench.say("in chatter, refactor")
    let session = bench.sessions[0]
    await bench.say("stop chatter")
    let question = [SidecarQuestion(question: "Which?", options: ["a", "b"], multiSelect: false)]
    for id in [session, "s404"] {
        await bench.conductor.handle(.permission(session: id, request: "r1", tool: "Bash",
                                                 input: .object(["command": .string("make")]), reason: nil, blockedPath: nil))
        await bench.conductor.handle(.question(session: id, request: "r2", questions: question))
    }
    // Decided inside handle: nothing was detached.
    #expect(await bench.conductor.running.isEmpty)
    let decisions = bench.sidecar.of("decision")
    #expect(decisions.count == 4 && bench.asks.isEmpty)
    #expect(decisions.allSatisfy { $0["allow"] as? Bool == false && $0["message"] as? String == "That job was stopped." })
    #expect(decisions.map { $0["session"] as? String } == [session, session, "s404", "s404"])
    #expect(decisions.map { $0["request"] as? String } == ["r1", "r2", "r1", "r2"])
}

/// A card answered after its job was stopped still sends its decision (Claude Code may
/// still wait on it) and never marks the place's new job as waiting.
@Test func aCardAnsweredAfterItsJobStoppedLeavesTheNewJobAlone() async throws {
    let bench = Bench()
    await bench.say("in chatter, deploy")
    let old = bench.sessions[0]
    await bench.conductor.handle(.permission(session: old, request: "r1", tool: "Bash",
                                             input: .object(["command": .string("make deploy")]), reason: nil, blockedPath: nil))
    #expect(await eventually { bench.asks.count == 1 })
    await bench.say("stop chatter")
    await bench.say("in chatter, try again")
    let fresh = bench.sessions.last!
    #expect(fresh != old)
    await bench.answer(bench.asks[0], 0)
    await bench.conductor.settled()
    let decision = try #require(bench.sidecar.of("decision").first)
    #expect(decision["session"] as? String == old && decision["allow"] as? Bool == true)
    #expect(await bench.conductor.jobs.map(\.status) == [.stopped, .running])
}

/// Released words are routed off the board's loop: the final caption goes first, and
/// nothing heard means "Sorry, I didn't catch that." with no request sent to Claude;
/// the general session opened as the user started talking only waits, costing nothing.
@Test(arguments: ["", "   ", "...", "\n\t"])
func releasingWithNothingHeardSendsNothing(_ heard: String) async {
    let bench = Bench(heard: heard)
    await bench.conductor.handle(.pttStart)
    await bench.conductor.handle(.pttEnd)
    await bench.conductor.settled()
    #expect(bench.sidecar.of("prompt").isEmpty)
    #expect(bench.sidecar.commands.count == bench.sidecar.of("start").count)
    #expect(bench.sidecar.of("start").count == 1)
    #expect(bench.speaker.lines == ["Sorry, I didn't catch that."])
    #expect(bench.board.messages.contains { if case .caption(_, true) = $0 { true } else { false } })
}

@Test func releasedWorkIsRoutedAfterTheFinalCaption() async {
    let bench = Bench(heard: "in chatter, run the tests")
    await bench.conductor.handle(.pttStart)
    await bench.conductor.handle(.pttEnd)
    await bench.conductor.settled()
    let messages = bench.board.messages
    let caption = messages.firstIndex { if case .caption("in chatter, run the tests", true) = $0 { true } else { false } }
    let thinking = messages.firstIndex { if case .state(.thinking, "chatter", _, _) = $0 { true } else { false } }
    #expect(caption != nil && thinking != nil && caption! < thinking!)
    #expect(bench.speaker.lines == ["Okay, in chatter."])
}

/// Property: many jobs' cards asked at once are shown one at a time; each answer
/// decides its own job's request, matched by session and request, and every job is
/// running again after.
@Test(arguments: 0..<16)
func manyCardsFromManyJobsAreShownInTurnAndEachDecideTheirOwn(_ seed: Int) async throws {
    var random = Seeded(seed)
    let bench = Bench()
    let names = ["codergotchi", "chatter", "atrium", "elysium-main"]
    for name in names { await bench.say("in \(name), deploy") }
    var opened: [(session: String, request: String, command: String)] = []
    for (index, session) in bench.sessions.enumerated() {
        for request in 0..<(1 + random.below(3)) {
            let command = "deploy-\(index)-\(request)"
            opened.append((session, "r\(request)", command))
            await bench.conductor.handle(.permission(session: session, request: "r\(request)", tool: "Bash",
                                                     input: .object(["command": .string(command)]), reason: nil, blockedPath: nil))
        }
    }
    var expected: [String: Bool] = [:]
    defer { Task { await bench.conductor.handle(.gone) } }  // a failure leaves no card waiting out its timeout
    for shown in 1...opened.count {
        try #require(await eventually { bench.asks.count == shown }, "seed \(seed): card \(shown) of \(opened.count)")
        #expect(await bench.conductor.approvals.openCount == 1, "seed \(seed): one card at a time")
        let ask = bench.asks[shown - 1]
        let command = String(ask.detail.prefix { $0 != "\n" })
        let card = try #require(opened.first { $0.command == command }, "seed \(seed)")
        let choice = UInt32(random.below(2))
        expected["\(card.session)/\(card.request)"] = choice == 0
        await bench.answer(ask, choice)
    }
    await bench.conductor.settled()
    let decisions = bench.sidecar.of("decision")
    #expect(decisions.count == opened.count && expected.count == opened.count, "seed \(seed)")
    for decision in decisions {
        let key = "\(decision["session"] as? String ?? "")/\(decision["request"] as? String ?? "")"
        #expect(expected[key] == decision["allow"] as? Bool, "seed \(seed): \(key)")
    }
    #expect(await bench.conductor.approvals.openCount == 0, "seed \(seed)")
    #expect(await bench.conductor.jobs.allSatisfy { $0.status == .running }, "seed \(seed)")
}

/// Cards waiting their turn when the board leaves are unanswered with the one shown,
/// and are never shown to a board that comes back.
@Test func cardsWaitingWhenTheBoardLeavesAreNeverShown() async throws {
    let bench = Bench()
    await bench.say("in chatter, deploy")
    let session = try #require(bench.sessions.first)
    for request in 0..<3 {
        await bench.conductor.handle(.permission(session: session, request: "r\(request)", tool: "Bash",
                                                 input: .object(["command": .string("deploy \(request)")]),
                                                 reason: nil, blockedPath: nil))
    }
    #expect(await eventually { await bench.conductor.approvals.waitingCount == 2 })
    await bench.conductor.handle(.gone)
    await bench.conductor.handle(.connected)
    await bench.conductor.settled()
    #expect(bench.asks.count == 1)
    #expect(bench.sidecar.of("decision").map { $0["allow"] as? Bool } == [false, false, false])
}

// MARK: Failures

/// A failed general job closes the general session, so the next question opens a new
/// one; the failure is said in plain words.
@Test func aFailedGeneralJobReopensTheGeneralSession() async throws {
    let bench = Bench()
    await bench.say("what is a monad")
    let first = try #require(await bench.session(of: .general))
    await bench.conductor.handle(.result(session: first, ok: false, text: "Claude Code process exited with code 1\n{...}",
                                         turns: 0, cost: 0))
    #expect(await eventually { bench.speaker.lines.contains("That didn't work: Claude Code process exited with code 1.") })
    #expect(bench.closes == [first])
    await bench.say("what is a monad")
    #expect(bench.starts.count == 2 && bench.sessions.last != first)
}

private func value(_ object: [String: Any], _ path: String) -> Any? {
    path.split(separator: ".").reduce(object as Any?) { ($0 as? [String: Any])?[String($1)] }
}

/// A recognizer that reports a burst of partial transcripts as soon as it begins.
private final class ChattyTranscriber: Transcribing, @unchecked Sendable {
    let partials: [String]
    init(_ partials: [String]) { self.partials = partials }
    func begin(partial: @escaping @Sendable (String) -> Void) async throws { partials.forEach(partial) }
    func feed(_ samples: [Int16]) async {}
    func finish() async throws -> String { "" }
    func cancel() async {}
}

/// Partial captions are rate-limited (a burst lets the first through, not all of it) and
/// fitted for the board: control characters become spaces, long text is cut.
@Test func partialCaptionsAreRateLimitedAndFitForTheBoard() async throws {
    let board = Board()
    let burst = ["in chat\u{1B}[31m", String(repeating: "word ", count: 200)] + (0..<20).map { "partial \($0)" }
    let conductor = Conductor(board: board, sidecar: Sidecar(transport: RecordingSidecar()), transcriber: ChattyTranscriber(burst),
                              speaker: FakeSpeaker(), resolver: ProjectResolver(roots: [projectRoot([])]))
    await conductor.handle(.pttStart)
    let partials = board.messages.compactMap { if case .caption(let text, false) = $0 { text } else { nil } }
    #expect(!partials.isEmpty && partials.count < burst.count, "\(partials.count) of \(burst.count)")
    #expect(partials.first == "in chat [31m")
    for message in board.messages { #expect((try? LokiCodec.encode(message, seq: 1)) != nil, "\(message)") }
}
