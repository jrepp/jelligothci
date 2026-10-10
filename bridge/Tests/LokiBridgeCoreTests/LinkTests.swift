import Foundation
import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

/// Collects a stream's events so a test can wait for one without owning the iterator.
actor EventLog<Event: Sendable> {
    private(set) var events: [Event] = []

    func append(_ event: Event) { events.append(event) }

    func wait(timeout: Duration = .seconds(5), _ match: (Event) -> Bool) async -> Event? {
        let deadline = ContinuousClock.now + timeout
        while ContinuousClock.now < deadline {
            if let event = events.first(where: match) { return event }
            try? await Task.sleep(for: .milliseconds(10))
        }
        return events.first(where: match)
    }
}

func collect<Event: Sendable>(_ stream: AsyncStream<Event>) -> EventLog<Event> {
    let log = EventLog<Event>()
    Task { for await event in stream { await log.append(event) } }
    return log
}

/// The bridge's and two boards' identities, made once for the whole suite.
enum Fixture {
    static let directory = scratchDirectory()
    static let bridge = try! TLSIdentity.loadOrCreate(at: directory.appendingPathComponent("bridge.p12"),
                                                      commonName: "Loki Bridge")
    static let board = try! TLSIdentity.loadOrCreate(at: directory.appendingPathComponent("board.p12"),
                                                     commonName: "Loki Agent")
    static let stranger = try! TLSIdentity.loadOrCreate(at: directory.appendingPathComponent("stranger.p12"),
                                                        commonName: "Stranger")
    static let hello = DeviceMessage.hello(fw: "0.10.0", board: "1.75c", channels: 1, frameMs: 60)

    /// A listener on a free loopback port that knows `paired`.
    static func server(pairing paired: [TLSIdentity] = [board]) async throws -> (LinkServer, UInt16) {
        let store = try PairingStore(file: scratchDirectory().appendingPathComponent("devices.json"))
        for identity in paired { try store.pair(PairedDevice(name: "Loki Agent", fingerprint: identity.fingerprint)) }
        let server = LinkServer(identity: bridge, pairing: store, port: 0) {
            .welcome(bridge: "Loki Bridge test", voice: "Scar")
        }
        return (server, try await server.start())
    }

    static func device(_ identity: TLSIdentity = board, pin: String = bridge.fingerprint, port: UInt16) -> SimulatedDevice {
        SimulatedDevice(identity: identity, bridgeFingerprint: pin, host: "127.0.0.1", port: port)
    }
}

private func isConnected(_ event: LinkEvent) -> Bool { if case .connected = event { true } else { false } }
private func isDisconnected(_ event: LinkEvent) -> Bool { if case .disconnected = event { true } else { false } }
private func isClosed(_ event: DeviceEvent) -> Bool { if case .closed = event { true } else { false } }

@Test func aPairedBoardTalksBothWays() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let bridgeEvents = collect(server.events)
    let device = Fixture.device(port: port)
    let deviceEvents = collect(device.events)
    try await device.connect(Fixture.hello)

    let welcome = await deviceEvents.wait { if case .welcome = $0 { true } else { false } }
    guard case .welcome(let bridge, let voice)? = welcome else { Issue.record("no welcome"); return }
    #expect(bridge == "Loki Bridge test" && voice == "Scar")
    let connected = await bridgeEvents.wait(isConnected)
    guard case .connected(let board)? = connected else { Issue.record("not connected"); return }
    #expect(board.hello == DeviceHello(fw: "0.10.0", board: "1.75c", channels: 1, frameMs: 60))
    #expect(board.paired.fingerprint == Fixture.board.fingerprint)

    // Board to bridge: a held-to-talk utterance.
    try device.send(.pttStart(stream: 1))
    for seq in 0..<3 {
        device.sendAudio(AudioFrame(kind: .mic, final: seq == 2, stream: 1, seq: UInt32(seq), payload: [1, 0, 2, 0]))
    }
    try device.send(.pttEnd(stream: 1, frames: 3, dropped: 0))
    #expect(await bridgeEvents.wait { if case .message(.pttEnd(1, 3, 0)) = $0 { true } else { false } } != nil)
    let frames = await bridgeEvents.events.compactMap { if case .audio(let f) = $0 { f } else { nil } }
    #expect(frames.map(\.seq) == [0, 1, 2] && frames.last?.final == true)

    // Bridge to board: state, then a reply.
    #expect(server.send(.state(.thinking, caption: "Running tests\u{2026}")))
    #expect(server.send(.replyBegin(reply: 9)))
    #expect(server.sendAudio(AudioFrame(kind: .reply, final: true, stream: 9, seq: 0, payload: [7, 7])))
    #expect(server.send(.replyEnd(reply: 9, frames: 1)))
    #expect(await deviceEvents.wait { if case .message(.replyEnd(9, 1)) = $0 { true } else { false } } != nil)
    let events = await deviceEvents.events
    #expect(events.contains { if case .message(.state(.thinking, "Running tests\u{2026}", -1, 0)) = $0 { true } else { false } })
    #expect(events.contains { if case .audio(let f) = $0 { f.stream == 9 && f.payload == [7, 7] } else { false } })
    // The board's answer and its sequence numbers keep flowing.
    try device.send(.played(reply: 9, complete: true))
    #expect(await bridgeEvents.wait { if case .message(.played(9, true)) = $0 { true } else { false } } != nil)
    device.close()
}

@Test func anUnpairedBoardNeverConnects() async throws {
    let (server, port) = try await Fixture.server(pairing: [Fixture.board])
    defer { server.stop() }
    let bridgeEvents = collect(server.events)
    let device = Fixture.device(Fixture.stranger, port: port)
    await #expect(throws: (any Error).self) { try await device.connect(Fixture.hello) }
    #expect(await bridgeEvents.wait(timeout: .milliseconds(500), isConnected) == nil)
    #expect(server.send(.stopAudio) == false)
}

@Test func aBoardRefusesABridgeItDidNotPin() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let bridgeEvents = collect(server.events)
    let device = Fixture.device(pin: Fixture.stranger.fingerprint, port: port)
    await #expect(throws: (any Error).self) { try await device.connect(Fixture.hello) }
    #expect(await bridgeEvents.wait(timeout: .milliseconds(500), isConnected) == nil)
}

@Test func aProtocolViolationEndsTheConnection() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let bridgeEvents = collect(server.events)
    let device = Fixture.device(port: port)
    let deviceEvents = collect(device.events)
    try await device.connect(Fixture.hello)
    #expect(await bridgeEvents.wait(isConnected) != nil)
    device.sendRaw(text: #"{"t":"cancel","seq":7}"#)  // the next is 2
    let gone = await bridgeEvents.wait(isDisconnected)
    guard case .disconnected(let reason)? = gone else { Issue.record("still connected"); return }
    #expect(reason.contains("sequence"))
    #expect(await deviceEvents.wait(isClosed) != nil)
    #expect(server.send(.stopAudio) == false)
}

@Test func aNewConnectionReplacesTheOldOne() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let bridgeEvents = collect(server.events)
    let first = Fixture.device(port: port)
    let firstEvents = collect(first.events)
    try await first.connect(Fixture.hello)
    #expect(await bridgeEvents.wait(isConnected) != nil)
    let second = Fixture.device(port: port)
    let secondEvents = collect(second.events)
    try await second.connect(Fixture.hello)
    #expect(await firstEvents.wait(isClosed) != nil)
    #expect(await secondEvents.wait { if case .welcome = $0 { true } else { false } } != nil)
    let connections = await bridgeEvents.events.filter(isConnected).count
    #expect(connections == 2)
    #expect(server.send(.stopAudio))
    #expect(await secondEvents.wait { if case .message(.stopAudio) = $0 { true } else { false } } != nil)
    second.close()
}

@Test func aBoardThatNeverSaysHelloIsDropped() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.helloTimeout = 0.3
    let device = Fixture.device(port: port)
    let deviceEvents = collect(device.events)
    try await device.connect(nil)
    #expect(await deviceEvents.wait(timeout: .seconds(3), isClosed) != nil)
}
