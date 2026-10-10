import Foundation
import Testing
@testable import LokiSessions

// The MCP client Ollama searches through (ADR-018), against a scripted server: one
// session initialized once and again after a failure, its session id echoed, JSON-RPC
// errors and tool failures as text, replies capped, and the event stream read however
// the server frames it (seeded, so a failure replays from its seed).

/// A local service scripted per host, so tests that each use their own host run side by
/// side. Records every request with its headers; a reply may be delayed, or the
/// connection refused.
final class LocalWire: URLProtocol, @unchecked Sendable {
    struct Reply {
        var status = 200
        var headers: [String: String] = ["Content-Type": "application/json"]
        var body = Data()
        /// Seconds before the reply arrives; a cancelled request never gets it.
        var delay = 0.0
        /// The connection fails instead.
        var refuse = false

        static func json(_ object: Any, status: Int = 200, headers: [String: String] = [:]) -> Reply {
            Reply(status: status, headers: ["Content-Type": "application/json"].merging(headers) { $1 },
                  body: try! JSONSerialization.data(withJSONObject: object))
        }
        static func events(_ text: String, headers: [String: String] = [:]) -> Reply {
            Reply(headers: ["Content-Type": "text/event-stream"].merging(headers) { $1 }, body: Data(text.utf8))
        }
        static func text(_ text: String, status: Int = 200, type: String = "application/json") -> Reply {
            Reply(status: status, headers: ["Content-Type": type], body: Data(text.utf8))
        }
    }

    struct Seen: Sendable {
        let host: String
        let path: String
        let headers: [String: String]
        let body: JSONValue?
        var method: String? { body?["method"]?.string }
        func header(_ name: String) -> String? { headers.first { $0.key.lowercased() == name.lowercased() }?.value }
    }

    typealias Handler = @Sendable (Seen) -> Reply
    private static let lock = NSLock()
    nonisolated(unsafe) private static var handlers: [String: Handler] = [:]
    nonisolated(unsafe) private static var seen: [String: [Seen]] = [:]
    private var stopped = false
    private let stopLock = NSLock()

    /// A fresh host for one test, answered by `handler`.
    static func host(_ handler: @escaping Handler) -> String {
        let host = "h\(UUID().uuidString.lowercased().prefix(12)).test"
        lock.withLock { handlers[host] = handler }
        return host
    }

    static func requests(_ host: String) -> [Seen] { lock.withLock { seen[host] ?? [] } }

    static func session() -> URLSession {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [LocalWire.self]
        return URLSession(configuration: configuration)
    }

    override class func canInit(with request: URLRequest) -> Bool { request.url?.host?.hasSuffix(".test") == true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        let data = request.httpBody ?? request.httpBodyStream.map(Self.read)
        let json = data.flatMap { try? JSONSerialization.jsonObject(with: $0) }.map(JSONValue.init)
        let host = request.url?.host ?? ""
        let entry = Seen(host: host, path: request.url?.path ?? "", headers: request.allHTTPHeaderFields ?? [:], body: json)
        let handler = Self.lock.withLock { () -> Handler? in
            Self.seen[host, default: []].append(entry)
            return Self.handlers[host]
        }
        let reply = handler?(entry) ?? Reply(status: 500)
        let deliver = { [self] in
            guard !stopLock.withLock({ stopped }) else { return }
            if reply.refuse {
                client?.urlProtocol(self, didFailWithError: URLError(.cannotConnectToHost))
                return
            }
            let response = HTTPURLResponse(url: request.url!, statusCode: reply.status, httpVersion: "HTTP/1.1",
                                           headerFields: reply.headers)!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: reply.body)
            client?.urlProtocolDidFinishLoading(self)
        }
        if reply.delay > 0 {
            DispatchQueue.global().asyncAfter(deadline: .now() + reply.delay, execute: deliver)
        } else {
            deliver()
        }
    }

    override func stopLoading() { stopLock.withLock { stopped = true } }

    private static func read(_ stream: InputStream) -> Data {
        stream.open()
        defer { stream.close() }
        var body = Data(), buffer = [UInt8](repeating: 0, count: 4096)
        while stream.hasBytesAvailable {
            let count = stream.read(&buffer, maxLength: buffer.count)
            if count <= 0 { break }
            body.append(buffer, count: count)
        }
        return body
    }
}

func wireJSON(_ object: Any) -> String {
    String(decoding: try! JSONSerialization.data(withJSONObject: object, options: [.sortedKeys]), as: UTF8.self)
}

/// The JSON-RPC id of a request, as the server echoes it.
func rpcID(_ seen: LocalWire.Seen) -> Int {
    if case .number(let n)? = seen.body?["id"] { return Int(n) }
    return -1
}

/// A Brave-like MCP server: answers initialize (with `session` as its Mcp-Session-Id),
/// tools/list and tools/call, each as JSON or an event stream; `call` scripts tools/call.
struct ScriptedMCP: Sendable {
    var session: String? = "sess-1"
    var stream = true
    /// tools/call finds the server gone: the connection is refused.
    var refuseCalls = false
    var tools: [[String: Any]] {
        [["name": "brave_web_search", "description": "Search the web",
          "inputSchema": ["type": "object", "properties": ["query": ["type": "string"]]]],
         ["name": "brave_news_search", "description": "Search the news"],
         ["description": "a tool without a name"], ["name": "", "description": "an empty name"]]
    }
    var call: @Sendable (LocalWire.Seen) -> [String: Any] = { seen in
        let query = seen.body?["params"]?["arguments"]?["query"]?.string ?? "?"
        return ["result": ["content": [["type": "text", "text": "Results for \(query)"]]]]
    }

    func reply(_ seen: LocalWire.Seen) -> LocalWire.Reply {
        let id = rpcID(seen)
        func answer(_ payload: [String: Any], headers: [String: String] = [:]) -> LocalWire.Reply {
            let message = payload.merging(["jsonrpc": "2.0", "id": id]) { $1 }
            return stream ? .events("event: message\ndata: \(wireJSON(message))\n\n", headers: headers)
                : .json(message, headers: headers)
        }
        switch seen.method {
        case "initialize":
            return answer(["result": ["protocolVersion": "2025-06-18", "capabilities": [:]]],
                          headers: session.map { ["Mcp-Session-Id": $0] } ?? [:])
        case "notifications/initialized": return LocalWire.Reply(status: 202)
        case "tools/list": return answer(["result": ["tools": tools]])
        case "tools/call": return refuseCalls ? LocalWire.Reply(refuse: true) : answer(call(seen))
        default: return .json(["jsonrpc": "2.0", "id": id, "error": ["code": -32601, "message": "Method not found"]])
        }
    }
}

private func client(_ host: String) -> MCPClient {
    MCPClient(endpoint: URL(string: "http://\(host)/mcp")!, session: LocalWire.session())
}

private func methods(_ host: String) -> [String] { LocalWire.requests(host).compactMap(\.method) }

// MARK: The session

@Test func theSessionIsInitializedOnceAndItsIDIsEchoedOnEveryLaterRequest() async throws {
    let server = ScriptedMCP(session: "abc-123")
    let host = LocalWire.host { server.reply($0) }
    let mcp = client(host)
    #expect(try await mcp.call("brave_web_search", arguments: .object(["query": .string("one")])) == "Results for one")
    #expect(try await mcp.call("brave_web_search", arguments: .object(["query": .string("two")])) == "Results for two")
    let tools = try await mcp.tools()
    _ = try await mcp.tools()  // listed once
    #expect(methods(host) == ["initialize", "notifications/initialized", "tools/call", "tools/call", "tools/list"])
    let seen = LocalWire.requests(host)
    #expect(seen[0].header("Mcp-Session-Id") == nil)
    #expect(seen.dropFirst().allSatisfy { $0.header("Mcp-Session-Id") == "abc-123" })
    #expect(seen.allSatisfy { $0.header("MCP-Protocol-Version") == "2025-06-18" })
    #expect(seen.allSatisfy { $0.header("Accept") == "application/json, text/event-stream" })
    #expect(seen.allSatisfy { $0.header("Content-Type") == "application/json" })
    // Ids rise, the notification has none, and initialize says who the client is.
    #expect(seen.filter { $0.method != "notifications/initialized" }.map(rpcID) == [1, 2, 3, 4])
    #expect(seen[1].body?["id"] == nil)
    #expect(seen[0].body?["params"]?["clientInfo"]?["name"]?.string == "Loki Bridge")
    // Tools without a name are dropped; one without a schema gets an empty object schema.
    #expect(tools.map(\.name) == ["brave_web_search", "brave_news_search"])
    #expect(tools[1].schema == .object(["type": .string("object")]) && tools[1].description == "Search the news")
    #expect(tools[0].schema["properties"]?["query"]?["type"]?.string == "string")
}

@Test func aServerWithoutSessionIDsIsAskedWithoutOneAndPlainJSONRepliesWork() async throws {
    let server = ScriptedMCP(session: nil, stream: false)
    let host = LocalWire.host { server.reply($0) }
    let mcp = client(host)
    #expect(try await mcp.call("brave_web_search", arguments: .object(["query": .string("x")])) == "Results for x")
    #expect(LocalWire.requests(host).allSatisfy { $0.header("Mcp-Session-Id") == nil })
}

@Test func afterAFailureTheNextCallStartsANewSessionWithoutTheOldID() async throws {
    let failures = Counter()
    let sessions = Counter()
    let host = LocalWire.host { seen in
        if seen.method == "tools/call", failures.next() == 0 { return LocalWire.Reply(status: 500) }
        var fresh = ScriptedMCP()
        if seen.method == "initialize" { fresh.session = "sess-\(sessions.next())" }
        return fresh.reply(seen)
    }
    let mcp = client(host)
    await #expect(throws: LocalHTTP.Failure.self) {
        try await mcp.call("brave_web_search", arguments: .object(["query": .string("a")]))
    }
    #expect(try await mcp.call("brave_web_search", arguments: .object(["query": .string("b")])) == "Results for b")
    #expect(methods(host) == ["initialize", "notifications/initialized", "tools/call",
                              "initialize", "notifications/initialized", "tools/call"])
    let seen = LocalWire.requests(host)
    #expect(seen[3].header("Mcp-Session-Id") == nil)  // the old session is not offered again
    #expect(seen[4].header("Mcp-Session-Id") == "sess-1" && seen[5].header("Mcp-Session-Id") == "sess-1")
    #expect(seen[2].header("Mcp-Session-Id") == "sess-0")
}

@Test func aFailedInitializeIsRetriedAndANotificationTheServerRefusesFailsTheCall() async throws {
    let attempts = Counter()
    let host = LocalWire.host { seen in
        if seen.method == "initialize", attempts.next() == 0 { return .text("{}", status: 503) }
        if seen.method == "notifications/initialized", attempts.next() == 2 { return .text("", status: 400) }
        return ScriptedMCP().reply(seen)
    }
    let mcp = client(host)
    await #expect(throws: LocalHTTP.Failure.self) { try await mcp.tools() }
    await #expect(throws: LocalHTTP.Failure.self) { try await mcp.tools() }
    #expect(try await mcp.tools().count == 2)
    #expect(methods(host) == ["initialize", "initialize", "notifications/initialized",
                              "initialize", "notifications/initialized", "tools/list"])
}

/// Two first calls at once: the session should still be initialized once. Each
/// initialize waits 100 ms, so both calls are in flight before either finishes.
@Test func twoFirstCallsAtOnceInitializeOneSession() async throws {
    let host = LocalWire.host { seen in
        var reply = ScriptedMCP().reply(seen)
        if seen.method == "initialize" { reply.delay = 0.1 }
        return reply
    }
    let mcp = client(host)
    async let one = mcp.call("brave_web_search", arguments: .object(["query": .string("1")]))
    async let two = mcp.call("brave_web_search", arguments: .object(["query": .string("2")]))
    #expect(try await [one, two].sorted() == ["Results for 1", "Results for 2"])
    // The second call waits for the first's initialize rather than starting its own.
    #expect(methods(host).filter { $0 == "initialize" }.count == 1)
    
}

// MARK: Replies

@Test func aJSONRPCErrorIsAFailureNamingTheMethodAndEndsTheSession() async throws {
    let host = LocalWire.host { seen in
        var server = ScriptedMCP()
        server.call = { _ in ["error": ["code": -32602, "message": "Invalid params: query is required"]] }
        return server.reply(seen)
    }
    let mcp = client(host)
    do {
        _ = try await mcp.call("brave_web_search", arguments: .object([:]))
        Issue.record("an error reply was taken as a result")
    } catch let failure as LocalHTTP.Failure {
        #expect(failure.description == "Brave Search Bridge refused tools/call: Invalid params: query is required")
    }
    // An error without a message still says something.
    let bare = LocalWire.host { seen in
        var server = ScriptedMCP()
        server.call = { _ in ["error": ["code": -1]] }
        return server.reply(seen)
    }
    await #expect(throws: LocalHTTP.Failure.self) { try await client(bare).call("x", arguments: .object([:])) }
    do { _ = try await client(bare).call("x", arguments: .object([:])) } catch {
        #expect("\(error)" == "Brave Search Bridge refused tools/call: no reason given")
    }
    _ = try? await mcp.call("brave_web_search", arguments: .object([:]))
    #expect(methods(host).filter { $0 == "initialize" }.count == 2)
}

@Test(arguments: [
    // (content, isError, text the model reads)
    ([["type": "text", "text": "a"], ["type": "image", "data": "zzz"], ["type": "text", "text": "b"]], false, "a\nb"),
    ([["type": "text", "text": "quota exceeded"]], true, "The tool failed: quota exceeded"),
    ([], true, "The tool failed: "),
    ([["type": "text"], ["type": "resource", "text": "not text"]], false, ""),
] as [([[String: String]], Bool, String)])
func aToolResultIsItsTextAndAFailureSaysSo(_ content: [[String: String]], _ isError: Bool, _ expected: String) async throws {
    let host = LocalWire.host { seen in
        var server = ScriptedMCP()
        server.call = { _ in ["result": ["content": content, "isError": isError]] }
        return server.reply(seen)
    }
    #expect(try await client(host).call("brave_web_search", arguments: .object([:])) == expected)
}

@Test func aResultWithoutContentIsEmptyAndAReplyWithoutAResultFails() async throws {
    let empty = LocalWire.host { seen in
        var server = ScriptedMCP()
        server.call = { _ in ["result": ["isError": "true"]] }  // a string is not true
        return server.reply(seen)
    }
    #expect(try await client(empty).call("t", arguments: .object([:])) == "")
    let none = LocalWire.host { seen in
        var server = ScriptedMCP()
        server.call = { _ in ["note": "no result"] }
        return server.reply(seen)
    }
    await #expect(throws: LocalHTTP.Failure.self) { try await client(none).call("t", arguments: .object([:])) }
    let noTools = LocalWire.host { seen in
        seen.method == "tools/list" ? .json(["jsonrpc": "2.0", "id": rpcID(seen), "result": ["tools": "none"]])
            : ScriptedMCP().reply(seen)
    }
    do {
        _ = try await client(noTools).tools()
        Issue.record("a list that is not an array was taken")
    } catch {
        #expect("\(error)" == "Brave Search Bridge listed no tools")
    }
}

@Test func anOversizedReplyIsCutOffAndTheNextCallStartsAgain() async throws {
    let big = Counter()
    let host = LocalWire.host { seen in
        if seen.method == "tools/call", big.next() == 0 {
            return .text(String(repeating: " ", count: MCPClient.maxReply + 1) + "{}")
        }
        return ScriptedMCP().reply(seen)
    }
    let mcp = client(host)
    do {
        _ = try await mcp.call("brave_web_search", arguments: .object([:]))
        Issue.record("an oversized reply was read")
    } catch {
        #expect("\(error)" == "Brave Search Bridge's reply is too large")
    }
    #expect(try await mcp.call("brave_web_search", arguments: .object(["query": .string("q")])) == "Results for q")
}

@Test func aServerThatIsNotRunningIsSaidInWords() async throws {
    let host = LocalWire.host { _ in LocalWire.Reply(refuse: true) }
    do {
        _ = try await client(host).tools()
        Issue.record("a refused connection gave tools")
    } catch {
        #expect("\(error)" == "Brave Search Bridge is not answering; is it running?")
    }
}

// MARK: LocalHTTP

@Test func aReplyOfExactlyTheLimitIsReadAndOneByteMoreIsNot() async throws {
    let host = LocalWire.host { seen in .text(String(repeating: "x", count: seen.path == "/ten" ? 10 : 11)) }
    let http = LocalHTTP(session: LocalWire.session())
    let (data, response) = try await http.send(URL(string: "http://\(host)/ten")!, limit: 10, service: "S")
    #expect(data.count == 10 && response.statusCode == 200)
    await #expect(throws: LocalHTTP.Failure.self) {
        try await http.send(URL(string: "http://\(host)/eleven")!, limit: 10, service: "S")
    }
    // A GET has no body; a POST is JSON.
    let seen = LocalWire.requests(host)
    #expect(seen[0].body == nil && seen[0].header("Content-Type") == nil)
}

@Test(arguments: [
    (404, #"{"error":"model 'x' not found"}"#, "S refused the request (404)", "model 'x' not found"),
    (500, "not json", "S refused the request (500)", nil),
    (302, "{}", "S refused the request (302)", nil),
    (503, #"{"error":42}"#, "S refused the request (503)", nil),
] as [(Int, String, String, String?)])
func aStatusOutside2xxIsARefusalKeepingTheServicesReason(_ status: Int, _ body: String, _ said: String, _ reason: String?) async {
    let host = LocalWire.host { _ in .text(body, status: status) }
    do {
        _ = try await LocalHTTP(session: LocalWire.session()).send(URL(string: "http://\(host)/x")!,
                                                                    .object(["a": .number(1)]), limit: 1000, service: "S")
        Issue.record("status \(status) was taken as an answer")
    } catch let failure as LocalHTTP.Failure {
        #expect(failure.description == said && failure.reason == reason)
    } catch {
        Issue.record("unexpected \(error)")
    }
}

/// Non-functional: the reply is read a byte at a time, so a reply at the MCP cap must
/// still be read promptly. Measured (median of 3), with a generous bound for CI.
@Test func aReplyAtTheCapIsReadPromptly() async throws {
    let body = "{\"pad\":\"" + String(repeating: "a", count: MCPClient.maxReply - 20) + "\"}"
    let host = LocalWire.host { _ in .text(body) }
    let http = LocalHTTP(session: LocalWire.session())
    var times: [Duration] = []
    for _ in 0..<3 {
        let start = ContinuousClock.now
        let (data, _) = try await http.send(URL(string: "http://\(host)/big")!, limit: MCPClient.maxReply, service: "S")
        times.append(ContinuousClock.now - start)
        #expect(data.count == body.utf8.count)
    }
    let median = times.sorted()[1]
    print("LocalHTTP read of \(body.utf8.count) bytes: median \(median)")
    #expect(median < .seconds(5))
}

// MARK: The event stream

private let seven = #"{"jsonrpc":"2.0","id":7,"result":{"ok":true,"text":"a: b"}}"#

@Test(arguments: [
    ("data: \(seven)\n\n", "LF and a space"),
    ("data:\(seven)\n\n", "no space after the colon"),
    ("data:  \(seven)\n\n", "two spaces: one is the separator"),
    ("data: \(seven)\r\n\r\n", "CR LF"),
    ("data: \(seven)\r\r", "CR alone"),
    (": keep-alive\n\nevent: message\nid: 3\nretry: 100\ndata: \(seven)\n\n", "comments and other fields"),
    ("data: {\"jsonrpc\":\"2.0\",\ndata: \"id\":7,\ndata: \"result\":{\"ok\":true,\"text\":\"a: b\"}}\n\n", "split over lines"),
    ("data: \(seven)", "no blank line at the end"),
    ("data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/message\"}\n\ndata: {\"id\":6,\"result\":{}}\n\ndata: \(seven)\n\n",
     "after a notification and another id"),
    ("\n\n\ndata: \(seven)\n\n\n", "blank lines around"),
] as [(String, String)])
func anEventStreamYieldsTheResponseHoweverItIsFramed(_ stream: String, _ framing: String) throws {
    let message = try MCPClient.response(to: 7, in: Data(stream.utf8), type: "Text/Event-Stream; charset=utf-8")
    #expect(message["result"]?["ok"]?.bool == true, "\(framing)")
    #expect(message["result"]?["text"]?.string == "a: b", "\(framing)")
}

@Test(arguments: [
    "data: \(seven)\ndata: \(seven)\n\n",  // two data lines in one event are one payload
    "event: message\n\n",
    "",
    ": only a comment\n\n",
    "data: {\"id\":\"7\",\"result\":{}}\n\n",  // the id as a string is another id
    "data:{broken\n\n",
] as [String])
func anEventStreamWithoutTheResponseIsAFailure(_ stream: String) {
    #expect(throws: LocalHTTP.Failure.self) { try MCPClient.response(to: 7, in: Data(stream.utf8), type: "text/event-stream") }
}

/// Property: one JSON-RPC response, pretty-printed and framed with random line endings,
/// comments, other fields, notifications and responses to other ids, is always found;
/// the parse never traps and gives the same answer twice.
@Test(arguments: 0..<150)
func anyFramingOfTheResponseIsFound(_ seed: Int) throws {
    var random = Seeded(seed)
    let id = 1 + random.below(1000)
    let text = String((0..<random.below(30)).map { _ in random.pick(Array("ab :\"\\\n\té😀{}[],")) })
    let message: [String: Any] = ["jsonrpc": "2.0", "id": id, "result": ["text": text, "n": random.below(100)]]
    let pretty = String(decoding: try JSONSerialization.data(withJSONObject: message, options: [.prettyPrinted]), as: UTF8.self)
    let endings = ["\n", "\r\n", "\r"]
    var stream = "", last = ""
    // A CR then an empty line ended by LF is one CR LF, which the spec reads as one
    // line ending: the blank line would really be gone, so that pair is not generated.
    func line(_ value: String) {
        var ending = random.pick(endings)
        if value.isEmpty, last == "\r", ending == "\n" { ending = "\r" }
        stream += value + ending
        last = ending
    }
    for _ in 0..<random.below(4) {
        switch random.below(4) {
        case 0: line(": ping")
        case 1: line("event: message"); line("data: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\"}"); line("")
        case 2: line("data: " + wireJSON(["jsonrpc": "2.0", "id": id + 1, "result": [:]])); line("")
        default: line("id: \(random.below(99))")
        }
    }
    for piece in pretty.split(separator: "\n", omittingEmptySubsequences: false) {
        line((random.chance(50) ? "data: " : "data:") + piece)
    }
    if random.chance(80) { line("") }
    let data = Data(stream.utf8)
    let found = try MCPClient.response(to: id, in: data, type: "text/event-stream")
    #expect(found["result"]?["text"]?.string == text, "seed \(seed): \(stream.debugDescription)")
    #expect(try MCPClient.response(to: id, in: data, type: "text/event-stream") == found, "seed \(seed)")
}

/// Property: random bytes as an event stream or JSON never trap and fail the same way.
@Test(arguments: 0..<150)
func anyBytesAsAReplyNeverTrap(_ seed: Int) {
    var random = Seeded(seed)
    let pieces: [[UInt8]] = [Array("data:".utf8), Array("data: ".utf8), [0x0A], [0x0D], [0x0D, 0x0A], Array("{\"id\":1".utf8),
                             Array(",\"result\":{}}".utf8), [0xFF], [0xC3], [0x00], Array(": c".utf8), Array("\"".utf8)]
    var bytes: [UInt8] = []
    for _ in 0..<random.below(40) { bytes += random.pick(pieces) }
    let type = random.pick(["text/event-stream", "application/json", nil] as [String?])
    let first = Result { try MCPClient.response(to: 1, in: Data(bytes), type: type) }
    let second = Result { try MCPClient.response(to: 1, in: Data(bytes), type: type) }
    switch (first, second) {
    case (.success(let a), .success(let b)):
        // An event stream yields only the response with the id; a JSON body is the reply.
        #expect(a == b && (type != "text/event-stream" || a["id"] == .number(1)), "seed \(seed)")
    case (.failure, .failure): break
    default: Issue.record("seed \(seed): two parses disagreed")
    }
}
