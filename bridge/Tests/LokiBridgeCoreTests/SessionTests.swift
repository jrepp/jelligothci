import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

private func text(_ message: DeviceMessage, _ seq: UInt32) -> [UInt8] {
    try! LokiCodec.encode(message, seq: seq)
}

private let hello = DeviceMessage.hello(fw: "0.10.0", board: "1.75c", channels: 1, frameMs: 60)

private func greeted() -> BridgeSession {
    var session = BridgeSession()
    _ = try! session.receiveText(text(hello, 1), at: 0)
    return session
}

private func mic(_ stream: UInt32, _ seq: UInt32 = 0) -> [UInt8] {
    AudioFrame(kind: .mic, final: false, stream: stream, seq: seq, payload: [0, 0, 1, 0]).bytes
}

@Test func helloComesFirst() throws {
    var session = BridgeSession()
    #expect(throws: SessionError.self) { try session.receiveText(text(.pttStart(stream: 1), 1), at: 0) }
    var ignoredFirst = BridgeSession()
    #expect(throws: SessionError.self) { try ignoredFirst.receiveText(Array(#"{"t":"selfie","seq":1}"#.utf8), at: 0) }
    var audioFirst = BridgeSession()
    #expect(throws: SessionError.self) { try audioFirst.receiveBinary(mic(1), at: 0) }
    var fine = BridgeSession()
    let input = try fine.receiveText(text(hello, 1), at: 0)
    #expect(input == .hello(DeviceHello(fw: "0.10.0", board: "1.75c", channels: 1, frameMs: 60)))
    #expect(throws: SessionError.self) { try fine.receiveText(text(hello, 2), at: 0) }
}

@Test func sequenceNumbersRiseByOne() throws {
    var session = greeted()
    #expect(try session.receiveText(Array(#"{"t":"selfie","seq":2}"#.utf8), at: 0) == .nothing)
    #expect(try session.receiveText(text(.cancel, 3), at: 0) == .message(.cancel))
    #expect(throws: SessionError.self) { try session.receiveText(text(.cancel, 5), at: 0) }
    var repeated = greeted()
    #expect(throws: SessionError.self) { try repeated.receiveText(text(.cancel, 1), at: 0) }
}

@Test func oneMicrophoneStreamAtATime() throws {
    var session = greeted()
    #expect(try session.receiveBinary(mic(1), at: 0) == .nothing)  // not open yet: dropped
    _ = try session.receiveText(text(.pttStart(stream: 1), 2), at: 0)
    #expect(try session.receiveBinary(mic(1), at: 0) == .audio(AudioFrame(parsing: mic(1), expected: .mic, channels: 1)))
    #expect(try session.receiveBinary(mic(2), at: 0) == .nothing)
    #expect(throws: SessionError.self) { try session.receiveText(text(.pttStart(stream: 2), 3), at: 0) }

    var ending = greeted()
    _ = try ending.receiveText(text(.pttStart(stream: 4), 2), at: 0)
    #expect(throws: SessionError.self) { try ending.receiveText(text(.pttEnd(stream: 3, frames: 0, dropped: 0), 3), at: 0) }

    var rising = greeted()
    _ = try rising.receiveText(text(.pttStart(stream: 4), 2), at: 0)
    _ = try rising.receiveText(text(.pttEnd(stream: 4, frames: 1, dropped: 0), 3), at: 0)
    #expect(try rising.receiveBinary(mic(4), at: 0) == .nothing)  // late frame of a closed stream
    #expect(throws: SessionError.self) { try rising.receiveText(text(.pttStart(stream: 4), 4), at: 0) }
}

@Test func malformedAudioClosesWith1003() {
    var session = greeted()
    do {
        _ = try session.receiveBinary([0x4C, 0x4B, 2, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0], at: 0)
        Issue.record("a reply frame from the board was accepted")
    } catch {
        #expect(error.code == 1003)
    }
}

@Test func ratesAreLimited() throws {
    var session = greeted()  // hello used one message at t=0
    for seq in 2...50 { _ = try session.receiveText(text(.cancel, UInt32(seq)), at: 0.5) }
    #expect(throws: SessionError.self) { try session.receiveText(text(.cancel, 51), at: 0.9) }
    var later = greeted()
    for seq in 2...50 { _ = try later.receiveText(text(.cancel, UInt32(seq)), at: 0.5) }
    #expect(try later.receiveText(text(.cancel, 51), at: 1.0) == .message(.cancel))  // a new window
}

@Test func welcomeIsSentFirstAndOnce() throws {
    var fresh = BridgeSession()
    #expect(throws: SessionError.self) { try fresh.encode(.welcome(bridge: "Loki Bridge")) }
    var session = greeted()
    #expect(throws: SessionError.self) { try session.encode(.stopAudio) }
    let welcome = try session.encode(.welcome(bridge: "Loki Bridge", voice: "Scar"))
    #expect(try LokiCodec.decodeBridgeMessage(welcome).seq == 1)
    #expect(throws: SessionError.self) { try session.encode(.welcome(bridge: "again")) }
    // A refused message does not use up a sequence number.
    #expect(throws: SessionError.self) { try session.encode(.settings(volume: 101)) }
    #expect(try LokiCodec.decodeBridgeMessage(try session.encode(.stopAudio)).seq == 2)
}

@Test func deviceSessionMirrorsTheBoard() throws {
    var device = DeviceSession()
    #expect(throws: SessionError.self) { try device.encode(.cancel) }
    _ = try device.encode(hello)
    #expect(throws: SessionError.self) { try device.encode(hello) }
    let state = try LokiCodec.encode(.state(.idle), seq: 1)
    #expect(throws: SessionError.self) { try device.receiveText(state) }  // welcome first
    var welcomed = DeviceSession()
    #expect(try welcomed.receiveText(LokiCodec.encode(.welcome(bridge: "B", voice: "Scar"), seq: 1))
        == .welcome(bridge: "B", voice: "Scar"))
    let reply = AudioFrame(kind: .reply, final: false, stream: 7, seq: 0, payload: [1, 2])
    #expect(try welcomed.receiveBinary(reply.bytes) == .nothing)
    _ = try welcomed.receiveText(LokiCodec.encode(.replyBegin(reply: 7), seq: 2))
    #expect(try welcomed.receiveBinary(reply.bytes) == .audio(reply))
    _ = try welcomed.receiveText(LokiCodec.encode(.stopAudio, seq: 3))
    #expect(try welcomed.receiveBinary(reply.bytes) == .nothing)
}
