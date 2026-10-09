/// Field tables for both directions (docs/loki-protocol.md), and the mapping between
/// them and the Swift message enums.
enum Specs {
    static let u32 = Int64(UInt32.max)

    static let bridge: [String: TypeSpec] = {
        let f = Schema.field
        let list = [
            TypeSpec(name: "welcome", fields: [
                f("proto", .unsigned(1...1), nil), f("bridge", .line(maxBytes: 40), nil),
                f("voice", .line(maxBytes: 40), .string("")),
            ]),
            TypeSpec(name: "state", fields: [
                f("state", .word(LokiState.allCases.map(\.rawValue)), nil),
                f("caption", .text(maxBytes: 200), .string("")),
                f("progress", .signed(-1...1000), .number(-1)),
                f("queue", .unsigned(0...999), .number(0)),
            ]),
            TypeSpec(name: "caption", fields: [f("text", .text(maxBytes: 400), nil), f("final", .flag, nil)]),
            TypeSpec(name: "ask", fields: [
                f("id", .identifier, nil), f("nonce", .hex(length: 32), nil),
                f("kind", .word(AskKind.allCases.map(\.rawValue)), nil),
                f("title", .line(maxBytes: 120), nil), f("detail", .text(maxBytes: 600), .string("")),
                f("options", .options(2...4), nil), f("timeout_ms", .unsigned(1000...600_000), nil),
            ]),
            TypeSpec(name: "ask_cancel", fields: [f("id", .identifier, nil)]),
            TypeSpec(name: "reply_begin", fields: [
                f("reply", .unsigned(1...u32), nil), f("rate", .unsigned(24_000...24_000), nil),
            ]),
            TypeSpec(name: "reply_end", fields: [f("reply", .unsigned(1...u32), nil), f("frames", .unsigned(0...u32), nil)]),
            TypeSpec(name: "stop_audio", fields: []),
            TypeSpec(name: "settings", fields: [f("volume", .unsigned(0...100), nil)]),
        ]
        return Dictionary(uniqueKeysWithValues: list.map { ($0.name, $0) })
    }()

    static let device: [String: TypeSpec] = {
        let f = Schema.field
        let list = [
            TypeSpec(name: "hello", fields: [
                f("proto", .unsigned(1...1), nil), f("app", .constant("loki-agent"), nil),
                f("fw", .line(maxBytes: 40), nil), f("board", .line(maxBytes: 16), nil),
                f("rate", .unsigned(24_000...24_000), nil), f("channels", .unsigned(1...4), nil),
                f("frame_ms", .unsigned(20...100), nil),
            ]),
            TypeSpec(name: "ptt_start", fields: [f("stream", .unsigned(1...u32), nil)]),
            TypeSpec(name: "ptt_end", fields: [
                f("stream", .unsigned(1...u32), nil), f("frames", .unsigned(0...u32), nil),
                f("dropped", .unsigned(0...u32), nil),
            ]),
            TypeSpec(name: "cancel", fields: []),
            TypeSpec(name: "answer", fields: [
                f("id", .identifier, nil), f("nonce", .hex(length: 32), nil),
                f("digest", .hex(length: 64), nil), f("choice", .unsigned(0...3), nil),
            ]),
            TypeSpec(name: "played", fields: [f("reply", .unsigned(1...u32), nil), f("complete", .flag, nil)]),
            TypeSpec(name: "status", fields: [
                f("battery", .signed(-1...100), nil), f("charging", .flag, nil), f("rssi", .signed(-127...0), nil),
            ]),
        ]
        return Dictionary(uniqueKeysWithValues: list.map { ($0.name, $0) })
    }()
}

/// Typed access to a decoded message's values; the schema guarantees their types.
private struct Values {
    let raw: RawMessage
    func number(_ key: String) -> Int64 {
        if case .number(let n) = raw.values[key] { return n }
        preconditionFailure("schema gives \(key) a number")
    }
    func u32(_ key: String) -> UInt32 { UInt32(number(key)) }
    func i32(_ key: String) -> Int32 { Int32(number(key)) }
    func flag(_ key: String) -> Bool {
        if case .flag(let flag) = raw.values[key] { return flag }
        preconditionFailure("schema gives \(key) a boolean")
    }
    func string(_ key: String) -> String {
        if case .string(let text) = raw.values[key] { return text }
        preconditionFailure("schema gives \(key) a string")
    }
    func strings(_ key: String) -> [String] {
        if case .strings(let list) = raw.values[key] { return list }
        preconditionFailure("schema gives \(key) a list")
    }
}

extension BridgeMessage {
    init(_ raw: RawMessage) throws(LokiError) {
        let v = Values(raw: raw)
        switch raw.type {
        case "welcome": self = .welcome(bridge: v.string("bridge"), voice: v.string("voice"))
        case "state":
            self = .state(LokiState(rawValue: v.string("state"))!, caption: v.string("caption"),
                          progress: v.i32("progress"), queue: v.u32("queue"))
        case "caption": self = .caption(text: v.string("text"), final: v.flag("final"))
        case "ask":
            let ask = Ask(id: v.string("id"), nonce: v.string("nonce"), kind: AskKind(rawValue: v.string("kind"))!,
                          title: v.string("title"), detail: v.string("detail"), options: v.strings("options"),
                          timeoutMs: v.u32("timeout_ms"))
            guard ask.kind == .choose || ask.options.count == 2 else {
                throw LokiError("an approval has exactly two options")
            }
            self = .ask(ask)
        case "ask_cancel": self = .askCancel(id: v.string("id"))
        case "reply_begin": self = .replyBegin(reply: v.u32("reply"))
        case "reply_end": self = .replyEnd(reply: v.u32("reply"), frames: v.u32("frames"))
        case "stop_audio": self = .stopAudio
        case "settings": self = .settings(volume: v.u32("volume"))
        default: throw LokiError("unknown type")
        }
    }

    func raw(seq: UInt32) -> RawMessage {
        func n<T: BinaryInteger>(_ value: T) -> FieldValue { .number(Int64(value)) }
        let (type, values): (String, [String: FieldValue]) = switch self {
        case .welcome(let bridge, let voice):
            ("welcome", ["proto": n(Loki.protocolVersion), "bridge": .string(bridge), "voice": .string(voice)])
        case .state(let state, let caption, let progress, let queue):
            ("state", ["state": .string(state.rawValue), "caption": .string(caption),
                       "progress": n(progress), "queue": n(queue)])
        case .caption(let text, let final): ("caption", ["text": .string(text), "final": .flag(final)])
        case .ask(let ask):
            ("ask", ["id": .string(ask.id), "nonce": .string(ask.nonce), "kind": .string(ask.kind.rawValue),
                     "title": .string(ask.title), "detail": .string(ask.detail),
                     "options": .strings(ask.options), "timeout_ms": n(ask.timeoutMs)])
        case .askCancel(let id): ("ask_cancel", ["id": .string(id)])
        case .replyBegin(let reply): ("reply_begin", ["reply": n(reply), "rate": n(Loki.sampleRate)])
        case .replyEnd(let reply, let frames): ("reply_end", ["reply": n(reply), "frames": n(frames)])
        case .stopAudio: ("stop_audio", [:])
        case .settings(let volume): ("settings", ["volume": n(volume)])
        }
        return RawMessage(type: type, seq: seq, values: values)
    }
}

extension DeviceMessage {
    init(_ raw: RawMessage) throws(LokiError) {
        let v = Values(raw: raw)
        switch raw.type {
        case "hello":
            self = .hello(fw: v.string("fw"), board: v.string("board"), channels: v.u32("channels"),
                          frameMs: v.u32("frame_ms"))
        case "ptt_start": self = .pttStart(stream: v.u32("stream"))
        case "ptt_end": self = .pttEnd(stream: v.u32("stream"), frames: v.u32("frames"), dropped: v.u32("dropped"))
        case "cancel": self = .cancel
        case "answer":
            self = .answer(id: v.string("id"), nonce: v.string("nonce"), digest: v.string("digest"),
                           choice: v.u32("choice"))
        case "played": self = .played(reply: v.u32("reply"), complete: v.flag("complete"))
        case "status": self = .status(battery: v.i32("battery"), charging: v.flag("charging"), rssi: v.i32("rssi"))
        default: throw LokiError("unknown type")
        }
    }

    func raw(seq: UInt32) -> RawMessage {
        func n<T: BinaryInteger>(_ value: T) -> FieldValue { .number(Int64(value)) }
        let (type, values): (String, [String: FieldValue]) = switch self {
        case .hello(let fw, let board, let channels, let frameMs):
            ("hello", ["proto": n(Loki.protocolVersion), "app": .string("loki-agent"), "fw": .string(fw),
                       "board": .string(board), "rate": n(Loki.sampleRate), "channels": n(channels),
                       "frame_ms": n(frameMs)])
        case .pttStart(let stream): ("ptt_start", ["stream": n(stream)])
        case .pttEnd(let stream, let frames, let dropped):
            ("ptt_end", ["stream": n(stream), "frames": n(frames), "dropped": n(dropped)])
        case .cancel: ("cancel", [:])
        case .answer(let id, let nonce, let digest, let choice):
            ("answer", ["id": .string(id), "nonce": .string(nonce), "digest": .string(digest), "choice": n(choice)])
        case .played(let reply, let complete): ("played", ["reply": n(reply), "complete": .flag(complete)])
        case .status(let battery, let charging, let rssi):
            ("status", ["battery": n(battery), "charging": .flag(charging), "rssi": n(rssi)])
        }
        return RawMessage(type: type, seq: seq, values: values)
    }
}

/// loki/1 control messages in both directions. The bridge decodes what the board
/// sends; the simulated device decodes what the bridge sends.
public enum LokiCodec {
    public static func decodeBridgeMessage(_ bytes: [UInt8]) throws(LokiError) -> Decoded<BridgeMessage> {
        switch try Schema.decode(bytes, specs: Specs.bridge, maxBytes: Loki.maxBridgeMessage) {
        case .ignored(let seq): return .ignored(seq: seq)
        case .message(let raw): return .message(seq: raw.seq, try BridgeMessage(raw))
        }
    }

    public static func decodeDeviceMessage(_ bytes: [UInt8]) throws(LokiError) -> Decoded<DeviceMessage> {
        switch try Schema.decode(bytes, specs: Specs.device, maxBytes: Loki.maxDeviceMessage) {
        case .ignored(let seq): return .ignored(seq: seq)
        case .message(let raw): return .message(seq: raw.seq, try DeviceMessage(raw))
        }
    }

    public static func encode(_ message: BridgeMessage, seq: UInt32) throws(LokiError) -> [UInt8] {
        if case .ask(let ask) = message, ask.kind == .approve, ask.options.count != 2 {
            throw LokiError("an approval has exactly two options")
        }
        return try Schema.encode(message.raw(seq: seq), specs: Specs.bridge, maxBytes: Loki.maxBridgeMessage)
    }

    public static func encode(_ message: DeviceMessage, seq: UInt32) throws(LokiError) -> [UInt8] {
        try Schema.encode(message.raw(seq: seq), specs: Specs.device, maxBytes: Loki.maxDeviceMessage)
    }

    /// The canonical form the golden vectors use.
    public static func canonical(_ message: BridgeMessage, seq: UInt32) -> String {
        Schema.canonical(message.raw(seq: seq), specs: Specs.bridge)
    }

    public static func canonical(_ message: DeviceMessage, seq: UInt32) -> String {
        Schema.canonical(message.raw(seq: seq), specs: Specs.device)
    }
}
