import Foundation
import Testing
@testable import LokiProtocol

/// tests/vectors/loki-1.tsv, shared with the board's C tests (docs/loki-protocol.md).
struct Vector: Sendable, CustomStringConvertible {
    let name, direction, verdict, input: String
    let canonical: String?
    var description: String { name }

    var isAudio: Bool { input.hasPrefix("hex:") }

    /// The input's bytes: hex for audio and json-hex inputs, the text itself otherwise.
    var bytes: [UInt8] {
        for prefix in ["hex:", "json-hex:"] where input.hasPrefix(prefix) {
            return Vector.unhex(String(input.dropFirst(prefix.count)))
        }
        return Array(input.utf8)
    }

    static func unhex(_ hex: String) -> [UInt8] {
        let digits = Array(hex.utf8)
        return stride(from: 0, to: digits.count, by: 2).map { i in
            UInt8(JSONReader.hexValue(digits[i])! << 4 | JSONReader.hexValue(digits[i + 1])!)
        }
    }

    static let all: [Vector] = {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../../tests/vectors/loki-1.tsv").standardized
        let text = try! String(contentsOf: url, encoding: .utf8)
        return text.split(separator: "\n", omittingEmptySubsequences: true)
            .filter { !$0.hasPrefix("#") }
            .map { line in
                let fields = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
                precondition(fields.count == 4 || fields.count == 5, "malformed vector \(line)")
                return Vector(name: fields[0], direction: fields[1], verdict: fields[2], input: fields[3],
                              canonical: fields.count == 5 ? fields[4] : nil)
            }
    }()

    static func named(_ name: String) -> Vector { all.first { $0.name == name }! }
}

/// The canonical form of whatever a vector decodes to, or "ignored", or "rejected".
private func outcome(_ vector: Vector) -> String {
    let bytes = vector.bytes
    let toBoard = vector.direction == "b2d"
    do {
        if vector.isAudio {
            return try AudioFrame(parsing: bytes, expected: toBoard ? .reply : .mic, channels: 1).canonical
        }
        if toBoard {
            switch try LokiCodec.decodeBridgeMessage(bytes) {
            case .message(let seq, let message): return LokiCodec.canonical(message, seq: seq)
            case .ignored: return "ignored"
            }
        }
        switch try LokiCodec.decodeDeviceMessage(bytes) {
        case .message(let seq, let message): return LokiCodec.canonical(message, seq: seq)
        case .ignored: return "ignored"
        }
    } catch {
        return "rejected"
    }
}

@Test func theVectorsLoad() {
    #expect(Vector.all.count >= 160)
    #expect(Set(Vector.all.map(\.name)).count == Vector.all.count)
}

/// Both directions: the bridge decodes what the board sends, and its simulated device
/// decodes what the bridge sends.
@Test(arguments: Vector.all)
func decodesAsTheVectorSays(_ vector: Vector) {
    let expected = switch vector.verdict {
    case "ok": vector.canonical!
    case "ignore": "ignored"
    default: "rejected"
    }
    #expect(outcome(vector) == expected)
}

private func expectSent(_ bytes: [UInt8], direction: String) {
    let text = String(decoding: bytes, as: UTF8.self)
    let present = Vector.all.contains { $0.direction == direction && $0.verdict == "ok" && $0.input == text }
    #expect(present, "encoded message is not a \(direction) vector: \(text)")
}

/// Every message the bridge's encoder writes here is a b2d vector, which the board's C
/// tests decode.
@Test func encodesWhatTheBoardDecodes() throws {
    let approve = Ask(id: "a1", nonce: "00112233445566778899aabbccddeeff", kind: .approve,
                      title: "Run outside the sandbox?", detail: "npm run dev\nin codergotchi",
                      options: ["Allow", "Deny"], timeoutMs: 60_000)
    let choose = Ask(id: "Q_9-z", nonce: "ffeeddccbbaa99887766554433221100", kind: .choose, title: "Which project?",
                     options: ["codergotchi", "chatter", "Atrium", "jev"], timeoutMs: 1000)
    let messages: [(BridgeMessage, UInt32)] = [
        (.welcome(bridge: "Loki Bridge 0.1.0", voice: "Scar"), 1),
        (.welcome(bridge: "Loki Bridge"), 1),
        (.state(.idle), 2),
        (.state(.working, caption: "Running tests\u{2026}", progress: 250, queue: 2), 3),
        (.state(.success, progress: 1000), 6),
        (.state(.listening, progress: 0, queue: 999), 7),
        (.caption(text: "run the tests in codergotchi", final: false), 8),
        (.ask(approve), 10),
        (.ask(choose), 11),
        (.askCancel(id: "a1"), 12),
        (.replyBegin(reply: 1), 13),
        (.replyEnd(reply: UInt32.max, frames: 0), 14),
        (.stopAudio, 15),
        (.settings(volume: 35), 16),
    ]
    for (message, seq) in messages {
        expectSent(try LokiCodec.encode(message, seq: seq), direction: "b2d")
    }
}

/// The simulated device's encoder matches the board's: both write the d2b vectors.
@Test func simulatedDeviceEncodesLikeTheBoard() throws {
    let answer = Vector.named("answer")
    let digest = Ask(id: "a1", nonce: "00112233445566778899aabbccddeeff", kind: .approve,
                     title: "Run outside the sandbox?", detail: "npm run dev\nin codergotchi",
                     options: ["Allow", "Deny"], timeoutMs: 60_000).digest
    #expect(answer.input.contains(digest))
    let messages: [(DeviceMessage, UInt32)] = [
        (.hello(fw: "0.10.0", board: "1.75c", channels: 1, frameMs: 60), 1),
        (.pttStart(stream: 1), 2),
        (.pttEnd(stream: 1, frames: 50, dropped: 2), 3),
        (.cancel, 4),
        (.answer(id: "a1", nonce: "00112233445566778899aabbccddeeff", digest: digest, choice: 0), 5),
        (.played(reply: 1, complete: true), 6),
        (.status(battery: 87, charging: false, rssi: -61), 7),
        (.status(battery: -1, charging: true, rssi: -127), 8),
    ]
    for (message, seq) in messages {
        expectSent(try LokiCodec.encode(message, seq: seq), direction: "d2b")
    }
}

@Test func audioFramesRoundTrip() throws {
    for name in ["mic-frame", "mic-last", "reply-frame", "reply-last"] {
        let vector = Vector.named(name)
        let kind: AudioFrame.Kind = vector.direction == "d2b" ? .mic : .reply
        let frame = try AudioFrame(parsing: vector.bytes, expected: kind, channels: 1)
        #expect(frame.bytes == vector.bytes)
    }
    let stereo = AudioFrame(kind: .mic, final: false, stream: 3, seq: 0, payload: [1, 2, 3, 4])
    #expect(throws: Never.self) { try AudioFrame(parsing: stereo.bytes, expected: .mic, channels: 2) }
    #expect(throws: LokiError.self) { try AudioFrame(parsing: Array(stereo.bytes.dropLast(2)), expected: .mic, channels: 2) }
    #expect(throws: LokiError.self) { try AudioFrame(parsing: stereo.bytes, expected: .mic, channels: 0) }
}
