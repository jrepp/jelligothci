import Testing
@testable import LokiProtocol

/// The Swift encoder at every field's limit and one past it (docs/loki-protocol.md):
/// what it accepts decodes back to the same message, and what it refuses throws.
struct EncoderEdgeTests {
    static let nonce = "00112233445566778899aabbccddeeff"
    static let digest = String(repeating: "0123456789abcdef", count: 4)

    static func repeated(_ text: String, _ count: Int) -> String { String(repeating: text, count: count) }

    static func ask(id: String = "a1", nonce: String = nonce, kind: AskKind = .choose, title: String = "T",
                    detail: String = "", options: [String] = ["a", "b"], timeoutMs: UInt32 = 1000) -> BridgeMessage {
        .ask(Ask(id: id, nonce: nonce, kind: kind, title: title, detail: detail, options: options,
                 timeoutMs: timeoutMs))
    }

    /// Every type with every field at its largest, escapes doubling the bytes where
    /// they can.
    static let largestBridgeMessages: [BridgeMessage] = [
        .welcome(bridge: repeated("\"", 40), voice: repeated("\\", 40)),
        .welcome(bridge: repeated("\u{1F984}", 10), voice: repeated("\u{e9}", 20)),
        .state(.failure, caption: repeated("\n", 200), progress: 1000, queue: 999),
        .state(.partial, caption: repeated("x", 196) + "\u{1F984}", progress: 0, queue: 0),
        .caption(text: repeated("\"", 400), final: true),
        .caption(text: repeated("\u{2026}", 133) + "x", final: false),
        ask(id: repeated("Z", 32), title: repeated("\"", 120), detail: repeated("\\", 600),
            options: Array(repeating: repeated("\"", 40), count: 4), timeoutMs: 600_000),
        ask(kind: .approve, title: repeated("\u{e9}", 60), detail: repeated("\u{1F984}", 150),
            options: [repeated("\u{1F984}", 10), repeated("\u{2026}", 13) + "x"]),
        .askCancel(id: repeated("-", 32)),
        .replyBegin(reply: UInt32.max),
        .replyEnd(reply: UInt32.max, frames: UInt32.max),
        .stopAudio,
        .settings(volume: 100),
        .models(names: Array(repeating: repeated("+", 40), count: 16), current: 15),
    ]

    @Test func largestMessagesFitAndRoundTrip() throws {
        var largest = 0
        for message in Self.largestBridgeMessages {
            let bytes = try LokiCodec.encode(message, seq: UInt32.max)
            largest = max(largest, bytes.count)
            #expect(bytes.count <= Loki.maxBridgeMessage)
            #expect(try LokiCodec.decodeBridgeMessage(bytes) == .message(seq: UInt32.max, message))
        }
        // The fullest ask, with escapes doubling every byte, still leaves room.
        #expect(largest > 1_900 && largest <= Loki.maxBridgeMessage, "\(largest)")
    }

    /// Accepted at the limit, refused one past it.
    static let bridgeLimits: [(BridgeMessage, Bool)] = [
        (.welcome(bridge: ""), true), (.welcome(bridge: repeated("b", 40)), true),
        (.welcome(bridge: repeated("b", 41)), false), (.welcome(bridge: repeated("b", 38) + "\u{e9}"), true),
        (.welcome(bridge: repeated("b", 39) + "\u{e9}"), false),
        (.welcome(bridge: repeated("b", 36) + "\u{1F984}"), true),
        (.welcome(bridge: repeated("b", 37) + "\u{1F984}"), false), (.welcome(bridge: "a\nb"), false),
        (.welcome(bridge: "B", voice: repeated("v", 40)), true), (.welcome(bridge: "B", voice: repeated("v", 41)), false),
        (.welcome(bridge: "B", voice: "a\nb"), false),
        (.state(.idle, caption: repeated("c", 200)), true), (.state(.idle, caption: repeated("c", 201)), false),
        (.state(.idle, caption: repeated("c", 197) + "\u{2026}"), true),
        (.state(.idle, caption: repeated("c", 198) + "\u{2026}"), false),
        (.state(.idle, caption: "a\nb\u{7f}"), true), (.state(.idle, caption: "a\rb"), false),
        (.state(.idle, caption: "a\u{0}b"), false), (.state(.idle, caption: "a\u{1f}b"), false),
        (.state(.idle, progress: -1), true), (.state(.idle, progress: 1000), true),
        (.state(.idle, progress: -2), false), (.state(.idle, progress: 1001), false),
        (.state(.idle, queue: 999), true), (.state(.idle, queue: 1000), false),
        (.caption(text: "", final: false), true), (.caption(text: repeated("t", 400), final: true), true),
        (.caption(text: repeated("t", 401), final: true), false),
        (.caption(text: repeated("t", 396) + "\u{1F984}", final: true), true),
        (.caption(text: repeated("t", 397) + "\u{1F984}", final: true), false),
        (.caption(text: "a\tb", final: true), false),
        (ask(id: "a"), true), (ask(id: repeated("i", 32)), true), (ask(id: repeated("i", 33)), false),
        (ask(id: ""), false), (ask(id: "\u{e9}"), false), (ask(id: "a.b"), false),
        (ask(nonce: String(nonce.dropLast())), false), (ask(nonce: nonce + "0"), false),
        (ask(nonce: nonce.uppercased()), false),
        (ask(title: ""), true), (ask(title: repeated("t", 120)), true), (ask(title: repeated("t", 121)), false),
        (ask(title: repeated("t", 118) + "\u{e9}"), true), (ask(title: repeated("t", 119) + "\u{e9}"), false),
        (ask(title: "a\nb"), false),
        (ask(detail: repeated("d", 599) + "\n"), true), (ask(detail: repeated("d", 600) + "\n"), false),
        (ask(options: ["", ""]), true), (ask(options: ["a", "b", "c"]), true),
        (ask(options: Array(repeating: repeated("o", 40), count: 4)), true),
        (ask(options: ["a"]), false), (ask(options: ["a", "b", "c", "d", "e"]), false), (ask(options: []), false),
        (ask(options: ["a", repeated("o", 41)]), false), (ask(options: ["a", repeated("o", 38) + "\u{e9}"]), true),
        (ask(options: ["a", repeated("o", 39) + "\u{e9}"]), false),
        (ask(options: ["a", "b\nc"]), false), (ask(options: ["a", "b\tc"]), false),
        (ask(kind: .approve, options: ["Allow", "Deny"]), true),
        (ask(kind: .approve, options: ["Allow", "Deny", "Later"]), false),
        (ask(timeoutMs: 1000), true), (ask(timeoutMs: 600_000), true),
        (ask(timeoutMs: 999), false), (ask(timeoutMs: 600_001), false),
        (.askCancel(id: repeated("_", 32)), true), (.askCancel(id: repeated("_", 33)), false),
        (.replyBegin(reply: 1), true), (.replyBegin(reply: UInt32.max), true), (.replyBegin(reply: 0), false),
        (.replyEnd(reply: 1, frames: 0), true), (.replyEnd(reply: 0, frames: 0), false),
        (.settings(volume: 0), true), (.settings(volume: 100), true), (.settings(volume: 101), false),
        (.models(names: ["a"], current: 0), true), (.models(names: [repeated("z", 40)], current: 0), true),
        (.models(names: [repeated("z", 41)], current: 0), false), (.models(names: [""], current: 0), false),
        (.models(names: ["a b"], current: 0), false), (.models(names: ["a,b"], current: 0), false),
        (.models(names: ["g\u{e9}mma"], current: 0), false), (.models(names: ["a\"b"], current: 0), false),
        (.models(names: ["a", "b"], current: 2), false), (.models(names: [], current: 0), false),
        (.models(names: Array(repeating: "m", count: 16), current: 15), true),
        (.models(names: Array(repeating: "m", count: 17), current: 0), false),
    ]

    @Test func bridgeFieldLimits() throws {
        for (index, (message, accepted)) in Self.bridgeLimits.enumerated() {
            if accepted {
                let bytes = try LokiCodec.encode(message, seq: 1)
                #expect(try LokiCodec.decodeBridgeMessage(bytes) == .message(seq: 1, message), "case \(index)")
            } else {
                #expect(throws: LokiError.self, "case \(index): \(message)") { try LokiCodec.encode(message, seq: 1) }
            }
        }
    }

    static let deviceLimits: [(DeviceMessage, Bool)] = [
        (.hello(fw: "", board: "", channels: 1, frameMs: 20), true),
        (.hello(fw: repeated("f", 40), board: repeated("b", 16), channels: 4, frameMs: 100), true),
        (.hello(fw: repeated("f", 41), board: "b", channels: 1, frameMs: 60), false),
        (.hello(fw: "f", board: repeated("b", 17), channels: 1, frameMs: 60), false),
        (.hello(fw: "f", board: repeated("b", 14) + "\u{e9}", channels: 1, frameMs: 60), true),
        (.hello(fw: "f", board: repeated("b", 15) + "\u{e9}", channels: 1, frameMs: 60), false),
        (.hello(fw: "1\"2\\3", board: "b", channels: 1, frameMs: 60), true),
        (.hello(fw: "1\n2", board: "b", channels: 1, frameMs: 60), false),
        (.hello(fw: "f", board: "b", channels: 0, frameMs: 60), false),
        (.hello(fw: "f", board: "b", channels: 5, frameMs: 60), false),
        (.hello(fw: "f", board: "b", channels: 1, frameMs: 19), false),
        (.hello(fw: "f", board: "b", channels: 1, frameMs: 101), false),
        (.pttStart(stream: UInt32.max), true), (.pttStart(stream: 0), false),
        (.pttEnd(stream: UInt32.max, frames: UInt32.max, dropped: UInt32.max), true),
        (.pttEnd(stream: 0, frames: 0, dropped: 0), false),
        (.answer(id: repeated("A", 32), nonce: nonce, digest: digest, choice: 3), true),
        (.answer(id: repeated("A", 33), nonce: nonce, digest: digest, choice: 0), false),
        (.answer(id: "a", nonce: nonce, digest: digest, choice: 4), false),
        (.answer(id: "a", nonce: nonce, digest: String(digest.dropLast()), choice: 0), false),
        (.answer(id: "a", nonce: nonce, digest: digest + "0", choice: 0), false),
        (.answer(id: "a", nonce: nonce, digest: digest.uppercased(), choice: 0), false),
        (.played(reply: UInt32.max, complete: false), true), (.played(reply: 0, complete: true), false),
        (.status(battery: -1, charging: false, rssi: -127), true), (.status(battery: 100, charging: true, rssi: 0), true),
        (.status(battery: -2, charging: false, rssi: 0), false), (.status(battery: 101, charging: false, rssi: 0), false),
        (.status(battery: 0, charging: false, rssi: -128), false), (.status(battery: 0, charging: false, rssi: 1), false),
        (.setModel("Claude"), true), (.setModel(repeated("z", 40)), true), (.setModel(repeated("z", 41)), false),
        (.setModel(""), false), (.setModel("a b"), false), (.setModel("a\\b"), false),
    ]

    @Test func deviceFieldLimits() throws {
        for (index, (message, accepted)) in Self.deviceLimits.enumerated() {
            if accepted {
                let bytes = try LokiCodec.encode(message, seq: UInt32.max)
                #expect(bytes.count <= Loki.maxDeviceMessage)
                #expect(try LokiCodec.decodeDeviceMessage(bytes) == .message(seq: UInt32.max, message), "case \(index)")
            } else {
                #expect(throws: LokiError.self, "case \(index): \(message)") { try LokiCodec.encode(message, seq: 1) }
            }
        }
    }

    @Test func seqStartsAtOne() throws {
        #expect(throws: LokiError.self) { try LokiCodec.encode(BridgeMessage.stopAudio, seq: 0) }
        #expect(throws: LokiError.self) { try LokiCodec.encode(DeviceMessage.cancel, seq: 0) }
        #expect(try LokiCodec.encode(BridgeMessage.stopAudio, seq: UInt32.max)
            == Array("{\"t\":\"stop_audio\",\"seq\":4294967295}".utf8))
    }

    /// Keys in table order, no spaces, defaults left out, and only ", \ and line feed
    /// escaped: everything else is raw UTF-8.
    @Test func encodingIsExact() throws {
        let cases: [(BridgeMessage, String)] = [
            (.state(.idle, caption: "", progress: -1, queue: 0), #"{"t":"state","seq":1,"state":"idle"}"#),
            (.state(.idle, progress: 0), #"{"t":"state","seq":1,"state":"idle","progress":0}"#),
            (.state(.heard, caption: "c", queue: 3), #"{"t":"state","seq":1,"state":"heard","caption":"c","queue":3}"#),
            (.welcome(bridge: "B", voice: ""), #"{"t":"welcome","seq":1,"proto":1,"bridge":"B"}"#),
            (.caption(text: "a\"b\\c/d\ne\u{e9}\u{7f}", final: false),
             "{\"t\":\"caption\",\"seq\":1,\"text\":\"a\\\"b\\\\c/d\\ne\u{e9}\u{7f}\",\"final\":false}"),
            (Self.ask(detail: ""), #"{"t":"ask","seq":1,"id":"a1","nonce":"00112233445566778899aabbccddeeff","kind":"choose","title":"T","options":["a","b"],"timeout_ms":1000}"#),
            (.replyBegin(reply: 7), #"{"t":"reply_begin","seq":1,"reply":7,"rate":24000}"#),
        ]
        for (message, json) in cases {
            #expect(String(decoding: try LokiCodec.encode(message, seq: 1), as: UTF8.self) == json)
        }
        #expect(String(decoding: try LokiCodec.encode(DeviceMessage.hello(fw: "1\"", board: "b", channels: 2, frameMs: 20), seq: 1), as: UTF8.self)
            == #"{"t":"hello","seq":1,"proto":1,"app":"loki-agent","fw":"1\"","board":"b","rate":24000,"channels":2,"frame_ms":20}"#)
    }

    /// The canonical form escapes by byte, with uppercase hex.
    @Test func canonicalQuotingIsByByte() {
        #expect(Schema.quoted("a\"\\\n\t\u{7f}\u{e9}\u{1F984}~ ")
            == #""a\"\\\n\x09\x7F\xC3\xA9\xF0\x9F\xA6\x84~ ""#)
        #expect(Schema.quoted("") == "\"\"")
    }

    /// Metamorphic: a card's digest changes with anything the board shows, and the
    /// quoting keeps option boundaries from blurring.
    @Test func cardDigestSeparatesCards() {
        let base = Ask(id: "a", nonce: Self.nonce, kind: .choose, title: "T", detail: "D", options: ["a", "b"],
                       timeoutMs: 1000)
        var variants = [base]
        var other = base
        other.kind = .approve; variants.append(other)
        other = base; other.title = "T "; variants.append(other)
        other = base; other.detail = ""; variants.append(other)
        other = base; other.options = ["b", "a"]; variants.append(other)
        other = base; other.options = ["a\",\"b", "c"]; variants.append(other)
        other = base; other.options = ["a", "b", ""]; variants.append(other)
        other = base; other.title = "T\" detail=\"D"; other.detail = ""; variants.append(other)
        #expect(Set(variants.map(\.digest)).count == variants.count)
        // What the board does not show leaves it unchanged.
        other = base; other.id = "z"; other.nonce = String(repeating: "f", count: 32); other.timeoutMs = 5000
        #expect(other.digest == base.digest)
        #expect(base.digest.count == 64 && base.digest.allSatisfy { "0123456789abcdef".contains($0) })
    }
}
