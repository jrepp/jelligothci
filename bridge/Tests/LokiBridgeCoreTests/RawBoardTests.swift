import Foundation
import Network
import Testing
@testable import LokiBridgeCore

// RawBoard is the oracle for every close-code test, so what it records from one receive
// is pinned here, deterministically, with the values Network.framework hands it.

private func context(_ opcode: NWProtocolWebSocket.Opcode, code: NWProtocolWebSocket.CloseCode? = nil)
    -> NWConnection.ContentContext {
    let metadata = NWProtocolWebSocket.Metadata(opcode: opcode)
    if let code { metadata.closeCode = code }
    return NWConnection.ContentContext(identifier: "test", metadata: [metadata])
}

private let brokenPipe = NWError.posix(.EPIPE)

@Test func aCloseFrameDeliveredWithTheErrorThatEndsTheConnectionIsStillAClose() {
    // Regression: the error was checked first, the close frame dropped, and closeCode() timed out.
    let (events, more) = RawBoard.interpret(nil, context(.close, code: .protocolCode(.protocolError)), complete: true,
                                            error: brokenPipe)
    #expect(events == [.close(1002), .ended("\(brokenPipe)")])
    #expect(!more)
}

@Test(arguments: [
    (NWProtocolWebSocket.CloseCode.protocolCode(.goingAway), UInt16(1001)),
    (.protocolCode(.messageTooBig), 1009),
    (.protocolCode(.policyViolation), 1008),
    (.applicationCode(3001), 3001),
    (.privateCode(4002), 4002),
])
func aCloseFrameRecordsItsCodeAndEndsReceiving(_ code: NWProtocolWebSocket.CloseCode, _ expected: UInt16) {
    let (events, more) = RawBoard.interpret(nil, context(.close, code: code), complete: true, error: nil)
    #expect(events == [.close(expected)])
    #expect(!more)
}

@Test func aWholeMessageDeliveredWithAnErrorIsRecordedBeforeTheEnd() {
    let bytes: [UInt8] = Array(#"{"t":"stop_audio","seq":2}"#.utf8)
    let text = RawBoard.interpret(Data(bytes), context(.text), complete: true, error: brokenPipe)
    #expect(text.events == [.text(bytes), .ended("\(brokenPipe)")] && !text.more)
    let binary = RawBoard.interpret(Data([1, 2, 3]), context(.binary), complete: true, error: brokenPipe)
    #expect(binary.events == [.binary([1, 2, 3]), .ended("\(brokenPipe)")] && !binary.more)
}

@Test func aPartialMessageDeliveredWithAnErrorIsOnlyTheEnd() {
    // A frame cut short is not a message, and a partial close frame is not a close.
    for opcode in [NWProtocolWebSocket.Opcode.text, .binary, .close] {
        let (events, more) = RawBoard.interpret(Data([0x7B]), context(opcode, code: .protocolCode(.protocolError)),
                                                complete: false, error: brokenPipe)
        #expect(events == [.ended("\(brokenPipe)")], "\(opcode)")
        #expect(!more, "\(opcode)")
    }
}

@Test func anErrorWithNoMessageIsTheEnd() {
    let reset = NWError.posix(.ECONNRESET)
    let (events, more) = RawBoard.interpret(nil, nil, complete: false, error: reset)
    #expect(events == [.ended("\(reset)")])
    #expect(!more)
}

@Test func messagesWithoutAnErrorAreRecordedAndReceivingGoesOn() {
    #expect(RawBoard.interpret(Data([0x61]), context(.text), complete: true, error: nil) == ([.text([0x61])], true))
    #expect(RawBoard.interpret(Data([9]), context(.binary), complete: true, error: nil) == ([.binary([9])], true))
    #expect(RawBoard.interpret(nil, context(.ping), complete: true, error: nil) == ([.ping], true))
    #expect(RawBoard.interpret(nil, context(.text), complete: true, error: nil) == ([.text([])], true))
    // A pong, or a receive with nothing in it, records nothing.
    #expect(RawBoard.interpret(nil, context(.pong), complete: true, error: nil) == ([], true))
    #expect(RawBoard.interpret(nil, nil, complete: false, error: nil) == ([], true))
}

/// Every opcode, completeness and error together: at most one end and always last, a
/// close only from a whole close frame, and receiving goes on only with no close and no error.
@Test func everyReceiveOutcomeKeepsTheLogsInvariants() {
    let opcodes: [NWProtocolWebSocket.Opcode?] = [nil, .cont, .text, .binary, .close, .ping, .pong]
    let errors: [NWError?] = [nil, brokenPipe, .posix(.ECONNRESET), .posix(.ECANCELED)]
    for opcode in opcodes {
        for complete in [true, false] {
            for error in errors {
                let label = "\(String(describing: opcode)) complete \(complete) error \(String(describing: error))"
                let ctx = opcode.map { context($0, code: .protocolCode(.normalClosure)) }
                let (events, more) = RawBoard.interpret(Data([1]), ctx, complete: complete, error: error)
                let ends = events.indices.filter { if case .ended = events[$0] { true } else { false } }
                #expect(ends == (error == nil ? [] : [events.count - 1]), "\(label)")
                let closes = events.filter { if case .close = $0 { true } else { false } }
                #expect(closes == (opcode == .close && (error == nil || complete) ? [.close(1000)] : []), "\(label)")
                #expect(more == (error == nil && opcode != .close), "\(label)")
                // Deterministic: the same receive reports the same thing.
                #expect(RawBoard.interpret(Data([1]), ctx, complete: complete, error: error) == (events, more), "\(label)")
            }
        }
    }
}

@Test func theLogKeepsTheOrderEventsArrivedIn() async {
    // Regression: one Task per event let a close land after the end it came with, or a
    // welcome after a close, so "nothing before the close" checks could pass falsely.
    let board = RawBoard(port: 9)  // never connected
    let count = 2000
    let arriving = DispatchQueue(label: "loki.test.arrivals")
    arriving.sync { for index in 0..<count { board.record(.ended("\(index)")) } }
    #expect(await waitUntil { await board.log.events.count == count })
    #expect(await board.log.events == (0..<count).map { .ended("\($0)") })
}
