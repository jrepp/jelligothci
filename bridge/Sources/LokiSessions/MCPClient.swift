import Foundation

/// JSON over HTTP to a service on this Mac (Ollama, Brave Search Bridge): no proxy,
/// cache, cookies or redirects, and replies capped, so a service that misbehaves can
/// only refuse or answer.
struct LocalHTTP: Sendable {
    struct Failure: Error, CustomStringConvertible, Sendable {
        let description: String
        /// The service's own reason, when it refused with one (`{"error": "…"}`).
        let reason: String?
        init(_ description: String, reason: String? = nil) {
            self.description = description
            self.reason = reason
        }
    }

    let session: URLSession

    static let privateSession: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.connectionProxyDictionary = [:]
        configuration.urlCache = nil
        configuration.httpCookieStorage = nil
        configuration.timeoutIntervalForRequest = 300
        configuration.timeoutIntervalForResource = 600
        return URLSession(configuration: configuration, delegate: RefuseRedirects(), delegateQueue: nil)
    }()

    /// The reply to a request with `body` as JSON (a GET without one), cut off with an
    /// error past `limit` bytes; any status but 2xx is an error naming `service`.
    func send(_ url: URL, _ body: JSONValue? = nil, headers: [String: String] = [:], limit: Int,
              service: String) async throws -> (Data, HTTPURLResponse) {
        var request = URLRequest(url: url)
        if let body {
            request.httpMethod = "POST"
            request.httpBody = try JSONSerialization.data(withJSONObject: body.foundation)
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        for (name, value) in headers { request.setValue(value, forHTTPHeaderField: name) }
        let bytes: URLSession.AsyncBytes, response: URLResponse
        do {
            (bytes, response) = try await session.bytes(for: request)
        } catch is CancellationError {
            throw CancellationError()
        } catch {
            if Task.isCancelled { throw CancellationError() }
            throw Failure("\(service) is not answering; is it running?")
        }
        var data = Data()
        for try await byte in bytes {
            guard data.count < limit else { throw Failure("\(service)'s reply is too large") }
            data.append(byte)
        }
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            let reason = (try? Self.object(data, service))?["error"]?.string
            throw Failure("\(service) refused the request (\((response as? HTTPURLResponse)?.statusCode ?? 0))",
                          reason: reason)
        }
        return (data, http)
    }

    static func object(_ data: Data, _ service: String) throws -> JSONValue {
        guard let any = try? JSONSerialization.jsonObject(with: data), any is [String: Any] else {
            throw Failure("\(service) sent a reply that is not a JSON object")
        }
        return JSONValue(any)
    }
}

/// Local services never redirect; a redirect is refused rather than followed.
final class RefuseRedirects: NSObject, URLSessionTaskDelegate, Sendable {
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest) async -> URLRequest? { nil }
}

/// A client for one MCP server over streamable HTTP: it initializes once, lists the
/// server's tools and calls them. Brave Search Bridge serves the Brave Search MCP
/// server this way (ADR-018). A reply is JSON or an event stream that carries the
/// JSON-RPC response; both are read. After any failure the next call starts over.
public actor MCPClient {
    public struct Tool: Equatable, Sendable {
        public let name: String
        public let description: String
        public let schema: JSONValue
    }

    public static let braveSearchBridge = URL(string: "http://127.0.0.1:18424/mcp")!
    static let protocolVersion = "2025-06-18"
    static let maxReply = 1 << 20
    static let service = "Brave Search Bridge"

    let endpoint: URL
    let http: LocalHTTP
    private var sessionID: String?
    private var started = false
    /// The initialize under way, which a second caller waits for rather than repeating.
    private var starting: Task<Void, Error>?
    private var cached: [Tool]?
    private var nextID = 1

    public init(endpoint: URL = MCPClient.braveSearchBridge, session: URLSession? = nil) {
        self.endpoint = endpoint
        http = LocalHTTP(session: session ?? LocalHTTP.privateSession)
    }

    /// The server's tools, listed once.
    public func tools() async throws -> [Tool] {
        if let cached { return cached }
        let result = try await request("tools/list", .object([:]))
        guard case .array(let list)? = result["tools"] else { throw LocalHTTP.Failure("\(Self.service) listed no tools") }
        let tools = list.compactMap { entry -> Tool? in
            guard let name = entry["name"]?.string, !name.isEmpty else { return nil }
            return Tool(name: name, description: entry["description"]?.string ?? "",
                        schema: entry["inputSchema"] ?? .object(["type": .string("object")]))
        }
        cached = tools
        return tools
    }

    /// The text a tool call returns; a tool's own failure comes back as text too, so
    /// the model can read it.
    public func call(_ name: String, arguments: JSONValue) async throws -> String {
        let result = try await request("tools/call", .object(["name": .string(name), "arguments": arguments]))
        var parts: [String] = []
        if case .array(let content)? = result["content"] {
            for item in content where item["type"]?.string == "text" {
                if let text = item["text"]?.string { parts.append(text) }
            }
        }
        let text = parts.joined(separator: "\n")
        return result["isError"]?.bool == true ? "The tool failed: \(text)" : text
    }

    private func start() async throws {
        guard !started else { return }
        if let starting { return try await starting.value }
        let begin = Task { try await self.initialize() }
        starting = begin
        defer { starting = nil }
        try await begin.value
        started = true
    }

    private func initialize() async throws {
        let params = JSONValue.object([
            "protocolVersion": .string(Self.protocolVersion), "capabilities": .object([:]),
            "clientInfo": .object(["name": .string("Loki Bridge"), "version": .string("1")]),
        ])
        _ = try await exchange("initialize", params)
        _ = try await http.send(endpoint, .object(["jsonrpc": .string("2.0"), "method": .string("notifications/initialized")]),
                                headers: headers(), limit: Self.maxReply, service: Self.service)
    }

    private func request(_ method: String, _ params: JSONValue) async throws -> JSONValue {
        do {
            try await start()
            return try await exchange(method, params)
        } catch {
            started = false
            sessionID = nil
            throw error
        }
    }

    private func exchange(_ method: String, _ params: JSONValue) async throws -> JSONValue {
        let id = nextID
        nextID += 1
        let body = JSONValue.object(["jsonrpc": .string("2.0"), "id": .number(Double(id)), "method": .string(method),
                                     "params": params])
        let (data, response) = try await http.send(endpoint, body, headers: headers(), limit: Self.maxReply,
                                                   service: Self.service)
        if let session = response.value(forHTTPHeaderField: "Mcp-Session-Id"), !session.isEmpty { sessionID = session }
        let message = try Self.response(to: id, in: data, type: response.value(forHTTPHeaderField: "Content-Type"))
        if let error = message["error"] {
            throw LocalHTTP.Failure("\(Self.service) refused \(method): \(error["message"]?.string ?? "no reason given")")
        }
        guard let result = message["result"] else { throw LocalHTTP.Failure("\(Self.service) sent no result") }
        return result
    }

    private func headers() -> [String: String] {
        var headers = ["Accept": "application/json, text/event-stream", "MCP-Protocol-Version": Self.protocolVersion]
        if let sessionID { headers["Mcp-Session-Id"] = sessionID }
        return headers
    }

    /// The JSON-RPC response with `id`: the whole body, or the `data:` of one event in
    /// an event stream.
    static func response(to id: Int, in data: Data, type: String?) throws -> JSONValue {
        guard type?.lowercased().hasPrefix("text/event-stream") == true else {
            return try LocalHTTP.object(data, service)
        }
        var lines: [Substring] = []
        func event() -> JSONValue? {
            defer { lines = [] }
            let payload = lines.joined(separator: "\n")
            guard !payload.isEmpty, let message = try? LocalHTTP.object(Data(payload.utf8), service),
                  message["id"] == .number(Double(id))
            else { return nil }
            return message
        }
        // Lines end with CR LF, LF or CR; Swift counts CR LF as one character.
        let text = String(decoding: data, as: UTF8.self)
        for line in text.split(omittingEmptySubsequences: false, whereSeparator: { $0 == "\n" || $0 == "\r\n" || $0 == "\r" }) {
            if line.isEmpty {
                if let message = event() { return message }
            } else if line.hasPrefix("data:") {
                let value = line.dropFirst(5)
                lines.append(value.hasPrefix(" ") ? value.dropFirst() : value)
            }
        }
        if let message = event() { return message }
        throw LocalHTTP.Failure("\(service) sent no reply to the request")
    }
}
