import Foundation
import LokiProtocol
import Network

/// The board's side of one connection, as the simulated device keeps it: hello first,
/// welcome first back, sequence numbers both ways, and one reply stream at a time.
public struct DeviceSession: Sendable {
    public enum Input: Equatable, Sendable {
        case welcome(bridge: String, voice: String)
        case message(BridgeMessage)
        case audio(AudioFrame)
        case nothing
    }

    public private(set) var welcomed = false
    public private(set) var openReply: UInt32?
    private var received = Sequence()
    private var sent = Sequence()

    public init() {}

    public mutating func receiveText(_ bytes: [UInt8]) throws(SessionError) -> Input {
        let decoded: Decoded<BridgeMessage>
        do {
            decoded = try LokiCodec.decodeBridgeMessage(bytes)
        } catch {
            throw .protocolError(error.reason)
        }
        guard received.isNext(decoded.seq) else { throw .protocolError("sequence \(decoded.seq) out of order") }
        received.advance()
        guard case .message(_, let message) = decoded else {
            guard welcomed else { throw .protocolError("welcome must come first") }
            return .nothing
        }
        if case .welcome(let bridge, let voice) = message {
            guard !welcomed else { throw .protocolError("welcome repeated") }
            welcomed = true
            return .welcome(bridge: bridge, voice: voice)
        }
        guard welcomed else { throw .protocolError("welcome must come first") }
        switch message {
        case .replyBegin(let reply): openReply = reply
        case .replyEnd(let reply, _) where openReply == reply: openReply = nil
        case .stopAudio: openReply = nil
        default: break
        }
        return .message(message)
    }

    public mutating func receiveBinary(_ bytes: [UInt8]) throws(SessionError) -> Input {
        guard welcomed else { throw .protocolError("welcome must come first") }
        do {
            let frame = try AudioFrame(parsing: bytes, expected: .reply, channels: 1)
            return frame.stream == openReply ? .audio(frame) : .nothing
        } catch {
            throw .badAudio(error.reason)
        }
    }

    public mutating func encode(_ message: DeviceMessage) throws(SessionError) -> [UInt8] {
        let isHello = if case .hello = message { true } else { false }
        guard isHello == (sent.last == 0) else { throw .protocolError("hello comes first, once") }
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

public enum DeviceEvent: Sendable {
    case welcome(bridge: String, voice: String)
    case message(BridgeMessage)
    case audio(AudioFrame)
    case closed(reason: String)
}

/// A simulated board for tests and for trying the bridge without hardware: it pairs
/// with its own TLS identity, pins the bridge's certificate, and speaks loki/1 like
/// the firmware.
public final class SimulatedDevice: @unchecked Sendable {
    public let events: AsyncStream<DeviceEvent>
    private let continuation: AsyncStream<DeviceEvent>.Continuation
    private let queue = DispatchQueue(label: "loki.link.device")
    private let connection: NWConnection
    private var session = DeviceSession()
    private var closed = false

    public init(identity: TLSIdentity, bridgeFingerprint: String, host: String, port: UInt16) {
        (events, continuation) = AsyncStream.makeStream(of: DeviceEvent.self)
        let parameters = LinkTLS.parameters(identity: identity, server: false, queue: queue) { $0 == bridgeFingerprint }
        connection = NWConnection(to: .url(URL(string: "wss://\(host):\(port)\(Loki.path)")!), using: parameters)
    }

    /// Connects and sends hello (nil sends nothing, for tests of the bridge's hello
    /// deadline); the bridge's welcome arrives on `events`.
    public func connect(_ hello: DeviceMessage?) async throws {
        try await withCheckedThrowingContinuation { (done: CheckedContinuation<Void, Error>) in
            let once = Once()
            connection.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                switch state {
                case .ready:
                    once.run {
                        do {
                            if let hello { try self.sendOnQueue(hello) }
                            self.receive()
                            done.resume()
                        } catch {
                            done.resume(throwing: error)
                        }
                    }
                case .failed(let error), .waiting(let error):
                    once.run { done.resume(throwing: error) }
                    self.finish("\(error)")
                    self.connection.cancel()
                case .cancelled:
                    once.run { done.resume(throwing: SessionError(code: 1001, reason: "cancelled")) }
                    self.finish("cancelled")
                default: break
                }
            }
            connection.start(queue: queue)
        }
    }

    public func send(_ message: DeviceMessage) throws {
        try queue.sync { try sendOnQueue(message) }
    }

    /// Sends raw text, for tests that break the protocol on purpose.
    public func sendRaw(text: String) {
        queue.sync {
            connection.send(content: Data(text.utf8), contentContext: LinkTLS.context(.text), isComplete: true,
                            completion: .contentProcessed { _ in })
        }
    }

    public func sendAudio(_ frame: AudioFrame) {
        queue.sync {
            connection.send(content: Data(frame.bytes), contentContext: LinkTLS.context(.binary), isComplete: true,
                            completion: .contentProcessed { _ in })
        }
    }

    public func close() {
        queue.sync { connection.cancel() }
    }

    /// A connection must be cancelled to release its socket, even when nobody closed it.
    deinit { connection.cancel() }

    private func sendOnQueue(_ message: DeviceMessage) throws {
        let bytes = try session.encode(message)
        connection.send(content: Data(bytes), contentContext: LinkTLS.context(.text), isComplete: true,
                        completion: .contentProcessed { _ in })
    }

    private func receive() {
        connection.receiveMessage { [weak self] content, context, _, error in
            guard let self, !self.closed else { return }
            if let error {
                self.finish("\(error)")
                return
            }
            let bytes = content.map { [UInt8]($0) } ?? []
            do {
                let input: DeviceSession.Input
                switch LinkTLS.opcode(context) {
                case .text: input = try self.session.receiveText(bytes)
                case .binary: input = try self.session.receiveBinary(bytes)
                case .close:
                    self.finish("closed by the bridge")
                    return
                default: input = .nothing
                }
                switch input {
                case .welcome(let bridge, let voice): self.continuation.yield(.welcome(bridge: bridge, voice: voice))
                case .message(let message): self.continuation.yield(.message(message))
                case .audio(let frame): self.continuation.yield(.audio(frame))
                case .nothing: break
                }
            } catch {
                self.finish("\(error)")
                self.connection.cancel()
                return
            }
            self.receive()
        }
    }

    private func finish(_ reason: String) {
        guard !closed else { return }
        closed = true
        continuation.yield(.closed(reason: reason))
        continuation.finish()
    }
}
