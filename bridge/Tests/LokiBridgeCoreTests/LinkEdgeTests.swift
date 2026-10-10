import Foundation
import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

private func isConnected(_ event: LinkEvent) -> Bool { if case .connected = event { true } else { false } }
private func isDisconnected(_ event: LinkEvent) -> Bool { if case .disconnected = event { true } else { false } }
private func isClosed(_ event: DeviceEvent) -> Bool { if case .closed = event { true } else { false } }
private func isText(_ event: RawBoard.Received) -> Bool { if case .text = event { true } else { false } }

/// Says hello and waits for the welcome, which must carry seq 1.
private func greet(_ board: RawBoard) async -> Bool {
    board.send(Fixture.hello, seq: 1)
    guard case .text(let bytes)? = await board.log.wait(isText) else { return false }
    guard case .message(1, .welcome)? = try? LokiCodec.decodeBridgeMessage(bytes) else { return false }
    return true
}

private func micFrame(stream: UInt32 = 1, payload: Int) -> [UInt8] {
    AudioFrame(kind: .mic, final: false, stream: stream, seq: 0,
               payload: (0..<payload).map { UInt8(truncatingIfNeeded: $0) }).bytes
}

/// A control message padded with an unknown field to exactly `size` bytes.
private func padded(_ type: String, seq: UInt32, to size: Int) -> [UInt8] {
    let bare = #"{"t":"\#(type)","seq":\#(seq),"pad":""}"#
    return Array((#"{"t":"\#(type)","seq":\#(seq),"pad":""# + String(repeating: "x", count: size - bare.utf8.count)
        + #""}"#).utf8)
}

@Test func messagesAtTheSizeLimitPassAndOneByteMoreCloses() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    // 512 bytes of control message and 12 + 4,800 of audio are the board's limits.
    let control = padded("cancel", seq: 2, to: 512)
    #expect(control.count == 512)
    board.send(text: control)
    board.send(.pttStart(stream: 1), seq: 3)
    let audio = micFrame(payload: 4800)
    #expect(audio.count == 4812)
    board.send(binary: audio)
    #expect(await events.wait { if case .audio(let frame) = $0 { frame.payload.count == 4800 } else { false } } != nil)
    #expect(await events.events.contains { if case .message(.cancel) = $0 { true } else { false } })
    #expect(await events.events.filter(isDisconnected).isEmpty)
    board.close()
}

@Test(arguments: [
    ("text one byte over", 513, false), ("text past the WebSocket limit", 9000, false),
    ("audio one byte over", 4813, true), ("audio past the WebSocket limit", 9000, true),
])
func oversizedMessagesCloseWith1009(_ name: String, _ size: Int, _ binary: Bool) async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    if binary {
        board.send(.pttStart(stream: 1), seq: 2)
        board.send(binary: micFrame(payload: size - 12))
    } else {
        board.send(text: padded("cancel", seq: 2, to: size))
    }
    #expect(await board.closeCode() == .some(1009), "\(name)")
    #expect(await events.wait(isDisconnected) != nil, "\(name)")
    #expect(await events.events.contains { if case .audio = $0 { true } else { false } } == false)
    #expect(server.send(.stopAudio) == false)
}

/// Each case runs on its own connection after hello, and must close with its code.
@Test func malformedFramesCloseWithTheirCodes() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let cases: [(String, [UInt8], Bool, UInt16)] = [
        ("not JSON", Array("hello".utf8), false, 1002),
        ("truncated object", Array(#"{"t":"cancel","seq":2"#.utf8), false, 1002),
        ("invalid UTF-8", [0x7B, 0x22, 0x74, 0x22, 0x3A, 0x22, 0xFF, 0x22, 0x7D], false, 1002),
        ("seq repeated", Array(#"{"t":"cancel","seq":1}"#.utf8), false, 1002),
        ("seq skipped", Array(#"{"t":"cancel","seq":3}"#.utf8), false, 1002),
        ("hello again", try LokiCodec.encode(Fixture.hello, seq: 2), false, 1002),
        ("ptt_end of no stream", try LokiCodec.encode(.pttEnd(stream: 1, frames: 0, dropped: 0), seq: 2), false, 1002),
        ("bad magic", [0x4A, 0x41, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], true, 1003),
        ("reply kind", [0x4C, 0x4B, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], true, 1003),
        ("reserved flag", [0x4C, 0x4B, 1, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], true, 1003),
        ("half a sample", [0x4C, 0x4B, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0], true, 1003),
        ("header only", [0x4C, 0x4B, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0], true, 1003),
        ("stream 0", [0x4C, 0x4B, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], true, 1003),
        ("empty binary", [], true, 1003),
    ]
    for (index, (name, bytes, binary, code)) in cases.enumerated() {
        let board = RawBoard(port: port)
        try await board.connect()
        #expect(await greet(board), "\(name)")
        if binary { board.send(binary: bytes) } else { board.send(text: bytes) }
        #expect(await board.closeCode() == .some(code), "\(name)")
        // One connected and one disconnected per case, in order.
        let settled = await waitUntil { await events.events.filter(isDisconnected).count == index + 1 }
        #expect(settled, "\(name)")
    }
    let all = await events.events
    #expect(all.filter(isConnected).count == cases.count && all.filter(isDisconnected).count == cases.count)
}

@Test func beforeHelloEverythingClosesWithoutAnyEvent() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let cases: [(String, [UInt8], Bool)] = [
        ("audio", micFrame(payload: 4), true),
        ("unknown type", Array(#"{"t":"selfie","seq":1}"#.utf8), false),
        ("ptt_start", try LokiCodec.encode(.pttStart(stream: 1), seq: 1), false),
        ("hello with seq 2", try LokiCodec.encode(Fixture.hello, seq: 2), false),
    ]
    for (name, bytes, binary) in cases {
        let board = RawBoard(port: port)
        try await board.connect()
        if binary { board.send(binary: bytes) } else { board.send(text: bytes) }
        #expect(await board.closeCode() == .some(1002), "\(name)")
    }
    try await Task.sleep(for: .milliseconds(100))
    #expect(await events.events.isEmpty)
}

@Test func aBoardThatSaysNothingAfterTLSIsClosedWith1002AndNeverReported() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.helloTimeout = 0.3
    let notes = Notes()
    server.diagnostics = { notes.add($0) }
    let events = collect(server.events)
    let silent = RawBoard(port: port)
    let start = ContinuousClock.now
    try await silent.connect()
    #expect(await silent.closeCode() == .some(1002))
    #expect(ContinuousClock.now - start >= .milliseconds(250))
    #expect(notes.all.contains { $0.contains("no hello in time") })
    #expect(await events.events.isEmpty)
    // The listener is unharmed: the next board is welcomed.
    let next = RawBoard(port: port)
    try await next.connect()
    #expect(await greet(next))
    #expect(await events.wait(isConnected) != nil)
    next.close()
}

@Test func sendingBeforeHelloLeavesTheBoardAlone() async throws {
    // Regression: send() used to close a board that had not said hello yet with 1002.
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let notes = Notes()
    server.diagnostics = { notes.add($0) }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await waitUntil { notes.all.contains { $0.contains("connected from") } })
    #expect(server.send(.stopAudio) == false)
    #expect(server.sendAudio(AudioFrame(kind: .reply, final: true, stream: 1, seq: 0, payload: [0, 0])) == false)
    // The welcome is still the first message, with seq 1, and the board is reported.
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    #expect(server.send(.stopAudio))
    let stop = await board.log.wait {
        if case .text(let bytes) = $0 { (try? LokiCodec.decodeBridgeMessage(bytes)) == .message(seq: 2, .stopAudio) }
        else { false }
    }
    #expect(stop != nil)
    #expect(await board.log.events.contains { if case .close = $0 { true } else { false } } == false)
    board.close()
}

@Test func sendWithNoBoardReturnsFalseAndReportsNothing() async throws {
    let (server, _) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    #expect(server.send(.stopAudio) == false)
    #expect(server.send(.welcome(bridge: "x")) == false)
    #expect(server.sendAudio(AudioFrame(kind: .reply, final: true, stream: 1, seq: 0, payload: [0, 0])) == false)
    try await Task.sleep(for: .milliseconds(50))
    #expect(await events.events.isEmpty)
}

@Test func sendAudioOnlyCarriesReplyFramesAndABrokenSendCloses() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    #expect(server.sendAudio(AudioFrame(kind: .mic, final: true, stream: 1, seq: 0, payload: [0, 0])) == false)
    #expect(server.sendAudio(AudioFrame(kind: .reply, final: true, stream: 1, seq: 0, payload: [5, 0])))
    #expect(await board.log.wait { $0 == .binary([0x4C, 0x4B, 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 5, 0]) } != nil)
    // A message that breaks the protocol is refused and ends the connection, as documented.
    #expect(server.send(.welcome(bridge: "again")) == false)
    #expect(await board.closeCode() == .some(1002))
    #expect(await events.wait(isDisconnected) != nil)
    #expect(server.send(.stopAudio) == false)
}

@Test func aBoardThatStopsAnsweringPingsIsDropped() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.pingInterval = 0.15
    let events = collect(server.events)
    let board = RawBoard(port: port, answerPings: false)
    try await board.connect()
    #expect(await greet(board))
    let connected = ContinuousClock.now
    #expect(await events.wait(isConnected) != nil)
    #expect(await board.log.wait { $0 == .ping } != nil)
    #expect(await board.closeCode() == .some(1001))
    guard case .disconnected(let reason)? = await events.wait(isDisconnected) else {
        Issue.record("still connected")
        return
    }
    #expect(reason == "no answer to a ping")
    // A ping goes out one interval after hello and the drop comes one interval later.
    #expect(ContinuousClock.now - connected >= .milliseconds(250))
}

@Test func aBoardThatAnswersPingsStaysConnected() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.pingInterval = 0.1
    let events = collect(server.events)
    let device = Fixture.device(port: port)
    let deviceEvents = collect(device.events)
    try await device.connect(Fixture.hello)
    #expect(await events.wait(isConnected) != nil)
    try await Task.sleep(for: .milliseconds(800))  // eight pings
    #expect(await events.events.filter(isDisconnected).isEmpty)
    #expect(server.send(.state(.idle)))
    #expect(await deviceEvents.wait { if case .message(.state(.idle, _, _, _)) = $0 { true } else { false } } != nil)
    device.close()
}

@Test func stopEndsAnActiveSessionAndTheEventStream() async throws {
    let (server, port) = try await Fixture.server()
    let finished = Flag()
    let events = EventLog<LinkEvent>()
    Task {
        for await event in server.events { await events.append(event) }
        finished.set()
    }
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    let waiting = RawBoard(port: port)  // a second board, replaced the old one but silent
    try await waiting.connect()
    server.stop()
    #expect(await waiting.hasEnded())
    #expect(await waitUntil { finished.isSet })
    #expect(server.send(.stopAudio) == false)
    #expect(server.sendAudio(AudioFrame(kind: .reply, final: true, stream: 1, seq: 0, payload: [0, 0])) == false)
    server.stop()  // twice is harmless
    // The listener and every connection are released.
    #expect(await waitUntil { tcpSockets(port: port).isEmpty }, "\(tcpSockets(port: port))")
    // The first board was replaced, then the bridge went away: one disconnection each.
    let all = await events.events
    #expect(all.filter(isConnected).count == 1 && all.filter(isDisconnected).count == 1)
    // Nothing listens any more.
    let late = RawBoard(port: port)
    await #expect(throws: (any Error).self) { try await late.connect() }
}

@Test func stopDuringASessionTellsTheBoardWhy() async throws {
    let (server, port) = try await Fixture.server()
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    server.stop()
    #expect(await board.closeCode() == .some(1001))
    guard case .disconnected(let reason)? = await events.wait(isDisconnected) else {
        Issue.record("no disconnection")
        return
    }
    #expect(reason == "the bridge stopped")
}

@Test func anUnpairedCertificateNeverYieldsAnyEvent() async throws {
    let store = try PairingStore(file: scratchDirectory().appendingPathComponent("devices.json"))
    try store.pair(PairedDevice(name: "Loki Agent", fingerprint: Fixture.board.fingerprint))
    let server = LinkServer(identity: Fixture.bridge, pairing: store, port: 0) { .welcome(bridge: "B") }
    let notes = Notes()
    server.diagnostics = { notes.add($0) }
    let port = try await server.start()
    defer { server.stop() }
    let events = collect(server.events)
    for _ in 0..<5 {
        let stranger = RawBoard(Fixture.stranger, port: port)
        await #expect(throws: (any Error).self) { try await stranger.connect() }
    }
    // Without the loki.1 subprotocol even a paired board is turned away.
    for offered in [["other.1"], []] {
        let other = RawBoard(port: port, subprotocols: offered)
        await #expect(throws: (any Error).self, "offering \(offered)") { try await other.connect() }
    }
    #expect(notes.all.contains { $0.contains("refused an unpaired certificate \(Fixture.stranger.fingerprint.prefix(16))") })
    // A board unpaired after it paired is refused from then on.
    try store.unpair(fingerprint: Fixture.board.fingerprint)
    let former = RawBoard(port: port)
    await #expect(throws: (any Error).self) { try await former.connect() }
    try await Task.sleep(for: .milliseconds(100))
    #expect(await events.events.isEmpty)
}

@Test func manyConnectionsComeAndGoWithoutLeaksOrStuckState() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.helloTimeout = 0.2
    let events = collect(server.events)
    // Warm up once, so lazily created resources do not count as a leak.
    let warm = Fixture.device(port: port)
    try await warm.connect(Fixture.hello)
    #expect(await events.wait(isConnected) != nil)
    warm.close()
    #expect(await events.wait(isDisconnected) != nil)
    var greeted = 1
    for cycle in 0..<40 {
        switch cycle % 4 {
        case 0, 1:  // hello, a message, then the board leaves or breaks the protocol
            let device = Fixture.device(port: port)
            try await device.connect(Fixture.hello)
            greeted += 1
            #expect(await waitUntil { await events.events.filter(isConnected).count == greeted }, "cycle \(cycle)")
            if cycle % 4 == 0 { device.close() } else { device.sendRaw(text: #"{"t":"cancel","seq":9}"#) }
        case 2:  // silent until the hello deadline
            let device = Fixture.device(port: port)
            try await device.connect(nil)
            device.close()
        default:  // refused in the handshake
            let stranger = Fixture.device(Fixture.stranger, port: port)
            await #expect(throws: (any Error).self) { try await stranger.connect(Fixture.hello) }
        }
        #expect(await waitUntil { await events.events.filter(isDisconnected).count == greeted }, "cycle \(cycle)")
    }
    // Every hello was reported once and every departure once, and the bridge still works.
    let last = Fixture.device(port: port)
    let lastEvents = collect(last.events)
    try await last.connect(Fixture.hello)
    #expect(await waitUntil { await events.events.filter(isConnected).count == greeted + 1 })
    #expect(server.send(.stopAudio))
    #expect(await lastEvents.wait { if case .message(.stopAudio) = $0 { true } else { false } } != nil)
    last.close()
    #expect(await waitUntil { await events.events.filter(isDisconnected).count == greeted + 1 })
    // Every connection was released: only the listening socket is left.
    #expect(await waitUntil { connectedSockets(port: port).isEmpty }, "\(connectedSockets(port: port))")
    #expect(tcpSockets(port: port).count == 1)
}

@Test func boardsConnectingAtOnceLeaveExactlyOneConnected() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let devices = (0..<8).map { _ in Fixture.device(port: port) }
    let logs = devices.map { collect($0.events) }
    await withTaskGroup(of: Void.self) { group in
        for device in devices { group.addTask { try? await device.connect(Fixture.hello) } }
    }
    // Wait until all but one are closed, then check the one left is the bridge's board.
    var open: [Int] = []
    let settled = await waitUntil(timeout: .seconds(5)) {
        open = []
        for (index, log) in logs.enumerated() where await log.events.contains(where: isClosed) == false {
            open.append(index)
        }
        let all = await events.events
        return open.count == 1 && all.filter(isConnected).count - all.filter(isDisconnected).count == 1
    }
    let reported = await events.events.count
    #expect(settled, "open boards \(open), events \(reported)")
    guard settled, let survivor = open.first else { return }
    #expect(server.send(.stopAudio))
    #expect(await logs[survivor].wait { if case .message(.stopAudio) = $0 { true } else { false } } != nil)
    // The replaced connections are gone on the bridge's side too: one pair is left.
    #expect(await waitUntil { connectedSockets(port: port).count == 2 }, "\(connectedSockets(port: port))")
    for device in devices { device.close() }
}

@Test func aBoardThatLeavesIsReportedOnce() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    board.close()
    #expect(await events.wait(isDisconnected) != nil)
    try await Task.sleep(for: .milliseconds(100))
    #expect(await events.events.filter(isDisconnected).count == 1)
    #expect(server.send(.stopAudio) == false)
}

/// Polls a condition until it holds or the time runs out.
func waitUntil(timeout: Duration = .seconds(5), _ condition: () async -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + timeout
    while ContinuousClock.now < deadline {
        if await condition() { return true }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return await condition()
}

final class Flag: @unchecked Sendable {
    private let lock = NSLock()
    private var value = false
    func set() { lock.withLock { value = true } }
    var isSet: Bool { lock.withLock { value } }
}
