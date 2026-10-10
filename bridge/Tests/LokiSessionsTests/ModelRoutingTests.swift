import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// ADR-018: requests go to Claude, the default, or to a model Ollama runs on the Mac,
// as the user says ("ask Claude to …", "ask Ollama to …") or as the board's Settings
// chose; the board is offered the models and its pick is kept.

/// Ollama as the conductor sees it: answers from a script, optionally held until
/// released, recording what it was asked.
final class FakeModels: LocalModels, @unchecked Sendable {
    private let lock = NSLock()
    private var asked: [(text: String, model: String, conversation: String)] = []
    private var forgotten: [String] = []
    private var warmed: [String] = []
    private var held = false
    private var waiters: [CheckedContinuation<Void, Never>] = []
    let names: [String]
    var reply: @Sendable (String) throws -> String = { "Answer to \($0)" }

    init(names: [String] = ["qwen3.8:27b-mlx", "gemma4:12b-mlx"]) { self.names = names }

    func models() async -> [String] { names }

    func answer(_ text: String, model: String, conversation: String,
                report: @escaping @Sendable (String) async -> Void) async throws -> String {
        let wait = lock.withLock {
            asked.append((text, model, conversation))
            return held
        }
        await report("Searching the web for \(text)")
        if wait {
            await withCheckedContinuation { resume in
                let now = lock.withLock { () -> Bool in
                    if held { waiters.append(resume) }
                    return !held
                }
                if now { resume.resume() }
            }
        }
        try Task.checkCancellation()
        return try reply(text)
    }

    func forget(_ conversation: String) async { lock.withLock { forgotten.append(conversation) } }
    func warm(_ model: String) async { lock.withLock { warmed.append(model) } }

    func hold() { lock.withLock { held = true } }
    func release() {
        let waiting = lock.withLock {
            held = false
            defer { waiters = [] }
            return waiters
        }
        waiting.forEach { $0.resume() }
    }

    var questions: [(text: String, model: String, conversation: String)] { lock.withLock { asked } }
    var forgot: [String] { lock.withLock { forgotten } }
    var warmedModels: [String] { lock.withLock { warmed } }
}

private struct Room {
    let board = Board()
    let speaker = FakeSpeaker()
    let sidecar = RecordingSidecar()
    let clock = TestClock()
    let models: FakeModels
    let store: MemoryModelChoiceStore
    let conductor: Conductor

    init(choice: ModelChoice = ModelChoice(), models: FakeModels = FakeModels(), heard: String = "") {
        self.models = models
        store = MemoryModelChoiceStore(choice)
        let clock = clock
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar), transcriber: FakeTranscriber(text: heard),
                              speaker: speaker, resolver: ProjectResolver(roots: [projectRoot(["codergotchi", "chatter"])]),
                              models: models, choices: store, clock: { clock.now })
    }

    func say(_ text: String) async {
        await conductor.route(text)
        await conductor.speakQueued()
    }

    /// Waits until the Ollama runs have spoken and nothing waits to be said.
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
}

// MARK: Saying who answers

@Test(arguments: [
    ("ask Claude to tell me where they are with my Elysium tasks", Assistant.claude, "tell me where they are with my Elysium tasks"),
    ("Ask Ollama to give me insights on how AI harnesses work", .ollama, "give me insights on how AI harnesses work"),
    ("Hey Loki, ask oh llama what the weather is", .ollama, "what the weather is"),
    ("ask o-llama: is it raining?", .ollama, "is it raining?"),
    ("Okay, ask cloud about the build", .claude, "about the build"),
    ("ask llama to", .ollama, ""),
    ("ASK CLAUDE TO STOP THE SERVER", .claude, "STOP THE SERVER"),
    // What recognizers made of "Ollama" said by macOS voices, and the reliable "local".
    ("Ask Lama what the weather is today.", .ollama, "what the weather is today."),
    ("Ask Ollimar what the weather is today.", .ollama, "what the weather is today."),
    ("Ask O llama what the weather is today.", .ollama, "what the weather is today."),
    ("Ask a llama to explain harnesses", .ollama, "explain harnesses"),
    ("Ask Obama what time it is", .ollama, "what time it is"),
    ("Ask the local model what the weather is today.", .ollama, "what the weather is today."),
    ("ask local about penguins", .ollama, "about penguins"),
])
func askingAnAssistantByNameKeepsTheRestAsSaid(_ text: String, _ who: Assistant, _ rest: String) {
    let request = Requests.classify(text, resolver: ProjectResolver(roots: [projectRoot(["chatter"])]))
    #expect(request == .ask(who, rest))
}

@Test(arguments: ["task Claude with the build", "can you ask Claude to help", "ask the team to review", "Claude, run the tests",
                  "ask Claudette to sing", "stop", "ask", "ask Alan to call", "ask lambda questions", "ask a question",
                  "ask him about llamas", "Ask Colin what the weather is today."])
func otherRequestsDoNotNameAnAssistant(_ text: String) {
    let request = Requests.classify(text, resolver: ProjectResolver(roots: [projectRoot(["chatter"])]))
    if case .ask = request { Issue.record("\(text) named an assistant") }
}

// MARK: The choice and the list

@Test func pickingAModelSetsTheDefaultAndClaudeKeepsTheOllamaModel() {
    var choice = ModelChoice()
    #expect(choice.defaultAssistant == .claude && choice.ollamaModel == "qwen3.8:27b-mlx")
    choice.pick("granite4.1:8b")
    #expect(choice.defaultAssistant == .ollama && choice.defaultModel == "granite4.1:8b" && choice.ollamaModel == "granite4.1:8b")
    choice.pick("Claude")
    #expect(choice.defaultAssistant == .claude && choice.ollamaModel == "granite4.1:8b")
}

@Test func theListIsClaudeThenValidModelsAtMostSixteenWithTheDefaultKept() {
    let many = (1...20).map { "m\($0):x" }
    var choice = ModelChoice()
    var offer = choice.offer(["gpt-oss:20b", "bad name", "gpt-oss:20b", String(repeating: "z", count: 41), "é:1"])
    #expect(offer.names == ["Claude", "gpt-oss:20b"] && offer.current == 0)
    offer = choice.offer(many)
    #expect(offer.names.count == 16 && offer.names.first == "Claude" && offer.names.last == "m15:x")
    choice.pick("m19:x")
    offer = choice.offer(many)
    #expect(offer.names.count == 16 && offer.names.last == "m19:x" && offer.current == 15)
    choice.pick("m3:x")
    offer = choice.offer(many)
    #expect(offer.names[Int(offer.current)] == "m3:x" && offer.names.last == "m15:x")
    // Every list is one the protocol carries.
    #expect(throws: Never.self) { try LokiCodec.encode(.models(names: offer.names, current: offer.current), seq: 1) }
}

@Test func theChoiceIsKeptInAPrivateFileAndABadFileIsTheFirstChoice() throws {
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("loki-models-\(UUID())")
    let store = FileModelChoiceStore(file: folder.appendingPathComponent("models.json"))
    #expect(store.load() == ModelChoice())
    var choice = ModelChoice()
    choice.pick("gemma4:12b-mlx")
    try store.save(choice)
    #expect(store.load() == choice)
    let attributes = try FileManager.default.attributesOfItem(atPath: store.file.path)
    #expect((attributes[.posixPermissions] as? NSNumber)?.intValue == 0o600)
    try store.save(ModelChoice())  // replaced whole
    #expect(store.load() == ModelChoice())
    for bad in ["not json", #"{"defaultModel":"a b","ollamaModel":"x"}"#, #"{"defaultModel":"Claude","ollamaModel":""}"#,
                String(repeating: " ", count: 5000)] {
        try Data(bad.utf8).write(to: store.file)
        #expect(store.load() == ModelChoice(), "\(bad.prefix(40))")
    }
}

@Test func theBoardIsOfferedTheModelsWhenItConnectsAndItsPickIsKept() async throws {
    let room = Room()
    await room.conductor.handle(.connected)
    await room.conductor.settled()
    #expect(room.offers.map(\.names) == [["Claude", "qwen3.8:27b-mlx", "gemma4:12b-mlx"]])
    #expect(room.offers.last?.current == 0)
    await room.conductor.handle(.setModel("gemma4:12b-mlx"))
    #expect(room.offers.last?.current == 2)
    #expect(room.store.load().defaultModel == "gemma4:12b-mlx")
    // Only a name the board was offered counts.
    await room.conductor.handle(.setModel("llama3:8b"))
    await room.conductor.handle(.setModel("claude"))
    // Each refused pick is answered with the list as it stands.
    #expect(room.offers.count == 4 && room.offers.last?.current == 2 && room.store.load().defaultModel == "gemma4:12b-mlx")
    // A default Ollama no longer lists stays on the list, so the board shows it.
    let gone = Room(choice: ModelChoice(defaultModel: "qwen3:8b", ollamaModel: "qwen3:8b"), models: FakeModels(names: []))
    await gone.conductor.handle(.connected)
    await gone.conductor.settled()
    #expect(gone.offers.last?.names == ["Claude", "qwen3:8b"] && gone.offers.last?.current == 1)
}

// MARK: Routing

@Test func askingOllamaAnswersWithTheOllamaModelAndNoClaudeSession() async throws {
    let room = Room()
    await room.say("ask Ollama to give me insights on how AI harnesses work")
    #expect(await room.spoken(["Answer to give me insights on how AI harnesses work"]))
    #expect(room.models.questions.map(\.model) == ["qwen3.8:27b-mlx"])
    #expect(room.sidecar.of("start").isEmpty && room.prompts.isEmpty)
    let job = try #require(await room.conductor.jobs.first)
    #expect(job.place == .ollama("qwen3.8:27b-mlx") && job.status == .done(ok: true))
    // Its search reached the board as a caption.
    #expect(room.board.messages.contains { if case .state(.working, let caption, _, _) = $0 { caption.hasPrefix("Ollama: Searching") } else { false } })
    // A follow-up stays with Ollama, in the same conversation.
    room.clock.advance(30)
    await room.say("and what about agents?")
    #expect(await room.spoken(["Answer to give me insights on how AI harnesses work", "Answer to and what about agents?"]))
    #expect(Set(room.models.questions.map(\.conversation)).count == 1)
}

@Test func askingClaudeGoesToClaudeWhateverTheDefault() async throws {
    let room = Room(choice: ModelChoice(defaultModel: "gemma4:12b-mlx", ollamaModel: "gemma4:12b-mlx"))
    await room.say("ask Claude to tell me where they are with my Elysium tasks")
    #expect(room.prompts == ["tell me where they are with my Elysium tasks"])
    #expect(room.models.questions.isEmpty)
    #expect(await room.conductor.jobs.first?.place == .general)
}

@Test func theDefaultDecidesWhereAQuestionGoesButProjectsStayWithClaude() async throws {
    let room = Room(choice: ModelChoice(defaultModel: "gemma4:12b-mlx", ollamaModel: "gemma4:12b-mlx"))
    await room.say("what is the capital of Peru")
    #expect(await room.spoken(["Answer to what is the capital of Peru"]))
    #expect(room.models.questions.map(\.model) == ["gemma4:12b-mlx"])
    room.clock.advance(300)  // past the follow-up window
    await room.say("in chatter, run the tests")
    #expect(room.prompts == ["in chatter, run the tests"])
    let jobs = await room.conductor.jobs
    #expect(jobs.last.map { if case .project = $0.place { true } else { false } } == true)
    // With Claude the default, a question goes to the general session.
    let claude = Room()
    await claude.say("what is the capital of Peru")
    #expect(claude.prompts == ["what is the capital of Peru"] && claude.models.questions.isEmpty)
}

@Test func stoppingAnOllamaAnswerSaysNothingMoreAndForgetsTheConversation() async throws {
    let models = FakeModels()
    models.hold()
    let room = Room(models: models)
    await room.say("ask ollama to write a long essay")
    #expect(await eventually { models.questions.count == 1 })
    await room.say("what's running")
    #expect(room.speaker.lines.last?.hasPrefix("Ollama") == true)
    await room.say("stop")
    models.release()
    try? await Task.sleep(for: .milliseconds(50))
    await room.conductor.speakQueued()
    #expect(room.speaker.lines.last == "Stopped Ollama.")
    #expect(await room.conductor.jobs.first?.status == .stopped)
    #expect(await eventually { models.forgot.count == 1 })
    #expect(room.sidecar.of("close").isEmpty)  // Claude Code was never involved
}

@Test func anOllamaFailureIsSaidAndClaudeCodeStoppingLeavesOllamaAlone() async throws {
    let models = FakeModels()
    models.reply = { _ in throw LocalHTTP.Failure("Ollama is not answering; is it running?") }
    let room = Room(models: models)
    await room.say("ask ollama what time it is in Tokyo")
    #expect(await eventually {
        await room.conductor.speakQueued()
        return room.speaker.lines.last?.hasPrefix("That didn't work: Ollama is not answering") == true
    })
    let held = FakeModels()
    held.hold()
    let other = Room(models: held)
    await other.say("ask ollama about penguins")
    #expect(await eventually { held.questions.count == 1 })
    await other.conductor.handle(.exited(status: 1))
    held.release()
    #expect(await other.spoken(["Answer to about penguins"]))
}

@Test func holdingToTalkWarmsTheDefaultOllamaModel() async throws {
    let room = Room(choice: ModelChoice(defaultModel: "gemma4:12b-mlx", ollamaModel: "gemma4:12b-mlx"))
    await room.conductor.handle(.pttStart)
    #expect(await eventually { room.models.warmedModels == ["gemma4:12b-mlx"] })
    let claude = Room()
    await claude.conductor.handle(.pttStart)
    try? await Task.sleep(for: .milliseconds(20))
    #expect(claude.models.warmedModels.isEmpty)
}

@Test func ollamasReasonsAreSaidInWords() {
    let load = "llama-server process has terminated: exit status 1: error loading model: key not found in model: gpt-oss.x"
    #expect(OllamaRunner.explained(load, model: "gpt-oss:20b")?.description
            == "Ollama could not load gpt-oss:20b; pick another model in Settings")
    #expect(OllamaRunner.explained("model 'x:1' not found, try pulling it first", model: "x:1")?.description
            == "Ollama does not have x:1")
    #expect(OllamaRunner.explained("out of memory\nmore", model: "m")?.description == "Ollama: out of memory")
    #expect(OllamaRunner.explained(nil, model: "m") == nil && OllamaRunner.explained("", model: "m") == nil)
    // As the job says it.
    #expect(JobBook.failure("Ollama could not load gpt-oss:20b; pick another model in Settings")
            == "That didn't work: Ollama could not load gpt-oss:20b; pick another model in Settings.")
}
