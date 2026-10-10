import Foundation

/// What the Agent SDK sidecar reports (bridge/sidecar/src/protocol.ts).
public enum SidecarEvent: Sendable, Equatable {
    case ready
    case state(session: String, state: String)
    case tool(session: String, tool: String, summary: String)
    case permission(session: String, request: String, tool: String, input: JSONValue, reason: String?, blockedPath: String?)
    case question(session: String, request: String, questions: [SidecarQuestion])
    case speak(session: String, text: String, mood: String)
    case progress(session: String, message: String, fraction: Double?)
    case result(session: String, ok: Bool, text: String, turns: Int, cost: Double)
    case error(session: String?, message: String)
    case closed(session: String)
    /// The sidecar process ended; the bridge restarts it.
    case exited(status: Int32)
}

public struct SidecarQuestion: Sendable, Equatable {
    public let question: String
    public let options: [String]
    public let multiSelect: Bool
}

/// A JSON value from the sidecar, kept for showing tool inputs on approval cards.
public enum JSONValue: Sendable, Equatable {
    case string(String), number(Double), bool(Bool), null
    indirect case array([JSONValue]), object([String: JSONValue])

    init(_ any: Any) {
        switch any {
        case let value as String: self = .string(value)
        case let value as NSNumber where CFGetTypeID(value) == CFBooleanGetTypeID(): self = .bool(value.boolValue)
        case let value as NSNumber: self = .number(value.doubleValue)
        case let value as [Any]: self = .array(value.map(JSONValue.init))
        case let value as [String: Any]: self = .object(value.mapValues(JSONValue.init))
        default: self = .null
        }
    }

    public subscript(key: String) -> JSONValue? {
        if case .object(let object) = self { return object[key] }
        return nil
    }

    public var string: String? {
        if case .string(let value) = self { return value }
        return nil
    }

    public var bool: Bool? {
        if case .bool(let value) = self { return value }
        return nil
    }

    /// Back to Foundation's JSON types, for JSONSerialization.
    public var foundation: Any {
        switch self {
        case .string(let s): s
        case .number(let n): n
        case .bool(let b): b
        case .null: NSNull()
        case .array(let a): a.map(\.foundation)
        case .object(let o): o.mapValues(\.foundation)
        }
    }

    /// Any JSON-compatible Foundation value, such as SessionPolicy's options.
    public static func from(_ any: Any) -> JSONValue? {
        guard JSONSerialization.isValidJSONObject(["v": any]) else { return nil }
        return JSONValue(any)
    }
}

enum SidecarParse {
    static func event(_ line: Data) -> SidecarEvent? {
        guard let object = try? JSONSerialization.jsonObject(with: line) as? [String: Any],
              let type = object["type"] as? String
        else { return nil }
        let session = object["session"] as? String
        func text(_ key: String) -> String? { object[key] as? String }
        switch (type, session) {
        case ("ready", _): return .ready
        case ("state", let s?): return text("state").map { .state(session: s, state: $0) }
        case ("tool", let s?): return .tool(session: s, tool: text("tool") ?? "", summary: text("summary") ?? "")
        case ("permission", let s?):
            guard let request = text("request"), let tool = text("tool") else { return nil }
            return .permission(session: s, request: request, tool: tool, input: JSONValue(object["input"] ?? NSNull()),
                               reason: text("reason"), blockedPath: text("blockedPath"))
        case ("question", let s?):
            guard let request = text("request"), let list = object["questions"] as? [[String: Any]] else { return nil }
            let questions = list.compactMap { entry -> SidecarQuestion? in
                guard let question = entry["question"] as? String, let options = entry["options"] as? [[String: Any]] else {
                    return nil
                }
                return SidecarQuestion(question: question, options: options.compactMap { $0["label"] as? String },
                                       multiSelect: entry["multiSelect"] as? Bool ?? false)
            }
            return .question(session: s, request: request, questions: questions)
        case ("speak", let s?): return text("text").map { .speak(session: s, text: $0, mood: text("mood") ?? "neutral") }
        case ("progress", let s?):
            return text("message").map { .progress(session: s, message: $0, fraction: (object["fraction"] as? NSNumber)?.doubleValue) }
        case ("result", let s?):
            return .result(session: s, ok: object["ok"] as? Bool ?? false, text: text("text") ?? "",
                           turns: (object["turns"] as? NSNumber)?.intValue ?? 0,
                           cost: (object["cost"] as? NSNumber)?.doubleValue ?? 0)
        case ("error", _): return .error(session: session, message: text("message") ?? "error")
        case ("closed", let s?): return .closed(session: s)
        default: return nil
        }
    }
}

/// What the bridge tells the sidecar.
public enum SidecarCommand: Sendable {
    case start(session: String, options: JSONValue)
    case prompt(session: String, text: String)
    case interrupt(session: String)
    case decision(session: String, request: String, allow: Bool, message: String?, answers: [String: String]?)
    case close(session: String)

    var json: [String: Any] {
        switch self {
        case .start(let session, let options): ["type": "start", "session": session, "options": options.foundation]
        case .prompt(let session, let text): ["type": "prompt", "session": session, "text": text]
        case .interrupt(let session): ["type": "interrupt", "session": session]
        case .decision(let session, let request, let allow, let message, let answers):
            ["type": "decision", "session": session, "request": request, "allow": allow]
                .merging(message.map { ["message": $0] } ?? [:]) { a, _ in a }
                .merging(answers.map { ["answers": $0] } ?? [:]) { a, _ in a }
        case .close(let session): ["type": "close", "session": session]
        }
    }
}

/// Carries lines between the bridge and a sidecar: a Node process in use, a fake in tests.
public protocol SidecarTransport: Sendable {
    func send(_ line: Data)
    var lines: AsyncStream<Data> { get }
}

/// The sidecar as a child process: `node bridge/sidecar/src/main.ts`, its standard
/// input and output carrying the lines, its errors going to the bridge's log.
public final class SidecarProcess: SidecarTransport, @unchecked Sendable {
    public let lines: AsyncStream<Data>
    private let continuation: AsyncStream<Data>.Continuation
    private let process = Process()
    private let input = Pipe(), output = Pipe()
    private let lock = NSLock()
    private var buffer = Data()
    /// The stream ends once both happened, so lines written just before exit arrive.
    private var drained = false, exited = false

    public init(node: URL, script: URL, environment: [String: String]) throws {
        (lines, continuation) = AsyncStream.makeStream(of: Data.self)
        process.executableURL = node
        process.arguments = [script.path]
        process.environment = environment
        process.standardInput = input
        process.standardOutput = output
        process.standardError = FileHandle.standardError
        output.fileHandleForReading.readabilityHandler = { [weak self] handle in self?.receive(handle.availableData) }
        process.terminationHandler = { [weak self] _ in self?.ended(drained: false) }
        try process.run()
    }

    public func send(_ line: Data) {
        lock.withLock { try? input.fileHandleForWriting.write(contentsOf: line + Data([0x0A])) }
    }

    public var status: Int32? { process.isRunning ? nil : process.terminationStatus }

    public func stop() {
        try? input.fileHandleForWriting.close()
        process.terminate()
    }

    private func receive(_ data: Data) {
        guard !data.isEmpty else { return ended(drained: true) }  // end of file
        lock.withLock {
            buffer.append(data)
            while let newline = buffer.firstIndex(of: 0x0A) {
                let line = buffer[buffer.startIndex..<newline]
                buffer.removeSubrange(buffer.startIndex...newline)
                if !line.isEmpty { continuation.yield(Data(line)) }
            }
            if buffer.count > 4_000_000 { buffer.removeAll() }  // a runaway line
        }
    }

    private func ended(drained pipe: Bool) {
        if pipe { output.fileHandleForReading.readabilityHandler = nil }
        let both = lock.withLock { () -> Bool in
            if pipe { drained = true } else { exited = true }
            return drained && exited
        }
        if both { continuation.finish() }
    }
}

/// Typed access to a sidecar over any transport.
public final class Sidecar: Sendable {
    public let events: AsyncStream<SidecarEvent>
    private let transport: SidecarTransport

    public init(transport: SidecarTransport) {
        self.transport = transport
        let (events, continuation) = AsyncStream.makeStream(of: SidecarEvent.self)
        self.events = events
        Task {
            for await line in transport.lines {
                if let event = SidecarParse.event(line) { continuation.yield(event) }
            }
            continuation.yield(.exited(status: (transport as? SidecarProcess)?.status ?? 0))
            continuation.finish()
        }
    }

    public func send(_ command: SidecarCommand) {
        guard JSONSerialization.isValidJSONObject(command.json),
              let data = try? JSONSerialization.data(withJSONObject: command.json)
        else { return }
        transport.send(data)
    }
}
