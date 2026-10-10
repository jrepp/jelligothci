import Testing
@testable import LokiProtocol

/// Seeded generator, so a failure names a replayable case.
struct Random {
    var state: UInt64
    mutating func next() -> UInt64 {
        state ^= state << 13
        state ^= state >> 7
        state ^= state << 17
        return state
    }
    mutating func below(_ bound: Int) -> Int { Int(next() % UInt64(bound)) }
    mutating func pick<T>(_ list: [T]) -> T { list[below(list.count)] }

    /// Text that exercises escapes and multi-byte UTF-8, within maxBytes.
    mutating func text(maxBytes: Int, lineFeeds: Bool) -> String {
        let pieces = ["a", "Z", " ", "\"", "\\", "/", "é", "…", "🦄", "{", "}", "[", "]", ":", ","]
            + (lineFeeds ? ["\n"] : [])
        var out = ""
        for _ in 0..<below(24) {
            let piece = pick(pieces)
            if out.utf8.count + piece.utf8.count > maxBytes { break }
            out += piece
        }
        return out
    }

    mutating func identifier() -> String {
        let chars = Array("abcXYZ019_-")
        return String((0..<(1 + below(32))).map { _ in pick(chars) })
    }

    mutating func hex(_ length: Int) -> String {
        String((0..<length).map { _ in pick(Array("0123456789abcdef")) })
    }

    mutating func bridgeMessage() -> BridgeMessage {
        switch below(9) {
        case 0: return .welcome(bridge: text(maxBytes: 40, lineFeeds: false), voice: text(maxBytes: 40, lineFeeds: false))
        case 1:
            return .state(pick(LokiState.allCases), caption: text(maxBytes: 200, lineFeeds: true),
                          progress: Int32(below(1002)) - 1, queue: UInt32(below(1000)))
        case 2: return .caption(text: text(maxBytes: 400, lineFeeds: true), final: below(2) == 0)
        case 3:
            let kind = pick(AskKind.allCases)
            let count = kind == .approve ? 2 : 2 + below(3)
            return .ask(Ask(id: identifier(), nonce: hex(32), kind: kind, title: text(maxBytes: 120, lineFeeds: false),
                            detail: text(maxBytes: 600, lineFeeds: true),
                            options: (0..<count).map { _ in text(maxBytes: 40, lineFeeds: false) },
                            timeoutMs: UInt32(1000 + below(599_001))))
        case 4: return .askCancel(id: identifier())
        case 5: return .replyBegin(reply: UInt32(1 + below(Int(UInt32.max - 1))))
        case 6: return .replyEnd(reply: UInt32(1 + below(1000)), frames: UInt32(below(Int(UInt32.max))))
        case 7: return .stopAudio
        default: return .settings(volume: UInt32(below(101)))
        }
    }

    mutating func deviceMessage() -> DeviceMessage {
        switch below(7) {
        case 0:
            return .hello(fw: text(maxBytes: 40, lineFeeds: false), board: text(maxBytes: 16, lineFeeds: false),
                          channels: UInt32(1 + below(4)), frameMs: UInt32(20 + below(81)))
        case 1: return .pttStart(stream: UInt32(1 + below(1000)))
        case 2: return .pttEnd(stream: UInt32(1 + below(1000)), frames: UInt32(below(5000)), dropped: UInt32(below(9)))
        case 3: return .cancel
        case 4: return .answer(id: identifier(), nonce: hex(32), digest: hex(64), choice: UInt32(below(4)))
        case 5: return .played(reply: UInt32(1 + below(1000)), complete: below(2) == 0)
        default: return .status(battery: Int32(below(102)) - 1, charging: below(2) == 0, rssi: -Int32(below(128)))
        }
    }
}

@Test func validMessagesRoundTrip() throws {
    var random = Random(state: 0x9E37_79B9_7F4A_7C15)
    for round in 0..<20_000 {
        let seq = UInt32(1 + random.below(Int(UInt32.max)))
        let toBoard = random.bridgeMessage()
        let decoded = try LokiCodec.decodeBridgeMessage(try LokiCodec.encode(toBoard, seq: seq))
        #expect(decoded == .message(seq: seq, toBoard), "round \(round)")
        let toBridge = random.deviceMessage()
        let back = try LokiCodec.decodeDeviceMessage(try LokiCodec.encode(toBridge, seq: seq))
        #expect(back == .message(seq: seq, toBridge), "round \(round)")
    }
}

/// Mutated vectors never crash the decoder, and whatever it accepts re-encodes to a
/// message that decodes the same way (the canonical forms agree).
@Test func mutatedVectorsDecodeConsistently() {
    var random = Random(state: 0xD1B5_4A32_D192_ED03)
    let inputs = Vector.all.filter { !$0.isAudio }
    let interesting = Array("{}[]\":,\\/ \n\t0123456789-+.eEtrufalsn".utf8) + [0x00, 0x7F, 0xC3, 0xA9, 0xED, 0xF4, 0xFF]
    var accepted = 0
    for _ in 0..<60_000 {
        let vector = random.pick(inputs)
        var bytes = vector.bytes
        for _ in 0..<(1 + random.below(4)) {
            let at = bytes.isEmpty ? 0 : random.below(bytes.count)
            switch random.below(5) {
            case 0 where !bytes.isEmpty: bytes[at] = random.pick(interesting)
            case 1: bytes.insert(random.pick(interesting), at: at)
            case 2 where !bytes.isEmpty: bytes.remove(at: at)
            case 3: bytes.insert(contentsOf: bytes[at..<min(bytes.count, at + 1 + random.below(24))], at: at)
            default: bytes = Array(bytes.prefix(at))
            }
        }
        if vector.direction == "b2d", case .message(let seq, let message)? = try? LokiCodec.decodeBridgeMessage(bytes) {
            let again = try? LokiCodec.decodeBridgeMessage(try LokiCodec.encode(message, seq: seq))
            #expect(again == .message(seq: seq, message))
            accepted += 1
        }
        if vector.direction == "d2b", case .message(let seq, let message)? = try? LokiCodec.decodeDeviceMessage(bytes) {
            let again = try? LokiCodec.decodeDeviceMessage(try LokiCodec.encode(message, seq: seq))
            #expect(again == .message(seq: seq, message))
            accepted += 1
        }
    }
    #expect(accepted > 500)
}

@Test func encoderRefusesWhatTheProtocolForbids() {
    let nonce = "00112233445566778899aabbccddeeff"
    let bad: [BridgeMessage] = [
        .state(.idle, caption: String(repeating: "x", count: 201)),
        .state(.idle, caption: "tab\there"),
        .state(.idle, progress: 1001),
        .caption(text: "bell\u{7}", final: true),
        .ask(Ask(id: "a b", nonce: nonce, kind: .choose, title: "t", options: ["a", "b"], timeoutMs: 1000)),
        .ask(Ask(id: "a", nonce: nonce.uppercased(), kind: .choose, title: "t", options: ["a", "b"], timeoutMs: 1000)),
        .ask(Ask(id: "a", nonce: nonce, kind: .approve, title: "t", options: ["a", "b", "c"], timeoutMs: 1000)),
        .ask(Ask(id: "a", nonce: nonce, kind: .choose, title: "two\nlines", options: ["a", "b"], timeoutMs: 1000)),
        .ask(Ask(id: "a", nonce: nonce, kind: .choose, title: "t", options: ["a"], timeoutMs: 1000)),
        .ask(Ask(id: "a", nonce: nonce, kind: .choose, title: "t", options: ["a", "b"], timeoutMs: 999)),
        .askCancel(id: ""),
        .replyBegin(reply: 0),
        .settings(volume: 101),
        .welcome(bridge: String(repeating: "b", count: 41)),
    ]
    for message in bad {
        #expect(throws: LokiError.self, "\(message)") { try LokiCodec.encode(message, seq: 1) }
    }
    #expect(throws: LokiError.self) { try LokiCodec.encode(.stopAudio, seq: 0) }
    #expect(throws: LokiError.self) {
        try LokiCodec.encode(.hello(fw: "1", board: "b", channels: 5, frameMs: 60), seq: 1)
    }
}

@Test func loggableTextCannotDriveOrRearrangeATerminal() {
    let hostile = "a\u{1B}]0;title\u{7}b\u{9B}2Jc\u{7F}d\u{85}e\u{202E}f\u{2066}g\u{2028}h\u{2029}i\u{200B}j\nk"
    #expect(LokiText.loggable(hostile, maxBytes: 200) == "a ]0;title b 2Jc d e f g h i j k")
    #expect(LokiText.loggable("Tests pass. 🦄 café", maxBytes: 200) == "Tests pass. 🦄 café")
    // Nothing a terminal treats as a control or invisible survives, on any input.
    var random = Random(state: 11)
    for _ in 0..<2000 {
        let raw = random.text(maxBytes: 900, lineFeeds: true) + "\u{1B}\u{9B}\u{202E}\u{2028}"
        let line = LokiText.loggable(raw, maxBytes: 300)
        #expect(line.utf8.count <= 300)
        #expect(line.unicodeScalars.allSatisfy {
            ![.control, .format, .lineSeparator, .paragraphSeparator].contains($0.properties.generalCategory)
        })
    }
}

@Test func textIsFittedToItsField() {
    #expect(LokiText.fitted("run\tthe\u{7}tests", maxBytes: 40) == "run the tests")
    #expect(LokiText.fitted("one\ntwo", maxBytes: 40) == "one two")
    #expect(LokiText.fitted("one\ntwo", maxBytes: 40, lineFeeds: true) == "one\ntwo")
    let long = String(repeating: "é", count: 150)  // 300 bytes
    let fitted = LokiText.fitted(long, maxBytes: 200)
    #expect(fitted.utf8.count <= 200 && fitted.hasSuffix("\u{2026}") && fitted.hasPrefix("éé"))
    #expect(LokiText.fitted("🦄🦄", maxBytes: 5) == "\u{2026}")
    #expect(LokiText.fitted("abc", maxBytes: 2) == "")
    // Whatever comes out is accepted by the encoder.
    var random = Random(state: 7)
    for _ in 0..<2000 {
        let raw = random.text(maxBytes: 900, lineFeeds: true) + "\t\u{1}"
        let caption = LokiText.fitted(raw, maxBytes: 200, lineFeeds: true)
        #expect(throws: Never.self) { try LokiCodec.encode(.state(.idle, caption: caption), seq: 1) }
    }
}
