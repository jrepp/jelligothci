import CryptoKit
import Testing
@testable import LokiProtocol

/// The board's C codec and the bridge's Swift codec must read every message the same
/// way (docs/loki-protocol.md): the same verdict, the same canonical form, and for a
/// question the same card digest. Inputs come from fixed seeds; a difference names the
/// seed, the case, and the input's hex, which belongs in tests/vectors/loki-1.tsv as a
/// regression vector once the codecs agree again.

/// Inputs each decoded by both codecs; returns how many were ok, ignored, rejected.
private func compareDecoders(_ inputs: [[UInt8]], seed: UInt64, _ label: String) throws
    -> (ok: Int, ignored: Int, rejected: Int) {
    let board = try BoardCodec.run(inputs.map { "d " + BoardCodec.hex($0) })
    var mismatches: [String] = []
    var counts = (ok: 0, ignored: 0, rejected: 0)
    for (index, input) in inputs.enumerated() {
        let bridge = BoardCodec.swiftDecode(input)
        if bridge != board[index] {
            mismatches.append("\(label) seed \(seed) case \(index): input \(BoardCodec.hex(input))\n"
                + "  board:  \(board[index])\n  bridge: \(bridge)")
        }
        if bridge.hasPrefix("ok") { counts.ok += 1 } else if bridge.hasPrefix("ignore") { counts.ignored += 1 } else {
            counts.rejected += 1
        }
    }
    #expect(mismatches.isEmpty, "\(mismatches.count) differences, first: \(mismatches.prefix(5).joined(separator: "\n"))")
    return counts
}

@Test func decodersAgreeOnMutatedVectors() throws {
    let seed: UInt64 = 0x5EED_0001
    var mutator = Mutator(random: Random(state: seed))
    let vectors = Vector.all.filter { !$0.isAudio }.map(\.bytes)
    let inputs = vectors + (0..<30_000).map { _ in
        let vector = mutator.random.pick(vectors)
        return mutator.mutate(vector)
    }
    let counts = try compareDecoders(inputs, seed: seed, "mutated")
    #expect(counts.ok > 500 && counts.ignored > 1_000 && counts.rejected > 10_000, "\(counts)")
}

@Test func decodersAgreeOnGeneratedMessages() throws {
    let seed: UInt64 = 0x5EED_0002
    var grammar = MessageGrammar(random: Random(state: seed))
    let inputs = (0..<30_000).map { _ in grammar.message() }
    let counts = try compareDecoders(inputs, seed: seed, "generated")
    #expect(counts.ok > 3_000 && counts.ignored > 1_000 && counts.rejected > 3_000, "\(counts)")
}

/// Messages near the 2,048-byte limit, padded with whitespace or an unknown member.
@Test func decodersAgreeAtTheSizeLimit() throws {
    let seed: UInt64 = 0x5EED_0003
    var grammar = MessageGrammar(random: Random(state: seed))
    let short = Vector.all.filter { $0.direction == "b2d" && !$0.isAudio && $0.bytes.count < 1_000 }
    var inputs: [[UInt8]] = []
    for vector in short {
        for size in 2_044...2_052 {
            var text = String(decoding: vector.bytes, as: UTF8.self)
            grammar.pad(&text, to: size)
            inputs.append(Array(text.utf8))
        }
    }
    let counts = try compareDecoders(inputs, seed: seed, "padded")
    #expect(counts.ok > 100 && counts.rejected > 100, "\(counts)")
}

/// Every message the bridge's encoder writes, the board decodes to the same canonical
/// form and card digest.
@Test func boardDecodesWhatTheBridgeEncodes() throws {
    var random = Random(state: 0x5EED_0004)
    var sent: [(BridgeMessage, UInt32, [UInt8])] = []
    for _ in 0..<10_000 {
        let seq = UInt32(1 + random.below(Int(UInt32.max)))
        let message = random.bridgeMessage()
        sent.append((message, seq, try LokiCodec.encode(message, seq: seq)))
    }
    for message in EncoderEdgeTests.largestBridgeMessages {
        sent.append((message, UInt32.max, try LokiCodec.encode(message, seq: UInt32.max)))
    }
    let board = try BoardCodec.run(sent.map { "d " + BoardCodec.hex($0.2) })
    for (index, (message, seq, bytes)) in sent.enumerated() {
        var expected = "ok\t" + LokiCodec.canonical(message, seq: seq)
        if case .ask(let ask) = message { expected += "\t" + ask.digest }
        #expect(board[index] == expected, "\(String(decoding: bytes, as: UTF8.self))")
    }
}

private extension Random {
    /// Text the board's encoder may or may not accept: mostly printable ASCII, with
    /// the characters it refuses mixed in.
    mutating func deviceText(maxBytes: Int) -> String {
        let pieces = ["a", "Z", "0", " ", ".", "-", "~", "/", "{", "\"", "\\", "\u{e9}", "\u{7f}", "\t", "\n"]
        let plain = below(3) != 0
        return String((0..<below(maxBytes + 3)).map { _ in plain ? "v" : Character(pick(pieces)) })
    }

    mutating func edgeDeviceMessage() -> DeviceMessage {
        let u32: [UInt32] = [0, 1, 2, UInt32.max, UInt32.max - 1, UInt32(below(100_000))]
        let hexDigits = Array("0123456789abcdef")
        func hex(_ count: Int, _ random: inout Random) -> String {
            String((0..<count).map { _ in random.pick(hexDigits) })
        }
        switch below(8) {
        case 0:
            return .hello(fw: deviceText(maxBytes: 40), board: deviceText(maxBytes: 16),
                          channels: UInt32(below(6)), frameMs: UInt32(18 + below(85)))
        case 1: return .pttStart(stream: pick(u32))
        case 2: return .pttEnd(stream: pick(u32), frames: pick(u32), dropped: pick(u32))
        case 3: return .cancel
        case 4:
            let id = below(5) == 0 ? pick(["", "a b", String(repeating: "i", count: 33), "\u{e9}"]) : identifier()
            let nonceLength = below(8) == 0 ? pick([31, 33]) : 32
            let digestLength = below(8) == 0 ? pick([63, 65]) : 64
            let nonce = hex(nonceLength, &self), digest = hex(digestLength, &self)
            let upperNonce = below(10) == 0, upperDigest = below(10) == 0
            return .answer(id: id, nonce: upperNonce ? nonce.uppercased() : nonce,
                           digest: upperDigest ? digest.uppercased() : digest, choice: UInt32(below(6)))
        case 5: return .played(reply: pick(u32), complete: below(2) == 0)
        case 6:
            let pieces = ["a", "Z", "0", ".", ":", "/", "+", "-", "_", " ", "\"", "\\", "\u{e9}", ","]
            let plain = below(3) != 0
            return .setModel(String((0..<below(43)).map { _ in plain ? "m" : Character(pick(pieces)) }))
        default:
            return .status(battery: Int32(below(105)) - 3, charging: below(2) == 0, rssi: Int32(3 - below(133)))
        }
    }
}

/// The board's encoder and the simulated device's write the same bytes for every
/// message both accept. The board's accepts less: its strings are printable ASCII
/// without quotes or backslashes, so it never needs to escape. Everything it writes
/// decodes back to the message it was given.
@Test func encodersAgree() throws {
    var random = Random(state: 0x5EED_0005)
    let cases = (0..<20_000).map { _ in
        let message = random.edgeDeviceMessage(), other = UInt32(1 + random.below(1_000_000))
        return (message, random.pick([0, 1, UInt32.max, other, other]))
    }
    let board = try BoardCodec.run(cases.map { BoardCodec.encodeCommand($0.0, seq: $0.1) })
    var bothAccepted = 0
    for (index, (message, seq)) in cases.enumerated() {
        let bridge = try? LokiCodec.encode(message, seq: seq)
        let line = board[index]
        if line == "reject" {
            if bridge != nil {
                #expect(BoardTextRules.outsideBoardStrings(message), "board refused \(message) seq \(seq)")
            }
            continue
        }
        let bytes = BoardCodec.unhex(line.dropFirst(3))
        #expect(bridge == bytes, "\(message) seq \(seq): board \(String(decoding: bytes, as: UTF8.self))")
        #expect(try LokiCodec.decodeDeviceMessage(bytes) == .message(seq: seq, message))
        bothAccepted += 1
    }
    #expect(bothAccepted > 5_000 && bothAccepted < cases.count - 2_000, "\(bothAccepted)")
}

enum BoardTextRules {
    /// True when a string field holds a byte the board's encoder refuses to send.
    static func outsideBoardStrings(_ message: DeviceMessage) -> Bool {
        let strings: [String] = switch message {
        case .hello(let fw, let board, _, _): [fw, board]
        case .answer(let id, let nonce, let digest, _): [id, nonce, digest]
        default: []
        }
        return strings.contains { $0.utf8.contains { $0 < 0x20 || $0 > 0x7E || $0 == 0x22 || $0 == 0x5C } }
    }
}

@Test func audioParsersAgree() throws {
    var random = Random(state: 0x5EED_0006)
    var cases: [([UInt8], AudioFrame.Kind, Int)] = []
    for _ in 0..<8_000 {
        // Mostly whole sample frames for 1 to 4 channels (12 bytes divides by 2, 4, 6
        // and 8), with sizes on and past each limit.
        let small = random.below(64), whole = 12 + 24 * random.below(201)
        let size = random.pick([0, 11, 12, 13, 14, 16, 18, 24, 4_812, 4_813, 4_814, 4_818, small, whole, whole, whole])
        var frame = (0..<size).map { _ in UInt8(truncatingIfNeeded: random.next()) }
        let kind: AudioFrame.Kind = random.pick([.mic, .reply])
        if size >= 12, random.below(5) != 0 {
            frame[0] = 0x4C
            frame[1] = 0x4B
            frame[2] = random.pick([kind.rawValue, kind.rawValue, kind.rawValue, 1, 2, 0, 3])
            frame[3] = random.pick([0, 1, 0, 1, 2, 0x80])
            if random.below(6) == 0 { for i in 4..<8 { frame[i] = 0 } }
        }
        cases.append((frame, kind, random.pick([1, 1, 2, 3, 4, 0, 5])))
    }
    let board = try BoardCodec.run(cases.map { "a \($0.1.rawValue) \($0.2) " + BoardCodec.hex($0.0) })
    var accepted = 0
    for (index, (frame, kind, channels)) in cases.enumerated() {
        let bridge = BoardCodec.swiftAudio(frame, kind: kind, channels: channels)
        #expect(bridge == board[index], "frame \(BoardCodec.hex(Array(frame.prefix(16)))) size \(frame.count)")
        if bridge != "reject" { accepted += 1 }
    }
    #expect(accepted > 400, "\(accepted)")
}

/// The board's SHA-256 against CryptoKit, with every length through four blocks
/// (each padding boundary: 55, 56, 63, 64, 119, 120, ...) and some long inputs.
@Test func sha256MatchesCryptoKit() throws {
    var random = Random(state: 0x5EED_0007)
    let lengths = Array(0...260) + (0..<200).map { _ in random.below(5_000) }
    let inputs = lengths.map { length in (0..<length).map { _ in UInt8(truncatingIfNeeded: random.next()) } }
    let board = try BoardCodec.run(inputs.map { "h " + BoardCodec.hex($0) })
    for (index, input) in inputs.enumerated() {
        let expected = "ok\t" + String(BoardCodec.hex(Array(SHA256.hash(data: input))).dropFirst())
        #expect(board[index] == expected, "length \(input.count)")
    }
}
