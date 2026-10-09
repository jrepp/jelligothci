/// A loki/1 audio frame: a 12-byte header and 16-bit little-endian samples.
public struct AudioFrame: Equatable, Sendable {
    public enum Kind: UInt8, Sendable {
        case mic = 1    // board to bridge
        case reply = 2  // bridge to board
    }

    public static let headerSize = 12
    public static let maxPayload = 4800

    public var kind: Kind
    /// The stream's last frame.
    public var final: Bool
    /// The ptt_start stream or the reply number; never 0.
    public var stream: UInt32
    /// 0 for a stream's first frame.
    public var seq: UInt32
    public var payload: [UInt8]

    public init(kind: Kind, final: Bool, stream: UInt32, seq: UInt32, payload: [UInt8]) {
        self.kind = kind
        self.final = final
        self.stream = stream
        self.seq = seq
        self.payload = payload
    }

    /// Validates a whole frame of the expected kind with `channels` interleaved.
    public init(parsing bytes: [UInt8], expected: Kind, channels: Int) throws(LokiError) {
        guard (1...4).contains(channels), bytes.count > Self.headerSize,
              bytes.count <= Self.headerSize + Self.maxPayload
        else { throw LokiError("bad audio frame size") }
        let stream = Self.le32(bytes, 4)
        let payload = Array(bytes[Self.headerSize...])
        guard bytes[0] == 0x4C, bytes[1] == 0x4B, bytes[2] == expected.rawValue, bytes[3] <= 1, stream != 0,
              payload.count % (2 * channels) == 0
        else { throw LokiError("bad audio frame") }
        self.init(kind: expected, final: bytes[3] == 1, stream: stream, seq: Self.le32(bytes, 8), payload: payload)
    }

    public var bytes: [UInt8] {
        var out: [UInt8] = [0x4C, 0x4B, kind.rawValue, final ? 1 : 0]
        for value in [stream, seq] {
            for shift in stride(from: 0, to: 32, by: 8) { out.append(UInt8(truncatingIfNeeded: value >> shift)) }
        }
        return out + payload
    }

    public var canonical: String {
        "audio kind=\(kind == .mic ? "mic" : "reply") final=\(final) stream=\(stream) seq=\(seq) bytes=\(payload.count)"
    }

    private static func le32(_ bytes: [UInt8], _ offset: Int) -> UInt32 {
        (0..<4).reduce(0) { $0 | UInt32(bytes[offset + $1]) << (8 * $1) }
    }
}
