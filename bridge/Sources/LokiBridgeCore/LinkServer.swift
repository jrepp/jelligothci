import Foundation
import LokiProtocol
import Network

/// A paired board on a live connection.
public struct ConnectedDevice: Equatable, Sendable {
    public let paired: PairedDevice
    public let hello: DeviceHello
}

public enum LinkEvent: Sendable {
    case connected(ConnectedDevice)
    case message(DeviceMessage)
    case audio(AudioFrame)
    case disconnected(reason: String)
}

/// The bridge's loki/1 listener (RFC-003). Only boards in the pairing store complete
/// the TLS handshake; one board is connected at a time, and a newer connection from a
/// paired board replaces the old one (a board that rejoined Wi-Fi). A board that is
/// unpaired while connected is dropped. Connections still in their handshake are few
/// and short-lived, so strangers on the network cannot use them up. Events arrive on
/// `events`; `send` goes to the connected board. All state lives on one queue.
public final class LinkServer: @unchecked Sendable {
    public let events: AsyncStream<LinkEvent>
    private let continuation: AsyncStream<LinkEvent>.Continuation
    private let queue = DispatchQueue(label: "loki.link.server")
    private let identity: TLSIdentity
    private let pairing: PairingStore
    private let port: UInt16
    private let welcome: @Sendable () -> BridgeMessage
    private var listener: NWListener?
    /// Connections between accept and ready, held so their handlers stay alive.
    private var pending: [ObjectIdentifier: Connection] = [:]
    private var current: Connection?
    /// Seconds a new connection has to finish its handshake, and then to send hello;
    /// seconds between keepalive pings.
    var handshakeTimeout: Double = 10
    var helloTimeout: Double = 5
    var pingInterval: Double = 15
    /// Handshakes in progress at once, in all and from one address; beyond either, a
    /// new one ends the oldest from the busiest address, so a stranger flooding from
    /// one address ends only their own handshakes. A stranger with many addresses can
    /// still crowd out the board's slower handshake while they keep it up: a denial of
    /// service on the LAN, never a way in.
    var maxPending = 8
    var maxPendingPerHost = 2
    /// Connection-level notes (refused certificates, failed handshakes) for logs.
    public nonisolated(unsafe) var diagnostics: (@Sendable (String) -> Void)?

    final class Connection: @unchecked Sendable {
        let nw: NWConnection
        let accepted = monotonicSeconds()
        /// The remote address, for the per-address limit.
        var host: String {
            if case .hostPort(let host, _) = nw.endpoint { "\(host)" } else { "\(nw.endpoint)" }
        }
        var session = BridgeSession()
        var device: PairedDevice?
        var timer: DispatchSourceTimer?
        var awaitingPong = false
        init(_ nw: NWConnection) { self.nw = nw }
    }

    /// `port` 0 picks any free port; `start` returns the one bound. `welcome` is
    /// answered to each board's hello, before the board is reported connected.
    public init(identity: TLSIdentity, pairing: PairingStore, port: UInt16 = Loki.defaultPort,
                welcome: @escaping @Sendable () -> BridgeMessage) {
        self.identity = identity
        self.pairing = pairing
        self.port = port
        self.welcome = welcome
        (events, continuation) = AsyncStream.makeStream(of: LinkEvent.self)
    }

    public func start() async throws -> UInt16 {
        let note = diagnostics
        let parameters = LinkTLS.parameters(identity: identity, server: true, queue: queue) { [pairing] fingerprint in
            let paired = pairing.device(fingerprint: fingerprint) != nil
            if !paired { note?("refused an unpaired certificate \(fingerprint.prefix(16))…") }
            return paired
        }
        let listener = try NWListener(using: parameters, on: NWEndpoint.Port(rawValue: port) ?? .any)
        listener.newConnectionHandler = { [weak self] connection in self?.accept(connection) }
        return try await withCheckedThrowingContinuation { (ready: CheckedContinuation<UInt16, Error>) in
            let once = Once()
            listener.stateUpdateHandler = { state in
                switch state {
                case .ready: once.run { ready.resume(returning: listener.port?.rawValue ?? 0) }
                case .failed(let error): once.run { ready.resume(throwing: error) }
                default: break
                }
            }
            queue.async { [self] in
                self.listener = listener
                listener.start(queue: queue)
            }
        }
    }

    public func stop() {
        queue.sync {
            listener?.cancel()
            listener = nil
            for connection in pending.values { connection.nw.cancel() }
            pending = [:]
            if let current { drop(current, reason: "the bridge stopped", code: 1001) }
        }
        continuation.finish()
    }

    /// Sends to the connected board; false when none is connected or the message breaks
    /// the protocol (which then closes the connection). A board that has not said hello
    /// is not connected yet: nothing is sent and its connection is left alone.
    @discardableResult
    public func send(_ message: BridgeMessage) -> Bool {
        queue.sync { () -> Bool in
            guard let connection = current, connection.device != nil, connection.session.hello != nil,
                  stillPaired(connection)
            else { return false }
            do throws(SessionError) {
                let bytes = try connection.session.encode(message)
                connection.nw.send(content: Data(bytes), contentContext: LinkTLS.context(.text), isComplete: true,
                                   completion: .contentProcessed { _ in })
                return true
            } catch {
                drop(connection, reason: error.reason, code: error.code)
                return false
            }
        }
    }

    @discardableResult
    public func sendAudio(_ frame: AudioFrame) -> Bool {
        queue.sync { () -> Bool in
            guard let connection = current, connection.session.hello != nil, frame.kind == .reply, stillPaired(connection)
            else { return false }
            connection.nw.send(content: Data(frame.bytes), contentContext: LinkTLS.context(.binary), isComplete: true,
                               completion: .contentProcessed { _ in })
            return true
        }
    }

    // MARK: Connections (on queue)

    private func accept(_ nw: NWConnection) {
        let connection = Connection(nw)
        let key = ObjectIdentifier(connection)
        makeRoom(for: connection.host)
        pending[key] = connection
        queue.asyncAfter(deadline: .now() + handshakeTimeout) { [weak self, weak connection] in
            guard let self, let connection, self.pending[key] === connection else { return }
            self.pending[key] = nil
            self.diagnostics?("handshake from \(connection.nw.endpoint) took too long")
            connection.nw.cancel()
        }
        nw.stateUpdateHandler = { [weak self, weak connection] state in
            guard let self, let connection else { return }
            switch state {
            case .ready:
                self.pending[key] = nil
                self.ready(connection)
            case .failed(let error), .waiting(let error):
                self.pending[key] = nil
                self.diagnostics?("connection from \(connection.nw.endpoint) failed: \(error)")
                self.drop(connection, reason: "\(error)", code: nil)
            case .cancelled:
                self.pending[key] = nil
            default: break
            }
        }
        nw.start(queue: queue)
    }

    /// Ends a handshake when a new one from `host` would pass a limit: that address's
    /// oldest when it is at its own limit, else the oldest of the busiest addresses.
    private func makeRoom(for host: String) {
        let byHost = Dictionary(grouping: pending.values, by: \.host)
        let candidates: [Connection]
        if (byHost[host]?.count ?? 0) >= maxPendingPerHost {
            candidates = byHost[host] ?? []
        } else if pending.count >= maxPending {
            let most = byHost.values.map(\.count).max() ?? 0
            candidates = byHost.values.filter { $0.count == most }.flatMap { $0 }
        } else {
            return
        }
        guard let oldest = candidates.min(by: { $0.accepted < $1.accepted }) else { return }
        pending[ObjectIdentifier(oldest)] = nil
        diagnostics?("too many handshakes; ending the one from \(oldest.nw.endpoint)")
        oldest.nw.cancel()
    }

    private func ready(_ connection: Connection) {
        guard let fingerprint = LinkTLS.peerFingerprint(connection.nw),
              let device = pairing.device(fingerprint: fingerprint)
        else {
            diagnostics?("connection from \(connection.nw.endpoint) has no paired certificate")
            connection.nw.cancel()
            return
        }
        diagnostics?("\(device.name) connected from \(connection.nw.endpoint)")
        if let old = current { drop(old, reason: "replaced by a new connection", code: 1001) }
        connection.device = device
        current = connection
        schedule(connection, after: helloTimeout)
        receive(connection)
    }

    /// Whether the connection's board is still paired; drops it when not.
    private func stillPaired(_ connection: Connection) -> Bool {
        guard let device = connection.device, pairing.device(fingerprint: device.fingerprint) == nil else { return true }
        drop(connection, reason: "\(device.name) was unpaired", code: 1008)
        return false
    }

    /// One timer per connection: the hello deadline first, then keepalive pings, each
    /// also checking that the board is still paired.
    private func schedule(_ connection: Connection, after seconds: Double) {
        connection.timer?.cancel()
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + seconds, repeating: pingInterval)
        timer.setEventHandler { [weak self, weak connection] in
            guard let self, let connection, self.stillPaired(connection) else { return }
            guard connection.session.hello != nil else {
                self.drop(connection, reason: "no hello in time", code: 1002)
                return
            }
            guard !connection.awaitingPong else {
                self.drop(connection, reason: "no answer to a ping", code: 1001)
                return
            }
            connection.awaitingPong = true
            let metadata = NWProtocolWebSocket.Metadata(opcode: .ping)
            metadata.setPongHandler(self.queue) { [weak connection] error in
                if error == nil { connection?.awaitingPong = false }
            }
            let context = NWConnection.ContentContext(identifier: "ping", metadata: [metadata])
            connection.nw.send(content: nil, contentContext: context, isComplete: true, completion: .contentProcessed { _ in })
        }
        connection.timer = timer
        timer.resume()
    }

    private func receive(_ connection: Connection) {
        connection.nw.receiveMessage { [weak self, weak connection] content, context, _, error in
            guard let self, let connection, self.current === connection else { return }
            if let error {
                self.drop(connection, reason: "\(error)", code: nil)
                return
            }
            let bytes = content.map { [UInt8]($0) } ?? []
            do {
                switch LinkTLS.opcode(context) {
                case .text:
                    // Every control message (an answer, a press to talk) is from a board
                    // that is paired now, not just when it connected.
                    guard self.stillPaired(connection) else { return }
                    try self.handle(try connection.session.receiveText(bytes, at: monotonicSeconds()), connection)
                case .binary: try self.handle(try connection.session.receiveBinary(bytes, at: monotonicSeconds()), connection)
                case .close:
                    self.drop(connection, reason: "the board closed the connection", code: nil)
                    return
                default: break
                }
            } catch let error as SessionError {
                self.drop(connection, reason: error.reason, code: error.code)
                return
            } catch {
                self.drop(connection, reason: "\(error)", code: 1011)
                return
            }
            self.receive(connection)
        }
    }

    private func handle(_ input: BridgeSession.Input, _ connection: Connection) throws(SessionError) {
        switch input {
        case .hello(let hello):
            let bytes = try connection.session.encode(welcome())
            connection.nw.send(content: Data(bytes), contentContext: LinkTLS.context(.text), isComplete: true,
                               completion: .contentProcessed { _ in })
            continuation.yield(.connected(ConnectedDevice(paired: connection.device!, hello: hello)))
            schedule(connection, after: pingInterval)
        case .message(let message): continuation.yield(.message(message))
        case .audio(let frame): continuation.yield(.audio(frame))
        case .nothing: break
        }
    }

    /// Ends a connection, closing it with `code` when given. Reports the board's
    /// departure if it had said hello.
    private func drop(_ connection: Connection, reason: String, code: UInt16?) {
        if current === connection { diagnostics?("dropping \(connection.device?.name ?? "board"): \(reason)") }
        connection.timer?.cancel()
        connection.timer = nil
        if let code {
            connection.nw.send(content: nil, contentContext: LinkTLS.context(.close, closeCode: code), isComplete: true,
                               completion: .contentProcessed { [nw = connection.nw] _ in nw.cancel() })
        } else {
            connection.nw.cancel()
        }
        guard current === connection else { return }
        current = nil
        if connection.session.hello != nil { continuation.yield(.disconnected(reason: reason)) }
    }
}
