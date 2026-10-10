import Foundation
import Testing
@testable import LokiSessions

/// Answers Ollama's and the MCP server's HTTP calls from a script, recording them.
final class FakeLocal: URLProtocol, @unchecked Sendable {
    struct Reply {
        var status = 200
        var type = "application/json"
        var body: String
    }
    nonisolated(unsafe) static var handler: (@Sendable (URLRequest, JSONValue?) -> Reply)?
    nonisolated(unsafe) static var seen: [(path: String, body: JSONValue?)] = []
    static let lock = NSLock()

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        var data = request.httpBody
        if data == nil, let stream = request.httpBodyStream {
            stream.open()
            var body = Data(), buffer = [UInt8](repeating: 0, count: 4096)
            while stream.hasBytesAvailable {
                let count = stream.read(&buffer, maxLength: buffer.count)
                if count <= 0 { break }
                body.append(buffer, count: count)
            }
            stream.close()
            data = body
        }
        let json = data.flatMap { try? JSONSerialization.jsonObject(with: $0) }.map(JSONValue.init)
        let reply = Self.lock.withLock {
            Self.seen.append((request.url!.path, json))
            return Self.handler?(request, json) ?? Reply(status: 500, body: "{}")
        }
        let response = HTTPURLResponse(url: request.url!, statusCode: reply.status, httpVersion: "HTTP/1.1",
                                       headerFields: ["Content-Type": reply.type])!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: Data(reply.body.utf8))
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}

    static func session() -> URLSession {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FakeLocal.self]
        lock.withLock { seen = [] }
        return URLSession(configuration: configuration)
    }

    static var calls: [(path: String, body: JSONValue?)] { lock.withLock { seen } }
}

private func json(_ object: Any) -> String {
    String(decoding: try! JSONSerialization.data(withJSONObject: object), as: UTF8.self)
}

/// An event stream holding one JSON-RPC result for the request with `id`.
private func event(_ id: Any, _ result: Any) -> FakeLocal.Reply {
    FakeLocal.Reply(type: "text/event-stream",
                    body: "event: message\ndata: \(json(["jsonrpc": "2.0", "id": id, "result": result]))\n\n")
}

private let ollama = URL(string: "http://ollama.test")!, mcp = URL(string: "http://mcp.test/mcp")!

/// The MCP side of Brave Search Bridge: initialize, tools/list and tools/call.
private func brave(_ request: URLRequest, _ body: JSONValue?) -> FakeLocal.Reply? {
    guard request.url?.host == "mcp.test" else { return nil }
    let id: Any = body?["id"].map { if case .number(let n) = $0 { Int(n) as Any } else { 0 as Any } } ?? 0
    switch body?["method"]?.string {
    case "initialize": return event(id, ["protocolVersion": "2025-06-18", "capabilities": [:]])
    case "notifications/initialized": return FakeLocal.Reply(status: 202, body: "")
    case "tools/list":
        return event(id, ["tools": [["name": "brave_web_search", "description": "Search the web",
                                     "inputSchema": ["type": "object", "properties": ["query": ["type": "string"]]]]]])
    case "tools/call":
        let query = body?["params"]?["arguments"]?["query"]?.string ?? ""
        return event(id, ["content": [["type": "text", "text": "Results for \(query): sunny, 63 F."]]])
    default: return FakeLocal.Reply(status: 400, body: "{}")
    }
}

@Suite(.serialized) struct OllamaRunnerTests {
    @Test func aReplyThatSearchesCallsTheToolAndAnswersInPlainSpeech() async throws {
        let rounds = Counter()
        FakeLocal.handler = { request, body in
            if let reply = brave(request, body) { return reply }
            switch request.url!.path {
            case "/api/show": return .init(body: json(["capabilities": ["completion", "tools", "thinking"]]))
            case "/api/chat":
                if rounds.next() == 0 {
                    return .init(body: json(["message": ["role": "assistant", "content": "", "thinking": "hmm",
                                                         "tool_calls": [["function": ["name": "brave_web_search",
                                                                                      "arguments": ["query": "Rogue River weather"]]]]]]))
                }
                return .init(body: json(["message": ["role": "assistant",
                                                     "content": "**Sunny**, a high of 63. See https://weather.gov for more."]]))
            default: return .init(status: 404, body: "{}")
            }
        }
        let session = FakeLocal.session()
        let runner = OllamaRunner(base: ollama, search: MCPClient(endpoint: mcp, session: session), session: session)
        let reports = Lines()
        let said = try await runner.answer("What's the weather in Rogue River?", model: "gpt-oss:20b", conversation: "s1") {
            reports.add($0)
        }
        #expect(said == "Sunny, a high of 63. See for more.")
        #expect(reports.all == ["Searching the web for Rogue River weather"])
        let chats = FakeLocal.calls.filter { $0.path == "/api/chat" }.compactMap(\.body)
        #expect(chats.count == 2)
        // Every call offers the search tool, thinks as little as the model allows, and the
        // second carries the tool's result after the call it answers.
        #expect(chats.allSatisfy { $0["tools"]?.foundation as? [Any] != nil && $0["think"] == .string("low") })
        guard case .array(let messages)? = chats[1]["messages"] else { Issue.record("no messages"); return }
        #expect(messages.map { $0["role"]?.string } == ["system", "user", "assistant", "tool"])
        #expect(messages[3]["content"]?.string == "Results for Rogue River weather: sunny, 63 F.")
        #expect(messages[2]["thinking"] == nil)  // thinking is not kept
        #expect(messages[0]["content"]?.string?.contains("spoken aloud") == true)
    }

    @Test func aConversationIsKeptForFollowUpsUntilForgotten() async throws {
        FakeLocal.handler = { request, body in
            if let reply = brave(request, body) { return reply }
            if request.url!.path == "/api/show" { return .init(body: json(["capabilities": ["completion", "tools"]])) }
            return .init(body: json(["message": ["role": "assistant", "content": "Okay."]]))
        }
        let session = FakeLocal.session()
        let runner = OllamaRunner(base: ollama, search: MCPClient(endpoint: mcp, session: session), session: session)
        _ = try await runner.answer("one", model: "granite4.1:8b", conversation: "s1") { _ in }
        _ = try await runner.answer("two", model: "granite4.1:8b", conversation: "s1") { _ in }
        _ = try await runner.answer("other", model: "granite4.1:8b", conversation: "s2") { _ in }
        await runner.forget("s1")
        _ = try await runner.answer("three", model: "granite4.1:8b", conversation: "s1") { _ in }
        let chats = FakeLocal.calls.filter { $0.path == "/api/chat" }.compactMap(\.body)
        func users(_ chat: JSONValue) -> [String] {
            guard case .array(let messages)? = chat["messages"] else { return [] }
            return messages.filter { $0["role"]?.string == "user" }.compactMap { $0["content"]?.string }
        }
        #expect(chats.map(users) == [["one"], ["one", "two"], ["other"], ["three"]])
        #expect(chats.allSatisfy { $0["think"] == nil })  // a model that does not think is not told how
    }

    @Test func withoutTheSearchBridgeOllamaStillAnswersAndErrorsAreReadable() async throws {
        FakeLocal.handler = { request, _ in
            if request.url?.host == "mcp.test" { return .init(status: 503, body: "{}") }
            if request.url!.path == "/api/show" { return .init(body: json(["capabilities": ["completion", "tools"]])) }
            return .init(body: json(["message": ["role": "assistant", "content": "Paris."]]))
        }
        let session = FakeLocal.session()
        let runner = OllamaRunner(base: ollama, search: MCPClient(endpoint: mcp, session: session), session: session)
        #expect(try await runner.answer("capital of France", model: "m", conversation: "s1") { _ in } == "Paris.")
        #expect(FakeLocal.calls.filter { $0.path == "/api/chat" }.allSatisfy { $0.body?["tools"] == nil })
        FakeLocal.handler = { _, _ in .init(body: json(["error": "model 'x' not found"])) }
        await #expect(throws: LocalHTTP.Failure.self) {
            try await runner.answer("hi", model: "x", conversation: "s3") { _ in }
        }
        FakeLocal.handler = { _, _ in .init(body: json(["message": ["role": "assistant", "content": "  "]])) }
        await #expect(throws: LocalHTTP.Failure.self) {
            try await runner.answer("hi", model: "x", conversation: "s4") { _ in }
        }
    }

    @Test func aModelThatNeverStopsSearchingIsMadeToAnswer() async throws {
        FakeLocal.handler = { request, body in
            if let reply = brave(request, body) { return reply }
            if request.url!.path == "/api/show" { return .init(body: json(["capabilities": ["tools"]])) }
            if body?["tools"] == nil { return .init(body: json(["message": ["role": "assistant", "content": "Done."]])) }
            return .init(body: json(["message": ["role": "assistant", "content": "",
                                                 "tool_calls": [["function": ["name": "brave_web_search",
                                                                              "arguments": ["query": "more"]]]]]]))
        }
        let session = FakeLocal.session()
        let runner = OllamaRunner(base: ollama, search: MCPClient(endpoint: mcp, session: session), session: session)
        #expect(try await runner.answer("loop", model: "m", conversation: "s1") { _ in } == "Done.")
        #expect(FakeLocal.calls.filter { $0.path == "/api/chat" }.count == OllamaRunner.maxRounds + 1)
    }

    @Test func modelsAreTheLocalOnesThatUseToolsNewestFirst() async throws {
        FakeLocal.handler = { request, body in
            switch request.url!.path {
            case "/api/tags":
                return .init(body: json(["models": [
                    ["name": "old:1", "modified_at": "2025-01-01T00:00:00Z"],
                    ["name": "new:1", "modified_at": "2026-10-01T00:00:00Z"],
                    ["name": "cloud:1", "modified_at": "2026-10-09T00:00:00Z", "remote_host": "https://ollama.com:443"],
                    ["name": "notools:1", "modified_at": "2026-09-01T00:00:00Z"],
                ]]))
            case "/api/show":
                let model = body?["model"]?.string ?? ""
                return .init(body: json(["capabilities": model == "notools:1" ? ["completion"] : ["completion", "tools"]]))
            default: return .init(status: 404, body: "{}")
            }
        }
        let runner = OllamaRunner(base: ollama, search: nil, session: FakeLocal.session())
        #expect(await runner.models() == ["new:1", "old:1"])
        FakeLocal.handler = { _, _ in .init(status: 500, body: "{}") }
        let down = OllamaRunner(base: ollama, search: nil, session: FakeLocal.session())
        #expect(await down.models() == [])
    }
}

// MARK: Pieces

@Test func anEventStreamYieldsTheResponseWithItsID() throws {
    let stream = "event: message\r\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}\r\n\r\n"
        + "data: {\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"ok\":true}}\n\n"
        + "data: {\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{\"ok\":false}}\n"
    let seven = try MCPClient.response(to: 7, in: Data(stream.utf8), type: "text/event-stream; charset=utf-8")
    #expect(seven["result"]?["ok"]?.bool == true)
    let nine = try MCPClient.response(to: 9, in: Data(stream.utf8), type: "text/event-stream")
    #expect(nine["result"]?["ok"]?.bool == false)
    #expect(throws: LocalHTTP.Failure.self) { try MCPClient.response(to: 8, in: Data(stream.utf8), type: "text/event-stream") }
    let plain = try MCPClient.response(to: 1, in: Data(#"{"id":1,"result":{}}"#.utf8), type: "application/json")
    #expect(plain["id"] == .number(1))
    #expect(throws: LocalHTTP.Failure.self) { try MCPClient.response(to: 1, in: Data("[1]".utf8), type: nil) }
}

@Test func repliesAreMadeFitForTheVoice() {
    #expect(OllamaRunner.speakable("## Weather\n- **Sunny** today\n- High of `63`\n1. Wear [a hat](https://x.y/z).")
            == "Weather Sunny today High of 63 Wear a hat.")
    #expect(OllamaRunner.speakable("Read https://example.com/a?b=c now") == "Read now")
    let long = String(repeating: "This is a sentence. ", count: 100)
    let cut = OllamaRunner.speakable(long)
    #expect(cut.count <= 900 && cut.hasSuffix("."))
    #expect(OllamaRunner.speakable("") == "")
    #expect(OllamaRunner.caption("brave_news_search", .object(["query": .string("AI")])) == "Searching the news for AI")
    #expect(OllamaRunner.caption("other", .object([:])) == "Searching the web")
}

@Test func aLongConversationKeepsItsInstructionsAndStartsAtAQuestion() {
    var messages: [JSONValue] = [OllamaRunner.message("system", "rules")]
    for index in 0..<30 {
        messages.append(OllamaRunner.message("user", "q\(index)"))
        messages.append(OllamaRunner.message("assistant", "a\(index)"))
        messages.append(.object(["role": .string("tool"), "content": .string("t")]))
    }
    let kept = OllamaRunner.trimmed(messages)
    #expect(kept.count <= OllamaRunner.maxMessages + 1 && kept.first?["content"]?.string == "rules")
    #expect(kept[1]["role"]?.string == "user" && kept.last == messages.last)
}

/// Lines heard from another task.
final class Lines: @unchecked Sendable {
    private let lock = NSLock()
    private var lines: [String] = []
    func add(_ line: String) { lock.withLock { lines.append(line) } }
    var all: [String] { lock.withLock { lines } }
}

/// Counts calls from any task: next() returns 0, 1, 2, …
final class Counter: @unchecked Sendable {
    private let lock = NSLock()
    private var count = 0
    func next() -> Int { lock.withLock { defer { count += 1 }; return count } }
}
