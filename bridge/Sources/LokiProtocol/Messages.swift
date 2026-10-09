import CryptoKit

/// The unicorn's states (docs/loki-protocol.md).
public enum LokiState: String, CaseIterable, Sendable {
    case idle, listening, heard, thinking, working, asking, speaking, success, partial, failure
}

/// approve: two options, 0 allows and 1 denies. choose: two to four options.
public enum AskKind: String, CaseIterable, Sendable {
    case approve, choose
}

/// A question the board shows as a card and answers with a tap.
public struct Ask: Equatable, Sendable {
    public var id: String
    public var nonce: String
    public var kind: AskKind
    public var title: String
    public var detail: String
    public var options: [String]
    public var timeoutMs: UInt32

    public init(id: String, nonce: String, kind: AskKind, title: String, detail: String = "",
                options: [String], timeoutMs: UInt32) {
        self.id = id
        self.nonce = nonce
        self.kind = kind
        self.title = title
        self.detail = detail
        self.options = options
        self.timeoutMs = timeoutMs
    }

    /// What the board's answer must carry: the SHA-256, in lowercase hex, of the card
    /// in canonical form ("kind=… title=… detail=… options=[…]").
    public var digest: String {
        let card = "kind=\(kind.rawValue) title=\(Schema.quoted(title)) detail=\(Schema.quoted(detail)) "
            + "options=[" + options.map(Schema.quoted).joined(separator: ",") + "]"
        return SHA256.hash(data: Array(card.utf8)).map { byte in
            let digits = Array("0123456789abcdef")
            return String([digits[Int(byte >> 4)], digits[Int(byte & 0x0F)]])
        }.joined()
    }
}

/// What the bridge sends to the board.
public enum BridgeMessage: Equatable, Sendable {
    case welcome(bridge: String, voice: String = "")
    case state(LokiState, caption: String = "", progress: Int32 = -1, queue: UInt32 = 0)
    case caption(text: String, final: Bool)
    case ask(Ask)
    case askCancel(id: String)
    case replyBegin(reply: UInt32)
    case replyEnd(reply: UInt32, frames: UInt32)
    case stopAudio
    case settings(volume: UInt32)
}

/// What the board sends to the bridge.
public enum DeviceMessage: Equatable, Sendable {
    case hello(fw: String, board: String, channels: UInt32, frameMs: UInt32)
    case pttStart(stream: UInt32)
    case pttEnd(stream: UInt32, frames: UInt32, dropped: UInt32)
    case cancel
    case answer(id: String, nonce: String, digest: String, choice: UInt32)
    case played(reply: UInt32, complete: Bool)
    case status(battery: Int32, charging: Bool, rssi: Int32)
}

/// A decoded control message: a known one with its sequence number, or one of a type
/// this side does not know, which only advances the sequence.
public enum Decoded<Message: Equatable & Sendable>: Equatable, Sendable {
    case message(seq: UInt32, Message)
    case ignored(seq: UInt32)

    public var seq: UInt32 {
        switch self {
        case .message(let seq, _), .ignored(let seq): seq
        }
    }
}

public enum Loki {
    public static let protocolVersion: UInt32 = 1
    public static let sampleRate: UInt32 = 24_000
    public static let maxBridgeMessage = 2048
    public static let maxDeviceMessage = 512
    public static let path = "/loki/1"
    public static let defaultPort: UInt16 = 8766
}
