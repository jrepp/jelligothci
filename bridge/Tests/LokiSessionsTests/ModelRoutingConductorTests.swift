import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// The conductor routing between Claude and Ollama (ADR-018): the default, follow-ups
// across the two, status, stop and repeat on Ollama jobs, Ollama's queue, the idle
// refresh, the board's list and pick, warming, and Claude Code's exit.

/// A store whose saves fail, as a full disk would.
private final class FailingStore: ModelChoiceStore, @unchecked Sendable {
    let first: ModelChoice
    init(_ first: ModelChoice = ModelChoice()) { self.first = first }
    func load() -> ModelChoice { first }
    func save(_ choice: ModelChoice) throws { throw CocoaError(.fileWriteOutOfSpace) }
}

/// A model that, once released, reports a search even when its run was cancelled, as
/// OllamaRunner does for each remaining tool call of a round.
private final class LateReporter: LocalModels, @unchecked Sendable {
    private let lock = NSLock()
    private var gate: CheckedContinuation<Void, Never>?
    private var released = false, began = false

    func models() async -> [String] { ["m:1"] }
    func answer(_ text: String, model: String, conversation: String,
                report: @escaping @Sendable (String) async -> Void) async throws -> String {
        await withCheckedContinuation { resume in
            let now = lock.withLock { () -> Bool in
                began = true
                if !released { gate = resume }
                return released
            }
            if now { resume.resume() }
        }
        await report("Searching the web for late news")
        try Task.checkCancellation()
        return "Late answer"
    }
    func forget(_ conversation: String) async {}
    func warm(_ model: String) async {}
    func release() {
        let waiting = lock.withLock { () -> CheckedContinuation<Void, Never>? in
            released = true
            defer { gate = nil }
            return gate
        }
        waiting?.resume()
    }
    var started: Bool { lock.withLock { began } }
}

private struct Lounge {
    let board = Board()
    let speaker = FakeSpeaker()
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let models: FakeModels
    let conductor: Conductor

    init(choice: ModelChoice = ModelChoice(), models: FakeModels = FakeModels(), local: LocalModels? = nil,
         store: ModelChoiceStore? = nil, noOllama: Bool = false) {
        self.models = models
        let clock = clock
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar), transcriber: FakeTranscriber(),
                              speaker: speaker, resolver: ProjectResolver(roots: [projectRoot(["codergotchi", "chatter"])]),
                              models: noOllama ? nil : (local ?? models), choices: store ?? MemoryModelChoiceStore(choice),
                              clock: { clock.now })
    }

    func say(_ text: String) async {
        await conductor.route(text)
        await conductor.speakQueued()
    }

    /// Waits until exactly these lines were spoken.
    func spoken(_ lines: [String]) async -> Bool {
        await eventually {
            await conductor.speakQueued()
            return speaker.lines == lines
        }
    }

    var offers: [(names: [String], current: UInt32)] {
        board.messages.compactMap { if case .models(let names, let current) = $0 { (names, current) } else { nil } }
    }
    var prompts: [String] { sidecar.of("prompt").compactMap { $0["text"] as? String } }
    var asked: [String] { models.questions.map(\.text) }
    var captions: [(LokiState, String)] {
        board.messages.compactMap { if case .state(let state, let caption, _, _) = $0 { (state, caption) } else { nil } }
    }
    func jobs() async -> [Job] { await conductor.jobs }
}

// MARK: Where a request goes

@Test func askingOllamaUsesTheLastOllamaModelPickedWhateverTheDefault() async throws {
    let claude = Lounge(choice: ModelChoice(defaultModel: "Claude", ollamaModel: "gemma4:12b-mlx"))
    await claude.say("ask ollama what is a harness")
    #expect(await claude.spoken(["Answer to what is a harness"]))
    #expect(claude.models.questions.map(\.model) == ["gemma4:12b-mlx"])
    // A plain question with Claude the default goes to Claude, after the follow-up window.
    claude.clock.advance(121)
    await claude.say("what is a harness")
    #expect(claude.prompts == ["what is a harness"] && claude.asked == ["what is a harness"])
}

@Test func anEmptyAskIsAnsweredWithAQuestionAndStartsNoJob() async throws {
    let lounge = Lounge()
    await lounge.say("ask Ollama")
    await lounge.say("ask Claude to")
    await lounge.say("ask the local model")
    #expect(lounge.speaker.lines == ["What should I ask Ollama?", "What should I ask Claude?", "What should I ask Ollama?"])
    #expect(await lounge.jobs().isEmpty && lounge.prompts.isEmpty && lounge.asked.isEmpty)
}

@Test func withoutOllamaAnOllamaRequestSaysSoAndTheBoardIsOfferedNothing() async throws {
    let lounge = Lounge(noOllama: true)
    await lounge.conductor.handle(.connected)
    await lounge.conductor.settled()
    await lounge.say("ask ollama about penguins")
    #expect(lounge.speaker.lines.last == "Ollama is not set up on this bridge.")
    #expect(await lounge.jobs().isEmpty && lounge.offers.isEmpty)
    await lounge.conductor.handle(.setModel("Claude"))
    #expect(lounge.offers.isEmpty)
}

@Test func anAskNamingAProjectGoesWhereItWasAddressed() async throws {
    let lounge = Lounge()
    await lounge.say("ask Claude in chatter, run the tests")
    #expect(lounge.prompts == ["in chatter, run the tests"] && lounge.speaker.lines == ["Okay, in chatter."])
    // Ollama asked by name answers even when a project is named: it works in none.
    await lounge.say("ask Ollama in chatter, what do the tests do")
    #expect(await eventually { lounge.asked == ["in chatter, what do the tests do"] })
}

@Test func followUpsStayWithWhoeverAnsweredLastUntilTheWindowCloses() async throws {
    let lounge = Lounge()
    await lounge.say("ask ollama about tides")
    #expect(await lounge.spoken(["Answer to about tides"]))
    lounge.clock.advance(30)
    await lounge.say("and the moon?")
    #expect(await lounge.spoken(["Answer to about tides", "Answer to and the moon?"]))
    // Claude by name, within the window: Claude, not the Ollama conversation.
    lounge.clock.advance(30)
    await lounge.say("ask Claude about the build")
    #expect(lounge.prompts == ["about the build"])
    // The follow-up to that goes to Claude too, while it runs and after it ends.
    await lounge.say("and the tests")
    #expect(lounge.prompts == ["about the build", "and the tests"])
    let general = try #require(await lounge.jobs().last?.session)
    for _ in 0..<2 { await lounge.conductor.handle(.result(session: general, ok: true, text: "Done.", turns: 1, cost: 0)) }
    lounge.clock.advance(60)
    await lounge.say("anything else")
    #expect(lounge.prompts.last == "anything else" && lounge.asked.count == 2)
}

@Test func withAnOllamaDefaultAClaudeAnswerStillTakesItsFollowUp() async throws {
    let lounge = Lounge(choice: ModelChoice(defaultModel: "gemma4:12b-mlx", ollamaModel: "gemma4:12b-mlx"))
    await lounge.say("ask Claude what changed")
    let general = try #require(await lounge.jobs().last?.session)
    await lounge.conductor.handle(.result(session: general, ok: true, text: "Two files.", turns: 1, cost: 0))
    lounge.clock.advance(60)
    await lounge.say("which ones")
    #expect(lounge.prompts == ["what changed", "which ones"] && lounge.asked.isEmpty)
    await lounge.conductor.handle(.result(session: general, ok: true, text: "a and b.", turns: 1, cost: 0))
    lounge.clock.advance(121)
    await lounge.say("what is the capital of Peru")
    #expect(await eventually { lounge.models.questions.map(\.model) == ["gemma4:12b-mlx"] })
}

@Test func withClaudeAndOllamaBothBusyAPlainRequestAsksWhichAndAskClaudeDoesNot() async throws {
    let models = FakeModels()
    models.hold()
    let lounge = Lounge(models: models)
    await lounge.say("ask Claude to check the build")
    await lounge.say("ask ollama about penguins")
    #expect(await eventually { models.questions.count == 1 })
    let routing = Task { await lounge.say("and also the weather") }
    let card = try #require(await lounge.board.lastAsk())
    #expect(card.title == "For which one?" && card.options == ["Ollama", "General"])
    await lounge.conductor.handle(.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 0))
    await routing.value
    // Claude by name skips the Ollama job: no second card, straight to the general session.
    await lounge.say("ask Claude about the logs")
    #expect(lounge.board.messages.filter { if case .ask = $0 { true } else { false } }.count == 1)
    #expect(lounge.prompts == ["check the build", "about the logs"])
    models.release()
    #expect(await eventually { lounge.asked == ["about penguins", "and also the weather"] })
}

// MARK: Ollama jobs

@Test func statusStopAndRepeatWorkOnAnOllamaJob() async throws {
    let models = FakeModels()
    models.hold()
    let lounge = Lounge(models: models)
    await lounge.say("ask ollama to write an essay")
    #expect(await eventually { models.questions.count == 1 })
    lounge.clock.advance(5)
    await lounge.say("what's running")
    #expect(lounge.speaker.lines.last == "Ollama is on: Searching the web for write an essay, for 5 seconds.")
    await lounge.say("stop chatter")
    #expect(lounge.speaker.lines.last == "Nothing is running.")
    #expect(await lounge.jobs().first?.status == .running)
    models.release()
    #expect(await eventually {
        await lounge.conductor.speakQueued()
        return lounge.speaker.lines.last == "Answer to write an essay"
    })
    await lounge.say("say that again")
    #expect(lounge.speaker.lines.last == "Ollama: Answer to write an essay")
    await lounge.say("what's running")
    #expect(lounge.speaker.lines.last == "Nothing is running. Ollama finished: Answer to write an essay")
}

@Test func twoOllamaRequestsAreAnsweredInTheOrderAskedOneAtATime() async throws {
    let models = FakeModels()
    models.hold()
    let lounge = Lounge(models: models)
    await lounge.say("ask ollama first")
    await lounge.say("ask ollama second")
    #expect(await eventually { models.questions.count == 1 })
    try? await Task.sleep(for: .milliseconds(50))
    #expect(models.questions.count == 1)  // the second waits for the first
    let jobs = await lounge.jobs()
    #expect(jobs.count == 2 && jobs.allSatisfy { $0.status == .running } && Set(jobs.map(\.session)).count == 1)
    models.release()
    #expect(await lounge.spoken(["Answer to first", "Answer to second"]))
    #expect(lounge.asked == ["first", "second"])
    #expect(await eventually { await lounge.conductor.ollamaRuns.isEmpty })
    #expect(await lounge.jobs().map(\.status) == [.done(ok: true), .done(ok: true)])
}

@Test func stoppingEndsTheRunningAndTheQueuedOllamaRequests() async throws {
    let models = FakeModels()
    models.hold()
    let lounge = Lounge(models: models)
    await lounge.say("ask ollama first")
    await lounge.say("ask ollama second")
    #expect(await eventually { models.questions.count == 1 })
    let session = try #require(await lounge.jobs().first?.session)
    await lounge.say("stop")
    models.release()
    try? await Task.sleep(for: .milliseconds(100))
    await lounge.conductor.speakQueued()
    #expect(lounge.speaker.lines == ["Stopped Ollama."])
    #expect(models.questions.count == 1)  // the queued one never reached the model
    #expect(await lounge.jobs().map(\.status) == [.stopped, .stopped])
    #expect(await eventually { models.forgot == [session] })
    #expect(await lounge.conductor.ollamaRuns.isEmpty)
    #expect(lounge.sidecar.of("close").isEmpty)
    // The next request opens a new conversation.
    await lounge.say("ask ollama third")
    #expect(await eventually { models.questions.last?.text == "third" })
    #expect(models.questions.last?.conversation != session)
}

@Test func theBoardsCancelStopsTheLatestOllamaJob() async throws {
    let models = FakeModels()
    models.hold()
    let lounge = Lounge(models: models)
    await lounge.say("ask ollama a long question")
    #expect(await eventually { models.questions.count == 1 })
    await lounge.conductor.handle(.cancel)
    #expect(await lounge.jobs().first?.status == .stopped)
    #expect(await eventually { models.forgot.count == 1 })
    models.release()
    try? await Task.sleep(for: .milliseconds(50))
    await lounge.conductor.speakQueued()
    #expect(lounge.speaker.lines.isEmpty)
}

@Test func aFailureIsSaidAndTheQueuedRequestStillRunsInTheSameConversation() async throws {
    let models = FakeModels()
    models.reply = { text in
        if text == "bad" { throw LocalHTTP.Failure("Ollama could not load m; pick another model in Settings") }
        return "Answer to \(text)"
    }
    models.hold()
    let lounge = Lounge(models: models)
    await lounge.say("ask ollama bad")
    await lounge.say("ask ollama good")
    models.release()
    #expect(await lounge.spoken(["That didn't work: Ollama could not load m; pick another model in Settings.", "Answer to good"]))
    #expect(Set(models.questions.map(\.conversation)).count == 1 && models.forgot.isEmpty)
    // A failure with nothing queued ends the conversation, so the next starts afresh.
    await lounge.say("ask ollama bad")
    #expect(await eventually { models.forgot.count == 1 })
    await lounge.say("ask ollama after")
    #expect(await eventually { models.questions.last?.text == "after" })
    #expect(models.questions.last?.conversation != models.questions.first?.conversation)
}

@Test func anIdleOllamaConversationStartsAfreshAfterTenMinutes() async throws {
    let lounge = Lounge()
    await lounge.say("ask ollama one")
    #expect(await lounge.spoken(["Answer to one"]))
    lounge.clock.advance(Conductor.ollamaIdleSeconds - 1)
    await lounge.say("ask ollama two")
    #expect(await lounge.spoken(["Answer to one", "Answer to two"]))
    lounge.clock.advance(Conductor.ollamaIdleSeconds + 1)
    await lounge.say("ask ollama three")
    #expect(await lounge.spoken(["Answer to one", "Answer to two", "Answer to three"]))
    let conversations = lounge.models.questions.map(\.conversation)
    #expect(conversations[0] == conversations[1] && conversations[2] != conversations[1])
    #expect(await eventually { lounge.models.forgot == [conversations[0]] })
}

@Test func claudeCodeExitingStopsClaudesJobsAndLeavesOllamasRunning() async throws {
    let models = FakeModels()
    models.hold()
    let lounge = Lounge(models: models)
    await lounge.say("ask Claude to check the build")
    await lounge.say("ask ollama about penguins")
    #expect(await eventually { models.questions.count == 1 })
    await lounge.conductor.handle(.exited(status: 1))
    await lounge.conductor.speakQueued()
    #expect(lounge.speaker.lines == ["Claude Code stopped, and with it General."])
    #expect(await lounge.jobs().map(\.status) == [.stopped, .running])
    models.release()
    #expect(await lounge.spoken(["Claude Code stopped, and with it General.", "Answer to about penguins"]))
}

/// A run that reports a search after it was stopped: the board, already idle, should
/// not flip back to "working" with the stopped job's caption.
@Test func aStoppedOllamaJobsLateSearchDoesNotReachTheBoard() async throws {
    let late = LateReporter()
    let lounge = Lounge(local: late)
    await lounge.say("ask ollama the news")
    #expect(await eventually { late.started })
    await lounge.say("stop")
    let before = lounge.captions.count
    late.release()
    try? await Task.sleep(for: .milliseconds(100))
    let after = lounge.captions.dropFirst(before)
    #expect(await lounge.jobs().first?.status == .stopped)
    #expect(!after.contains { $0.0 == .working && $0.1.contains("late news") })
    
}

/// Load: many requests in one conversation are all answered, in order, and the
/// conductor keeps no run once they are done.
@Test func manyQueuedOllamaRequestsAreAnsweredInOrderAndLeaveNothingBehind() async throws {
    let models = FakeModels()
    models.hold()
    let lounge = Lounge(models: models)
    let count = 30
    for index in 0..<count { await lounge.say("ask ollama q\(index)") }
    #expect(await lounge.conductor.ollamaRuns.values.first?.count == count)
    models.release()
    #expect(await lounge.spoken((0..<count).map { "Answer to q\($0)" }))
    #expect(lounge.asked == (0..<count).map { "q\($0)" })
    #expect(await eventually { await lounge.conductor.ollamaRuns.isEmpty })
    #expect(await lounge.conductor.ollamaActive.count == 1)
}

// MARK: The board's list

@Test func aPickBeforeAnyListOrNotOnItChangesNothing() async throws {
    let lounge = Lounge()
    await lounge.conductor.handle(.setModel("qwen3.8:27b-mlx"))
    #expect(lounge.offers.isEmpty && lounge.models.warmedModels.isEmpty)
    await lounge.conductor.handle(.connected)
    await lounge.conductor.settled()
    for wrong in ["", "CLAUDE", "qwen3.8:27b", "gemma4:12b-mlx ", "llama3:8b"] { await lounge.conductor.handle(.setModel(wrong)) }
    // Each refused pick is answered with the list as it was, so the board shows the truth.
    #expect(lounge.offers.count == 6 && lounge.offers.allSatisfy { $0.current == 0 })
    await lounge.say("what is two plus two")
    #expect(lounge.prompts == ["what is two plus two"])  // still Claude
}

@Test func theListLeavesOutNamesTheBoardCannotShowAndIsSentOnEveryConnect() async throws {
    let lounge = Lounge(models: FakeModels(names: ["good:1", "bad name", "good:1", "Claude", "é:1", "fine/two:3"]))
    await lounge.conductor.handle(.connected)
    await lounge.conductor.settled()
    await lounge.conductor.handle(.connected)
    await lounge.conductor.settled()
    #expect(lounge.offers.map(\.names) == [["Claude", "good:1", "fine/two:3"], ["Claude", "good:1", "fine/two:3"]])
}

@Test func pickingAModelWarmsItConfirmsItAndRoutesToIt() async throws {
    let lounge = Lounge()
    await lounge.conductor.handle(.connected)
    await lounge.conductor.settled()
    await lounge.conductor.handle(.setModel("gemma4:12b-mlx"))
    #expect(lounge.offers.last?.current == 2)
    #expect(await eventually { lounge.models.warmedModels == ["gemma4:12b-mlx"] })
    await lounge.say("what is the tallest tree")
    #expect(await eventually { lounge.models.questions.map(\.model) == ["gemma4:12b-mlx"] })
    // Holding to talk within a minute does not warm it again; after a minute it does.
    await lounge.conductor.handle(.pttStart)
    try? await Task.sleep(for: .milliseconds(30))
    #expect(lounge.models.warmedModels == ["gemma4:12b-mlx"])
    await lounge.conductor.handle(.cancel)
    lounge.clock.advance(61)
    await lounge.conductor.handle(.pttStart)
    #expect(await eventually { lounge.models.warmedModels == ["gemma4:12b-mlx", "gemma4:12b-mlx"] })
    await lounge.conductor.handle(.cancel)
    // Back to Claude: nothing is warmed, and "ask Ollama" still uses the model picked.
    await lounge.conductor.handle(.setModel("Claude"))
    #expect(lounge.offers.last?.current == 0)
    await lounge.conductor.handle(.pttStart)
    await lounge.conductor.handle(.cancel)
    try? await Task.sleep(for: .milliseconds(30))
    #expect(lounge.models.warmedModels.count == 2)
    lounge.clock.advance(200)
    await lounge.say("ask ollama about cedars")
    #expect(await eventually { lounge.models.questions.last?.model == "gemma4:12b-mlx" })
}

@Test func aPickThatCannotBeSavedIsSaidAndStillTakesEffect() async throws {
    let lounge = Lounge(store: FailingStore())
    await lounge.conductor.handle(.connected)
    await lounge.conductor.settled()
    await lounge.conductor.handle(.setModel("gemma4:12b-mlx"))
    #expect(lounge.captions.contains { $0.0 == .failure && $0.1 == "Model choice not saved" })
    #expect(lounge.offers.last?.current == 2)
    await lounge.say("what is the tallest tree")
    #expect(await eventually { lounge.models.questions.map(\.model) == ["gemma4:12b-mlx"] })
}
