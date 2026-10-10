import Foundation

/// Ollama on the Mac (ADR-018): the models it runs here that can use tools, and
/// answers by voice, with Brave Search Bridge's web search tools passed to every
/// call. A conversation is kept per job place for follow-ups; the model has no
/// other tool, so it reads no file and runs no command.
public actor OllamaRunner: LocalModels {
    public static let local = URL(string: "http://127.0.0.1:11434")!
    /// Tool rounds a reply may take before it must answer.
    static let maxRounds = 6
    /// A tool's text is cut to this before the model reads it.
    static let maxToolText = 8_000
    /// Searches one reply may make, in a round and in all: a page the model read
    /// cannot make it spend the user's Brave quota.
    static let maxCallsPerRound = 4, maxCalls = 12
    /// Messages kept per conversation, besides the instructions.
    static let maxMessages = 40
    static let maxReply = 4 << 20
    static let service = "Ollama"

    let base: URL
    let http: LocalHTTP
    let search: MCPClient?
    private var conversations: [String: [JSONValue]] = [:]
    private var capabilities: [String: Set<String>] = [:]

    public init(base: URL = OllamaRunner.local, search: MCPClient? = MCPClient(), session: URLSession? = nil) {
        self.base = base
        self.search = search
        http = LocalHTTP(session: session ?? LocalHTTP.privateSession)
    }

    // MARK: Models

    public func models() async -> [String] {
        guard let (data, _) = try? await http.send(base.appendingPathComponent("api/tags"), limit: Self.maxReply,
                                                   service: Self.service),
              let tags = try? LocalHTTP.object(data, Self.service), case .array(let list)? = tags["models"]
        else { return [] }
        // Local models only: one with a remote host runs elsewhere.
        let local = list.filter { $0["remote_host"] == nil }
            .sorted { ($0["modified_at"]?.string ?? "") > ($1["modified_at"]?.string ?? "") }
            .compactMap { $0["name"]?.string }
        var usable: [String] = []
        for name in local where await features(of: name).contains("tools") { usable.append(name) }
        return usable
    }

    /// What a model can do (`completion`, `tools`, `thinking`, …), asked once.
    func features(of model: String) async -> Set<String> {
        if let known = capabilities[model] { return known }
        guard let (data, _) = try? await http.send(base.appendingPathComponent("api/show"),
                                                   .object(["model": .string(model)]), limit: Self.maxReply,
                                                   service: Self.service),
              let show = try? LocalHTTP.object(data, Self.service), case .array(let list)? = show["capabilities"]
        else { return [] }
        let found = Set(list.compactMap(\.string))
        capabilities[model] = found
        return found
    }

    /// Loads the model, so a request that follows does not wait for it.
    public func warm(_ model: String) async {
        _ = try? await http.send(base.appendingPathComponent("api/chat"),
                                 .object(["model": .string(model), "messages": .array([])]), limit: Self.maxReply,
                                 service: Self.service)
    }

    public func forget(_ conversation: String) { conversations[conversation] = nil }

    // MARK: Answers

    public func answer(_ text: String, model: String, conversation: String,
                       report: @escaping @Sendable (String) async -> Void) async throws -> String {
        var messages = conversations[conversation] ?? [Self.message("system", Self.instructions())]
        messages.append(Self.message("user", text))
        let tools = await toolList()
        let thinking = await features(of: model).contains("thinking")
        var calls = 0
        for round in 0...Self.maxRounds {
            try Task.checkCancellation()
            let offered = round < Self.maxRounds ? tools : []
            let reply = try await chat(model, messages, tools: offered, thinking: thinking)
            let asked: [JSONValue] = if case .array(let list)? = reply["tool_calls"] { list } else { [] }
            messages.append(Self.kept(reply))
            guard !asked.isEmpty, !offered.isEmpty else {
                conversations[conversation] = Self.trimmed(messages)
                let said = Self.speakable(reply["content"]?.string ?? "")
                guard !said.isEmpty else { throw LocalHTTP.Failure("\(model) gave no answer") }
                return said
            }
            for (index, call) in asked.enumerated() {
                try Task.checkCancellation()
                let name = call["function"]?["name"]?.string ?? ""
                let arguments = call["function"]?["arguments"] ?? .object([:])
                let result: String
                if index >= Self.maxCallsPerRound || calls >= Self.maxCalls {
                    result = "Search limit reached; answer now with what you have."
                } else {
                    calls += 1
                    await report(Self.caption(name, arguments))
                    result = try await use(name, arguments)
                }
                messages.append(.object(["role": .string("tool"), "tool_name": .string(name), "content": .string(result)]))
            }
        }
        throw LocalHTTP.Failure("\(model) kept searching and gave no answer")
    }

    /// Brave Search Bridge's search tools in Ollama's form; none when it is not running.
    /// Only `brave_` tools are passed on, so Ollama gains nothing else the server might
    /// one day list.
    private func toolList() async -> [JSONValue] {
        guard let search, let tools = try? await search.tools() else { return [] }
        return tools.filter { Self.searchTool($0.name) }.map { tool in
            .object(["type": .string("function"), "function": .object([
                "name": .string(tool.name), "description": .string(tool.description), "parameters": tool.schema,
            ])])
        }
    }

    static func searchTool(_ name: String) -> Bool { name.hasPrefix("brave_") }

    /// A search's text for the model; a failure is text it can read, a stop is not.
    private func use(_ name: String, _ arguments: JSONValue) async throws -> String {
        guard let search, Self.searchTool(name) else { return "No such tool." }
        do {
            return String(try await search.call(name, arguments: arguments).prefix(Self.maxToolText))
        } catch is CancellationError {
            throw CancellationError()
        } catch {
            if Task.isCancelled { throw CancellationError() }
            return "The search failed: \(error)"
        }
    }

    private func chat(_ model: String, _ messages: [JSONValue], tools: [JSONValue], thinking: Bool) async throws
        -> JSONValue {
        var body: [String: JSONValue] = ["model": .string(model), "messages": .array(messages), "stream": .bool(false)]
        if !tools.isEmpty { body["tools"] = .array(tools) }
        // Thinking slows a spoken answer: as little as the model allows.
        if thinking { body["think"] = model.hasPrefix("gpt-oss") ? .string("low") : .bool(false) }
        let data: Data
        do {
            (data, _) = try await http.send(base.appendingPathComponent("api/chat"), .object(body), limit: Self.maxReply,
                                            service: Self.service)
        } catch let failure as LocalHTTP.Failure {
            throw Self.explained(failure.reason, model: model) ?? failure
        }
        let reply = try LocalHTTP.object(data, Self.service)
        if let error = reply["error"]?.string { throw Self.explained(error, model: model) ?? LocalHTTP.Failure("Ollama: \(error)") }
        guard let message = reply["message"] else { throw LocalHTTP.Failure("Ollama sent no message") }
        return message
    }

    // MARK: Pieces

    /// Ollama's reason in words for the user: the model missing or failing to load,
    /// else its first line.
    static func explained(_ reason: String?, model: String) -> LocalHTTP.Failure? {
        guard let reason, !reason.isEmpty else { return nil }
        let lower = reason.lowercased()
        if lower.contains("not found") && lower.contains("model") && !lower.contains("key not found") {
            return LocalHTTP.Failure("Ollama does not have \(model)", reason: reason)
        }
        if lower.contains("error loading model") || lower.contains("llama-server process has terminated") {
            return LocalHTTP.Failure("Ollama could not load \(model); pick another model in Settings", reason: reason)
        }
        let line = reason.split(whereSeparator: \.isNewline).first.map(String.init) ?? reason
        return LocalHTTP.Failure("Ollama: \(line.prefix(120))", reason: reason)
    }

    static func message(_ role: String, _ content: String) -> JSONValue {
        .object(["role": .string(role), "content": .string(content)])
    }

    /// The reply as the conversation keeps it: its thinking left out.
    static func kept(_ reply: JSONValue) -> JSONValue {
        guard case .object(var object) = reply else { return message("assistant", "") }
        object["thinking"] = nil
        object["role"] = .string("assistant")
        return .object(object)
    }

    /// The instructions, then the most recent messages, starting at a user's: at least
    /// the last question and what followed it, however long that exchange was.
    static func trimmed(_ messages: [JSONValue]) -> [JSONValue] {
        guard messages.count > maxMessages + 1 else { return messages }
        let users = messages.indices.dropFirst().filter { messages[$0]["role"]?.string == "user" }
        let window = messages.count - maxMessages
        guard let start = users.first(where: { $0 >= window }) ?? users.last else { return [messages[0]] }
        return [messages[0]] + messages[start...]
    }

    static func instructions(now: Date = Date(), zone: TimeZone = .current) -> String {
        let format = DateFormatter()
        format.locale = Locale(identifier: "en_US_POSIX")
        format.timeZone = zone
        format.dateFormat = "EEEE d MMMM yyyy, HH:mm"
        return """
            You are Loki, a voice assistant on the user's desk. What you write is spoken aloud by a \
            text-to-speech voice, so answer in plain spoken sentences: no markdown, lists, tables, code, \
            links or emoji. Keep it short, one to three sentences, unless the user asks for more. Use the \
            Brave search tools for anything current or anything you are not sure of, such as news, weather, \
            prices, sports and events, and say what you found. It is now \(format.string(from: now)) in \
            \(zone.identifier).
            """
    }

    /// A short line for the board while a tool runs.
    static func caption(_ tool: String, _ arguments: JSONValue) -> String {
        let query = arguments["query"]?.string.map { " for \($0)" } ?? ""
        let kind = switch tool {
        case "brave_news_search": "the news"
        case "brave_local_search", "brave_place_search": "places"
        case "brave_image_search": "images"
        case "brave_video_search": "videos"
        default: "the web"
        }
        return "Searching \(kind)\(query)"
    }

    /// Text fit for the voice: links, markdown marks and list bullets gone, whitespace
    /// single, and at most about 900 characters, cut after a sentence.
    static func speakable(_ text: String) -> String {
        var out = text.replacingOccurrences(of: #"\[([^\]]*)\]\([^)]*\)"#, with: "$1", options: .regularExpression)
        out = out.replacingOccurrences(of: #"https?://\S+"#, with: "", options: .regularExpression)
        out = out.replacingOccurrences(of: #"(?m)^\s*(#{1,6}|[-*•]|\d+[.)])\s+"#, with: "", options: .regularExpression)
        out = out.replacingOccurrences(of: #"[*_`~|]+"#, with: "", options: .regularExpression)
        out = out.split(whereSeparator: \.isWhitespace).joined(separator: " ")
        guard out.count > 900 else { return out }
        let head = String(out.prefix(900))
        if let end = head.lastIndex(where: { ".!?".contains($0) }), head.distance(from: head.startIndex, to: end) > 200 {
            return String(head[...end])
        }
        // No sentence ends in reach: cut between words when a space is near the end.
        if let space = head.lastIndex(of: " "), head.distance(from: head.startIndex, to: space) > 600 {
            return String(head[..<space])
        }
        return head
    }
}
