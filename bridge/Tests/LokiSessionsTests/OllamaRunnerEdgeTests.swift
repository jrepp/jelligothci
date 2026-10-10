import Foundation
import Testing
@testable import LokiSessions

// OllamaRunner beyond the happy path (ADR-018): tool calls in every shape a model
// sends, tool failures handed back as text, the round cap, cancellation mid-run, the
// conversation's trimming, the think parameter per model, and Ollama's errors in words.

/// One Ollama and one MCP server on hosts of their own: `chat` answers /api/chat with
/// the request body and the call's number (0, 1, …).
private struct Rig {
    let ollama: String
    let mcp: String
    let runner: OllamaRunner
    let chats = Counter()

    init(capabilities: [String] = ["completion", "tools"], server: ScriptedMCP? = ScriptedMCP(),
         chat: @escaping @Sendable (JSONValue?, Int) -> LocalWire.Reply) {
        let chats = chats
        mcp = LocalWire.host { seen in server?.reply(seen) ?? LocalWire.Reply(refuse: true) }
        ollama = LocalWire.host { seen in
            switch seen.path {
            case "/api/show": return .json(["capabilities": capabilities])
            case "/api/chat": return chat(seen.body, chats.next())
            default: return .text("{}", status: 404)
            }
        }
        let session = LocalWire.session()
        runner = OllamaRunner(base: URL(string: "http://\(ollama)")!,
                              search: server == nil ? nil : MCPClient(endpoint: URL(string: "http://\(mcp)/mcp")!, session: session),
                              session: session)
    }

    var chatBodies: [JSONValue] { LocalWire.requests(ollama).filter { $0.path == "/api/chat" }.compactMap(\.body) }
    var mcpCalls: [LocalWire.Seen] { LocalWire.requests(mcp).filter { $0.method == "tools/call" } }

    func ask(_ text: String, model: String = "m:1", in conversation: String = "c1", reports: Lines = Lines()) async throws -> String {
        try await runner.answer(text, model: model, conversation: conversation) { reports.add($0) }
    }
}

private func assistant(_ content: String, calls: [[String: Any]] = []) -> LocalWire.Reply {
    var message: [String: Any] = ["role": "assistant", "content": content]
    if !calls.isEmpty { message["tool_calls"] = calls }
    return .json(["message": message])
}

private func messages(_ chat: JSONValue) -> [JSONValue] {
    if case .array(let list)? = chat["messages"] { return list }
    return []
}

// MARK: Tool calls

@Test func toolArgumentsAsAnObjectReachTheServerAndAsAStringAreForwardedAsGiven() async throws {
    let rig = Rig { _, n in
        switch n {
        case 0: return assistant("", calls: [["function": ["name": "brave_web_search", "arguments": ["query": "tides"]]]])
        case 1: return assistant("", calls: [["function": ["name": "brave_web_search", "arguments": #"{"query":"moon"}"#]]])
        case 2: return assistant("", calls: [["function": ["name": "brave_news_search"]]])
        default: return assistant("Done.")
        }
    }
    let reports = Lines()
    #expect(try await rig.ask("tides and the moon", reports: reports) == "Done.")
    #expect(reports.all == ["Searching the web for tides", "Searching the web", "Searching the news"])
    let arguments = rig.mcpCalls.map { $0.body?["params"]?["arguments"] }
    #expect(arguments.count == 3)
    #expect(arguments[0] == .object(["query": .string("tides")]))
    // Ollama's API always sends an object; a string from elsewhere goes on unparsed, and
    // missing arguments are an empty object.
    #expect(arguments[1] == .string(#"{"query":"moon"}"#))
    #expect(arguments[2] == .object([:]))
    // Each tool's result is answered under the tool's name, after the call it answers.
    let last = messages(rig.chatBodies[3])
    #expect(last.map { $0["role"]?.string ?? "" } == ["system", "user", "assistant", "tool", "assistant", "tool", "assistant", "tool"])
    #expect(last[3]["tool_name"]?.string == "brave_web_search" && last[3]["content"]?.string == "Results for tides")
    #expect(last[7]["tool_name"]?.string == "brave_news_search")
}

@Test func aCallWithoutANameOrToAnUnknownToolIsAnsweredWithTheServersRefusal() async throws {
    var server = ScriptedMCP()
    server.call = { seen in
        let name = seen.body?["params"]?["name"]?.string ?? ""
        return name.hasPrefix("brave_") ? ["result": ["content": [["type": "text", "text": "ok"]]]]
            : ["error": ["code": -32602, "message": "Unknown tool: \(name)"]]
    }
    let rig = Rig(server: server) { _, n in
        switch n {
        case 0: return assistant("", calls: [["function": ["arguments": ["query": "x"]]], ["function": ["name": "run_shell"]]])
        default: return assistant("I could not search.")
        }
    }
    let reports = Lines()
    #expect(try await rig.ask("search", reports: reports) == "I could not search.")
    #expect(reports.all == ["Searching the web for x", "Searching the web"])
    let tools = messages(rig.chatBodies[1]).filter { $0["role"]?.string == "tool" }
    #expect(tools.map { $0["tool_name"]?.string } == ["", "run_shell"])
    // Only Brave's search tools are called: anything else is refused without asking the server.
    #expect(tools.map { $0["content"]?.string } == ["No such tool.", "No such tool."])
    #expect(rig.mcpCalls.allSatisfy { $0.body?["method"]?.string != "tools/call" })
}

@Test func aToolsFailureAndAServerThatStopsMidRunReachTheModelAsText() async throws {
    let calls = Counter()
    var server = ScriptedMCP()
    server.call = { _ in
        calls.next() == 0 ? ["result": ["content": [["type": "text", "text": "rate limited"]], "isError": true]]
            : ["result": ["content": [["type": "text", "text": "fine"]]]]
    }
    let rig = Rig(server: server) { _, n in
        n < 2 ? assistant("", calls: [["function": ["name": "brave_web_search", "arguments": ["query": "q\(n)"]]]])
            : assistant("Answer.")
    }
    #expect(try await rig.ask("q") == "Answer.")
    let tools = messages(rig.chatBodies[2]).filter { $0["role"]?.string == "tool" }.compactMap { $0["content"]?.string }
    #expect(tools == ["The tool failed: rate limited", "fine"])
    // The MCP server gone mid-run (it listed its tools, then stopped): the model reads
    // why and still answers.
    var stopped = ScriptedMCP()
    stopped.refuseCalls = true
    let gone = Rig(server: stopped) { _, n in
        n == 0 ? assistant("", calls: [["function": ["name": "brave_web_search", "arguments": ["query": "x"]]]]) : assistant("Paris.")
    }
    #expect(try await gone.ask("capital of France") == "Paris.")
    #expect(messages(gone.chatBodies[1]).last?["content"]?.string
            == "The search failed: Brave Search Bridge is not answering; is it running?")
    // Without the server at all, no tools are offered and the model still answers.
    let none = Rig(server: nil) { _, _ in assistant("Paris.") }
    #expect(try await none.ask("capital of France") == "Paris.")
    #expect(none.chatBodies.allSatisfy { $0["tools"] == nil })
}

@Test func aToolsTextIsCutToTheLimitBeforeTheModelReadsIt() async throws {
    var server = ScriptedMCP()
    server.call = { _ in ["result": ["content": [["type": "text", "text": String(repeating: "é", count: 20_000)]]]] }
    let rig = Rig(server: server) { _, n in
        n == 0 ? assistant("", calls: [["function": ["name": "brave_web_search", "arguments": ["query": "x"]]]]) : assistant("Ok.")
    }
    _ = try await rig.ask("x")
    let tool = try #require(messages(rig.chatBodies[1]).last?["content"]?.string)
    #expect(tool.count == OllamaRunner.maxToolText && tool.allSatisfy { $0 == "é" })
}

@Test func aRoundMakesAtMostFourSearchesAndTheRestAreToldToAnswer() async throws {
    let rig = Rig { _, n in
        n == 0 ? assistant("", calls: (0..<12).map { ["function": ["name": "brave_web_search", "arguments": ["query": "q\($0)"]]] })
            : assistant("Twelve searches.")
    }
    let reports = Lines()
    #expect(try await rig.ask("everything", reports: reports) == "Twelve searches.")
    #expect(reports.all == (0..<4).map { "Searching the web for q\($0)" })
    #expect(rig.mcpCalls.compactMap { $0.body?["params"]?["arguments"]?["query"]?.string } == (0..<4).map { "q\($0)" })
    let tools = messages(rig.chatBodies[1]).filter { $0["role"]?.string == "tool" }.compactMap { $0["content"]?.string }
    #expect(tools.count == 12 && tools.dropFirst(4).allSatisfy { $0.hasPrefix("Search limit reached") })
}

// MARK: The round cap

@Test func theLastRoundOffersNoToolsAndAModelThatStillCallsOneAnswersWithItsText() async throws {
    let calling = Rig { _, _ in
        assistant("Here is what I have.", calls: [["function": ["name": "brave_web_search", "arguments": ["query": "more"]]]])
    }
    #expect(try await calling.ask("loop") == "Here is what I have.")
    // Six rounds search; the seventh has no tools and must answer.
    #expect(calling.chatBodies.count == OllamaRunner.maxRounds + 1)
    #expect(calling.chatBodies.prefix(OllamaRunner.maxRounds).allSatisfy { $0["tools"] != nil })
    #expect(calling.chatBodies.last?["tools"] == nil)
    #expect(calling.mcpCalls.count == OllamaRunner.maxRounds)
    // With nothing to say in that round it is a failure, not silence.
    let silent = Rig { _, _ in assistant("", calls: [["function": ["name": "brave_web_search", "arguments": ["query": "x"]]]]) }
    do {
        _ = try await silent.ask("loop", model: "q:1")
        Issue.record("an empty last round was spoken")
    } catch {
        #expect("\(error)" == "q:1 gave no answer")
    }
    // A model whose tool calls come while no tools were offered is not searched for.
    let unoffered = Rig(server: nil) { _, _ in
        assistant("Plain.", calls: [["function": ["name": "brave_web_search", "arguments": ["query": "x"]]]])
    }
    #expect(try await unoffered.ask("x") == "Plain." && unoffered.chatBodies.count == 1)
}

// MARK: Cancellation

@Test func cancellingMidRunEndsTheAnswerPromptlyAndTheConversationKeepsNothingOfIt() async throws {
    let rig = Rig { _, n in
        var reply = assistant("Answer \(n).")
        if n == 0 { reply.delay = 5 }
        return reply
    }
    let run = Task { try await rig.ask("slow question") }
    #expect(await eventually { rig.chatBodies.count == 1 })
    let start = ContinuousClock.now
    run.cancel()
    let outcome = await run.result
    #expect(ContinuousClock.now - start < .seconds(2))
    switch outcome {
    case .success(let text): Issue.record("a cancelled run answered \(text)")
    case .failure: break
    }
    // The next question in the conversation starts from the instructions alone.
    #expect(try await rig.ask("next question") == "Answer 1.")
    #expect(messages(rig.chatBodies[1]).map { $0["role"]?.string ?? "" } == ["system", "user"])
    #expect(messages(rig.chatBodies[1]).last?["content"]?.string == "next question")
}

@Test func aRunCancelledBeforeItStartsAsksNothingOfTheModel() async throws {
    let rig = Rig { _, _ in assistant("Never.") }
    let run = Task {
        withUnsafeCurrentTask { $0?.cancel() }
        return try await rig.ask("q")
    }
    await #expect(throws: (any Error).self) { try await run.value }
    #expect(rig.chatBodies.isEmpty)
}

@Test func aFailedRunLeavesTheConversationAsItWas() async throws {
    let rig = Rig { _, n in n == 1 ? .json(["error": "boom"]) : assistant("A\(n).") }
    #expect(try await rig.ask("one") == "A0.")
    await #expect(throws: LocalHTTP.Failure.self) { try await rig.ask("two") }
    #expect(try await rig.ask("three") == "A2.")
    let users = messages(rig.chatBodies[2]).filter { $0["role"]?.string == "user" }.compactMap { $0["content"]?.string }
    #expect(users == ["one", "three"])
}

// MARK: Thinking and capabilities

@Test(arguments: [
    (["completion", "tools", "thinking"], "gpt-oss:20b", JSONValue.string("low")),
    (["completion", "tools", "thinking"], "gpt-oss-safeguard:20b", .string("low")),
    (["completion", "tools", "thinking"], "qwen3.8:27b-mlx", .bool(false)),
    (["completion", "tools", "thinking"], "GPT-OSS:20b", .bool(false)),
    (["completion", "tools"], "gpt-oss:20b", nil),
    (["completion", "tools"], "granite4.1:8b", nil),
] as [([String], String, JSONValue?)])
func thinkingIsAsLittleAsEachModelAllows(_ capabilities: [String], _ model: String, _ think: JSONValue?) async throws {
    let rig = Rig(capabilities: capabilities) { _, _ in assistant("Ok.") }
    _ = try await rig.ask("q", model: model)
    #expect(rig.chatBodies.first?["think"] == think)
    #expect(rig.chatBodies.first?["stream"] == .bool(false) && rig.chatBodies.first?["model"]?.string == model)
}

@Test func aModelsCapabilitiesAreAskedOnceAndAFailedAskIsTriedAgain() async throws {
    let shows = Counter()
    let host = LocalWire.host { seen in
        switch seen.path {
        case "/api/show": return shows.next() == 0 ? .text("{}", status: 500) : .json(["capabilities": ["tools", "thinking"]])
        default: return assistant("Ok.")
        }
    }
    let runner = OllamaRunner(base: URL(string: "http://\(host)")!, search: nil, session: LocalWire.session())
    #expect(await runner.features(of: "m:1") == [])
    #expect(await runner.features(of: "m:1") == ["tools", "thinking"])
    #expect(await runner.features(of: "m:1") == ["tools", "thinking"])
    #expect(LocalWire.requests(host).filter { $0.path == "/api/show" }.count == 2)
    #expect(LocalWire.requests(host).first?.body?["model"]?.string == "m:1")
}

@Test func theModelListSkipsRemoteAndNamelessModelsAndSurvivesOddTags() async throws {
    let host = LocalWire.host { seen in
        switch seen.path {
        case "/api/tags":
            return .json(["models": [
                ["name": "b:1", "modified_at": "2026-10-02T00:00:00-07:00"],
                ["modified_at": "2026-10-09T00:00:00Z"],
                ["name": "nulled:1", "modified_at": "2026-10-03T00:00:00Z", "remote_host": NSNull()],
                ["name": "a:1"],
                ["name": "c:1", "modified_at": "2026-10-05T00:00:00Z"],
            ]])
        case "/api/show": return .json(["capabilities": ["tools"]])
        default: return .text("{}", status: 404)
        }
    }
    let runner = OllamaRunner(base: URL(string: "http://\(host)")!, search: nil, session: LocalWire.session())
    // Newest first by the date as written; a model without a date sorts last. A
    // remote_host that is null still counts as remote.
    #expect(await runner.models() == ["c:1", "b:1", "a:1"])
    for odd in [#"{"models":"none"}"#, "[]", "not json", #"{"other":[]}"#] {
        let bad = LocalWire.host { _ in .text(odd) }
        #expect(await OllamaRunner(base: URL(string: "http://\(bad)")!, search: nil, session: LocalWire.session()).models() == [])
    }
}

@Test func warmingLoadsTheModelWithNoMessagesAndIgnoresFailures() async throws {
    let host = LocalWire.host { _ in .text("{}", status: 500) }
    let runner = OllamaRunner(base: URL(string: "http://\(host)")!, search: nil, session: LocalWire.session())
    await runner.warm("gemma4:12b-mlx")
    let seen = try #require(LocalWire.requests(host).first)
    #expect(seen.path == "/api/chat" && seen.body?["model"]?.string == "gemma4:12b-mlx" && seen.body?["messages"] == .array([]))
}

// MARK: Ollama's errors

@Test(arguments: [
    (LocalWire.Reply.text(#"{"error":"model \"x:1\" not found, try pulling it first"}"#, status: 404), "Ollama does not have x:1"),
    (.text(#"{"error":"llama-server process has terminated: exit status 2"}"#, status: 500),
     "Ollama could not load x:1; pick another model in Settings"),
    (.text(#"{"error":"error loading model: key not found in model: x.y"}"#, status: 500),
     "Ollama could not load x:1; pick another model in Settings"),
    (.text(#"{"error":"model key not found"}"#, status: 500), "Ollama: model key not found"),
    (.text("Internal Server Error", status: 500, type: "text/plain"), "Ollama refused the request (500)"),
    (.text(#"{"error":""}"#, status: 500), "Ollama refused the request (500)"),
    (.text(#"{"error":"out of memory\nat line 2"}"#), "Ollama: out of memory"),
    (.text("not json"), "Ollama sent a reply that is not a JSON object"),
    (.text(#"{"done":true}"#), "Ollama sent no message"),
    (.json(["message": ["role": "assistant", "content": ""]]), "x:1 gave no answer"),
    (.json(["message": ["role": "assistant", "content": "**`|~_**"]]), "x:1 gave no answer"),
    (LocalWire.Reply(refuse: true), "Ollama is not answering; is it running?"),
] as [(LocalWire.Reply, String)])
func ollamasFailuresAreSaidInWords(_ reply: LocalWire.Reply, _ said: String) async {
    let rig = Rig(server: nil) { _, _ in reply }
    do {
        let text = try await rig.ask("q", model: "x:1")
        Issue.record("answered \(text)")
    } catch {
        #expect("\(error)" == said)
    }
}

@Test func aLongReasonIsCutToItsFirstLineAndAFewWords() {
    let long = String(repeating: "word ", count: 60) + "\nsecond line"
    let said = OllamaRunner.explained(long, model: "m")?.description ?? ""
    #expect(said == "Ollama: " + String(long.prefix(120)) && !said.contains("second"))
    #expect(OllamaRunner.explained("\n\nlater", model: "m")?.description == "Ollama: later")
}

// MARK: Pieces: properties

/// Property: the kept conversation is the instructions, then a suffix of the messages
/// beginning at a user's, never longer than the limit, and trimming twice changes nothing.
@Test(arguments: 0..<200)
func trimmingKeepsInstructionsAndARecentSuffixFromAQuestion(_ seed: Int) {
    var random = Seeded(seed)
    var all: [JSONValue] = [OllamaRunner.message("system", "rules")]
    for index in 0..<random.below(120) {
        all.append(OllamaRunner.message(random.pick(["user", "user", "assistant", "tool", "tool"]), "m\(index)"))
    }
    let kept = OllamaRunner.trimmed(all)
    #expect(kept.first == all.first, "seed \(seed)")
    #expect(OllamaRunner.trimmed(kept) == kept, "seed \(seed)")
    if all.count <= OllamaRunner.maxMessages + 1 {
        #expect(kept == all, "seed \(seed)")
        return
    }
    #expect(kept.count <= OllamaRunner.maxMessages + 1, "seed \(seed)")
    #expect(Array(all.suffix(kept.count - 1)) == Array(kept.dropFirst()), "seed \(seed)")
    if kept.count > 1 { #expect(kept[1]["role"]?.string == "user", "seed \(seed)") }
    // Nothing is dropped that could have been kept: the message before the kept ones
    // either falls outside the limit or is not a user's.
    let window = all.dropFirst().suffix(OllamaRunner.maxMessages)
    let firstUser = window.firstIndex { $0["role"]?.string == "user" }
    #expect(kept.count - 1 == (firstUser.map { window.endIndex - $0 } ?? 0), "seed \(seed)")
}

/// The current question is kept even when its answer took more than the limit's worth
/// of tool messages (40), so a follow-up has its context.
@Test func aSearchHeavyAnswerKeepsNoHistoryOfItsQuestion() {
    var all: [JSONValue] = [OllamaRunner.message("system", "rules"), OllamaRunner.message("user", "question")]
    for _ in 0..<6 {
        all.append(OllamaRunner.message("assistant", ""))
        for _ in 0..<7 { all.append(OllamaRunner.message("tool", "t")) }
    }
    all.append(OllamaRunner.message("assistant", "answer"))
    let kept = OllamaRunner.trimmed(all)
    #expect(kept.first == OllamaRunner.message("system", "rules"))
    #expect(kept.count > 1 && kept[1] == OllamaRunner.message("user", "question"))
    
}

/// Property: speakable text has no links, markdown marks or doubled whitespace, is at
/// most 900 characters, and is the same twice.
@Test(arguments: 0..<200)
func speakableTextIsPlainBoundedAndDeterministic(_ seed: Int) {
    var random = Seeded(seed)
    let pieces = ["word", "Sentence.", "Why?", " ", "  ", "\n", "\t", "**bold**", "_it_", "`code`", "~x~", "| a | b |", "# Head\n",
                  "- item\n", "1. first\n", "• dot\n", "[link](https://a.b/c)", "https://x.y/z?q=1", "http://h", "é", "😀",
                  "👩‍💻", "e\u{301}", "中文", "!", "...", "(paren)", "[bracket]", "2 * 3"]
    var text = ""
    for _ in 0..<random.below(random.chance(20) ? 600 : 60) { text += random.pick(pieces) + random.pick(["", " "]) }
    let said = OllamaRunner.speakable(text)
    #expect(said == OllamaRunner.speakable(text), "seed \(seed)")
    #expect(said.count <= 900, "seed \(seed)")
    #expect(!said.contains("http://") && !said.contains("https://"), "seed \(seed): \(said)")
    #expect(!said.contains { "*_`~|".contains($0) }, "seed \(seed): \(said)")
    #expect(!said.contains("  ") && !said.contains("\n") && !said.contains("\t"), "seed \(seed)")
    #expect(said == said.trimmingCharacters(in: .whitespaces), "seed \(seed)")
}

@Test func speakingCutsALongReplyAfterASentenceOnlyWhenOneEndsLateEnough() {
    // The last sentence end within 900 characters, when past the first 200.
    let sentences = String(repeating: "Alpha beta gamma delta. ", count: 60)
    let cut = OllamaRunner.speakable(sentences)
    #expect(cut.hasSuffix("delta.") && cut.count <= 900 && cut.count > 850)
    // No sentence end late enough: cut at 900 characters as they are.
    let run = "Short. " + String(repeating: "x", count: 2000)
    #expect(OllamaRunner.speakable(run).count == 900)
    // Graphemes, not scalars: an emoji family is never split.
    let emoji = String(repeating: "👩‍👩‍👧‍👦", count: 1000)
    #expect(OllamaRunner.speakable(emoji) == String(repeating: "👩‍👩‍👧‍👦", count: 900))
}

@Test(arguments: [
    ("brave_web_search", JSONValue.object(["query": .string("tides")]), "Searching the web for tides"),
    ("brave_news_search", .object(["query": .string("AI")]), "Searching the news for AI"),
    ("brave_local_search", .object(["query": .string("pizza")]), "Searching places for pizza"),
    ("brave_place_search", .object([:]), "Searching places"),
    ("brave_image_search", .object(["query": .string("cats")]), "Searching images for cats"),
    ("brave_video_search", .object(["query": .string("x")]), "Searching videos for x"),
    ("brave_summarizer", .object(["query": .number(3)]), "Searching the web"),
    ("", .string("{\"query\":\"q\"}"), "Searching the web"),
] as [(String, JSONValue, String)])
func eachToolHasACaption(_ tool: String, _ arguments: JSONValue, _ caption: String) {
    #expect(OllamaRunner.caption(tool, arguments) == caption)
}

@Test func theInstructionsGiveTheDateAndZoneAndAskForSpeech() throws {
    let zone = try #require(TimeZone(identifier: "America/Los_Angeles"))
    let text = OllamaRunner.instructions(now: Date(timeIntervalSince1970: 1_791_662_400), zone: zone)
    #expect(text.contains("It is now Saturday 10 October 2026, 13:00 in America/Los_Angeles."))
    #expect(text.contains("no markdown") && text.contains("Brave search tools"))
}

@Test func aReplyIsKeptWithoutItsThinkingAndAsTheAssistants() {
    let kept = OllamaRunner.kept(.object(["role": .string("model"), "content": .string("a"), "thinking": .string("hmm"),
                                          "tool_calls": .array([])]))
    #expect(kept == .object(["role": .string("assistant"), "content": .string("a"), "tool_calls": .array([])]))
    #expect(OllamaRunner.kept(.string("odd")) == OllamaRunner.message("assistant", ""))
}

/// Cancelled during a round's first tool call: the rest of the round's calls should not
/// be reported or made. `use()` turns the cancelled search into "The search failed" text
/// and the loop goes on, so each remaining call is still reported (and tried).
@Test func cancellingDuringAToolCallEndsTheRound() async throws {
    let mcp = LocalWire.host { seen in
        var reply = ScriptedMCP().reply(seen)
        if seen.method == "tools/call" { reply.delay = 0.3 }
        return reply
    }
    let ollama = LocalWire.host { seen in
        switch seen.path {
        case "/api/show": return .json(["capabilities": ["tools"]])
        default: return assistant("", calls: (0..<3).map { ["function": ["name": "brave_web_search", "arguments": ["query": "q\($0)"]]] })
        }
    }
    let session = LocalWire.session()
    let runner = OllamaRunner(base: URL(string: "http://\(ollama)")!,
                              search: MCPClient(endpoint: URL(string: "http://\(mcp)/mcp")!, session: session), session: session)
    let reports = Lines()
    let run = Task { try await runner.answer("q", model: "m:1", conversation: "c") { reports.add($0) } }
    #expect(await eventually { LocalWire.requests(mcp).contains { $0.method == "tools/call" } })
    run.cancel()
    _ = await run.result
    let reported = reports.all.count
    #expect(reported >= 1)
    #expect(reported == 1)
    
}
