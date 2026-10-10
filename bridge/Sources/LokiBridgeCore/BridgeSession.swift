import LokiProtocol

/// What the bridge learned from a board's hello.
public struct DeviceHello: Equatable, Sendable {
    public let fw: String
    public let board: String
    public let channels: Int
    public let frameMs: Int
}

/// Why a connection must close, with the WebSocket close code it is closed with.
public struct SessionError: Error, Equatable, Sendable, CustomStringConvertible {
    public let code: UInt16
    public let reason: String
    public var description: String { "\(reason) (\(code))" }

    static func protocolError(_ reason: String) -> SessionError { SessionError(code: 1002, reason: reason) }
    static func badAudio(_ reason: String) -> SessionError { SessionError(code: 1003, reason: reason) }
}

/// Messages per second a peer may send: generous for real use (a 60 ms audio frame
/// is about 17 a second), small enough to stop a runaway peer.
struct RateLimit: Sendable {
    let perSecond: Int
    private var windowStart = -Double.infinity
    private var count = 0

    init(perSecond: Int) { self.perSecond = perSecond }

    mutating func admit(at now: Double) -> Bool {
        if now - windowStart >= 1 {
            windowStart = now
            count = 0
        }
        count += 1
        return count <= perSecond
    }
}

/// Per-direction sequence numbers: the first message is 1, each next one 1 more.
struct Sequence: Sendable {
    private(set) var last: UInt32 = 0

    func isNext(_ seq: UInt32) -> Bool { last < .max && seq == last + 1 }
    var next: UInt32? { last < .max ? last + 1 : nil }
    mutating func advance() { last += 1 }
}

/// The bridge's side of one connection (docs/loki-protocol.md): hello first, sequence
/// numbers in both directions, one microphone stream at a time with rising stream
/// numbers, and rate limits. Pure: the link server feeds it frames, so it is tested
/// without a network. `now` is seconds on any monotonic clock.
public struct BridgeSession: Sendable {
    public enum Input: Equatable, Sendable {
        case hello(DeviceHello)
        case message(DeviceMessage)
        case audio(AudioFrame)
        /// An ignored message type, or a frame for a stream that is not open.
        case nothing
    }

    public private(set) var hello: DeviceHello?
    public private(set) var openStream: UInt32?
    private var lastStream: UInt32 = 0
    private var received = Sequence()
    private var sent = Sequence()
    private var control = RateLimit(perSecond: 50)
    private var audio = RateLimit(perSecond: 60)

    public init() {}

    public mutating func receiveText(_ bytes: [UInt8], at now: Double) throws(SessionError) -> Input {
        guard control.admit(at: now) else { throw .protocolError("too many control messages") }
        let decoded: Decoded<DeviceMessage>
        do {
            decoded = try LokiCodec.decodeDeviceMessage(bytes)
        } catch {
            throw bytes.count > Loki.maxDeviceMessage ? SessionError(code: 1009, reason: error.reason)
                : .protocolError(error.reason)
        }
        guard received.isNext(decoded.seq) else { throw .protocolError("sequence \(decoded.seq) out of order") }
        received.advance()
        guard case .message(_, let message) = decoded else {
            guard hello != nil else { throw .protocolError("hello must come first") }
            return .nothing
        }
        return try accept(message)
    }

    private mutating func accept(_ message: DeviceMessage) throws(SessionError) -> Input {
        if case .hello(let fw, let board, let channels, let frameMs) = message {
            guard hello == nil else { throw .protocolError("hello repeated") }
            let info = DeviceHello(fw: fw, board: board, channels: Int(channels), frameMs: Int(frameMs))
            hello = info
            return .hello(info)
        }
        guard hello != nil else { throw .protocolError("hello must come first") }
        switch message {
        case .pttStart(let stream):
            guard openStream == nil, stream > lastStream else { throw .protocolError("stream \(stream) cannot start") }
            openStream = stream
            lastStream = stream
        case .pttEnd(let stream, _, _):
            guard openStream == stream else { throw .protocolError("stream \(stream) is not open") }
            openStream = nil
        default:
            break
        }
        return .message(message)
    }

    public mutating func receiveBinary(_ bytes: [UInt8], at now: Double) throws(SessionError) -> Input {
        guard let hello else { throw .protocolError("hello must come first") }
        guard audio.admit(at: now) else { throw .badAudio("too many audio frames") }
        // A larger message closes with 1009, like an oversized control message.
        guard bytes.count <= AudioFrame.headerSize + AudioFrame.maxPayload else {
            throw SessionError(code: 1009, reason: "audio frame too long")
        }
        let frame: AudioFrame
        do {
            frame = try AudioFrame(parsing: bytes, expected: .mic, channels: hello.channels)
        } catch {
            throw .badAudio(error.reason)
        }
        return frame.stream == openStream ? .audio(frame) : .nothing
    }

    /// Encodes the next message for the board. The first is always welcome, and only
    /// after the board's hello.
    public mutating func encode(_ message: BridgeMessage) throws(SessionError) -> [UInt8] {
        guard hello != nil else { throw .protocolError("nothing is sent before hello") }
        let isWelcome = if case .welcome = message { true } else { false }
        guard isWelcome == (sent.last == 0) else { throw .protocolError("welcome comes first, once") }
        guard let seq = sent.next else { throw .protocolError("sequence exhausted") }
        do {
            let bytes = try LokiCodec.encode(message, seq: seq)
            sent.advance()
            return bytes
        } catch {
            throw .protocolError(error.reason)
        }
    }
}
