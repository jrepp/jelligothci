import Foundation
import Network
import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

/// A board under the test's full control: it keeps no session rules, answers pings
/// only when asked to, sends frames of any size, and records what the bridge sent,
/// close codes included.
final class RawBoard: @unchecked Sendable {
    enum Received: Sendable, Equatable {
        case text([UInt8])
        case binary([UInt8])
        case ping
        /// A close frame and its code.
        case close(UInt16?)
        /// The connection ended without (or after) a close frame.
        case ended(String)
    }

    let log = EventLog<Received>()
    private let queue = DispatchQueue(label: "loki.test.raw-board")
    private let connection: NWConnection

    /// `subprotocols` are what the board offers; loki/1 boards offer "loki.1".
    init(_ identity: TLSIdentity = Fixture.board, pin: String = Fixture.bridge.fingerprint, port: UInt16,
         answerPings: Bool = true, subprotocols: [String] = [LinkTLS.subprotocol]) {
        let parameters = LinkTLS.parameters(identity: identity, server: false, queue: queue) { $0 == pin }
        let websocket = NWProtocolWebSocket.Options()
        websocket.autoReplyPing = answerPings
        websocket.maximumMessageSize = 1 << 20  // so the test can send more than the bridge accepts
        if !subprotocols.isEmpty { websocket.setSubprotocols(subprotocols) }
        parameters.defaultProtocolStack.applicationProtocols[0] = websocket  // [WebSocket, TLS]
        connection = NWConnection(to: .url(URL(string: "wss://127.0.0.1:\(port)\(Loki.path)")!), using: parameters)
    }

    /// Resolves once the WebSocket is open, or throws when the handshake fails.
    func connect() async throws {
        try await withCheckedThrowingContinuation { (done: CheckedContinuation<Void, Error>) in
            let once = Once()
            connection.stateUpdateHandler = { [self] state in
                switch state {
                case .ready:
                    once.run { done.resume() }
                    receive()
                case .failed(let error), .waiting(let error):
                    once.run { done.resume(throwing: error) }
                    record(.ended("\(error)"))
                    connection.cancel()
                case .cancelled:
                    once.run { done.resume(throwing: SessionError(code: 1001, reason: "cancelled")) }
                    record(.ended("cancelled"))
                default: break
                }
            }
            connection.start(queue: queue)
        }
    }

    func send(text bytes: [UInt8]) { send(bytes, .text) }
    func send(binary bytes: [UInt8]) { send(bytes, .binary) }
    func send(_ message: DeviceMessage, seq: UInt32) { send(text: try! LokiCodec.encode(message, seq: seq)) }

    func close() { queue.sync { connection.cancel() } }

    /// Whether the connection has ended, seen from this side.
    func hasEnded(timeout: Duration = .seconds(5)) async -> Bool {
        await log.wait(timeout: timeout) { if case .ended = $0 { true } else { false } } != nil
    }

    func closeCode(timeout: Duration = .seconds(5)) async -> UInt16?? {
        guard case .close(let code)? = await log.wait(timeout: timeout, { if case .close = $0 { true } else { false } })
        else { return nil }
        return .some(code)
    }

    private func send(_ bytes: [UInt8], _ opcode: NWProtocolWebSocket.Opcode) {
        queue.sync {
            connection.send(content: Data(bytes), contentContext: LinkTLS.context(opcode), isComplete: true,
                            completion: .contentProcessed { _ in })
        }
    }

    private func record(_ event: Received) { Task { await log.append(event) } }

    private func receive() {
        connection.receiveMessage { [self] content, context, _, error in
            if let error {
                record(.ended("\(error)"))
                connection.cancel()
                return
            }
            let bytes = content.map { [UInt8]($0) } ?? []
            let metadata = context?.protocolMetadata(definition: NWProtocolWebSocket.definition)
                as? NWProtocolWebSocket.Metadata
            switch metadata?.opcode {
            case .text: record(.text(bytes))
            case .binary: record(.binary(bytes))
            case .ping: record(.ping)
            case .close:
                let code: UInt16? = switch metadata?.closeCode {
                case .protocolCode(let defined): defined.rawValue
                case .applicationCode(let value), .privateCode(let value): value
                case nil: nil
                @unknown default: nil
                }
                record(.close(code))
                connection.cancel()
                return
            default: break
            }
            receive()
        }
    }
}

/// This process's TCP sockets on `port` (lsof lines), listening or connected: an
/// exact leak oracle for one listener, untouched by tests running alongside.
func tcpSockets(port: UInt16) -> [String] {
    let lsof = Process()
    lsof.executableURL = URL(fileURLWithPath: "/usr/sbin/lsof")
    lsof.arguments = ["-nP", "-a", "-p", "\(getpid())", "-iTCP:\(port)"]
    let output = Pipe()
    lsof.standardOutput = output
    lsof.standardError = FileHandle.nullDevice
    guard (try? lsof.run()) != nil else { return ["lsof did not run"] }
    let text = String(decoding: output.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
    lsof.waitUntilExit()
    return text.split(separator: "\n").dropFirst().map(String.init)  // the first line is the header
}

/// Connected sockets only: what is left when every connection has been released.
func connectedSockets(port: UInt16) -> [String] { tcpSockets(port: port).filter { !$0.contains("(LISTEN)") } }

/// Lines the link server reports through `diagnostics`, kept for assertions.
final class Notes: @unchecked Sendable {
    private let lock = NSLock()
    private var lines: [String] = []
    func add(_ line: String) { lock.withLock { lines.append(line) } }
    var all: [String] { lock.withLock { lines } }
}
