import Foundation
import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

/// Seeded xorshift, so a failing trace names a seed that replays it.
struct Xorshift {
    var state: UInt64
    init(seed: UInt64) { state = seed &* 0x9E37_79B9_7F4A_7C15 | 1 }
    mutating func next() -> UInt64 {
        state ^= state << 13
        state ^= state >> 7
        state ^= state << 17
        return state
    }
    mutating func below(_ bound: Int) -> Int { Int(next() % UInt64(bound)) }
    mutating func chance(_ percent: Int) -> Bool { below(100) < percent }
    mutating func pick<T>(_ list: [T]) -> T { list[below(list.count)] }
}

/// What a session step should do: give an input, or close with a WebSocket code.
private enum Outcome<Input: Equatable>: Equatable {
    case input(Input)
    case close(UInt16)
}

/// The rate rule as the protocol states it: a window opens with the first message at
/// least a second after the last window opened, and admits `limit` messages.
private struct Window {
    let limit: Int
    var opened: Double?
    var count = 0
    mutating func admit(_ now: Double) -> Bool {
        if opened.map({ now - $0 >= 1 }) ?? true {
            opened = now
            count = 0
        }
        count += 1
        return count <= limit
    }
}

private func ascii(_ text: String) -> [UInt8] { Array(text.utf8) }

/// A message on the wire with any `seq`, 0 included, which the encoder will not write.
private func wire(_ encode: (UInt32) throws -> [UInt8], seq: UInt32) -> [UInt8] {
    let bytes = try! encode(max(seq, 1))
    guard seq == 0 else { return bytes }
    let text = String(decoding: bytes, as: UTF8.self)
    return ascii(text.replacingOccurrences(of: #""seq":1"#, with: #""seq":0"#))
}

// MARK: BridgeSession

/// An independent model of the bridge's side of a connection (docs/loki-protocol.md).
private struct BridgeModel {
    var hello: DeviceHello?
    var openStream: UInt32?
    var lastStream: UInt32 = 0
    var received: UInt32 = 0
    var sent: UInt32 = 0
    var control = Window(limit: 50)
    var audio = Window(limit: 60)
}

/// One generated board-to-bridge text message and how it decodes.
private enum TextKind { case message(DeviceMessage), unknown, malformed, oversized }

private struct BridgeTrace {
    var random: Xorshift
    var model = BridgeModel()
    var session = BridgeSession()
    var now = 0.0
    var log: [String] = []
    /// Counts of what happened, so the generator is shown to reach every rule.
    var seen: [String: Int] = [:]
    /// Sends nothing broken, so only the rate limits end the trace.
    var clean = false

    init(seed: UInt64) { random = Xorshift(seed: seed) }

    /// Mostly what a sound board would send next; now and then anything at all.
    mutating func textKind() -> TextKind {
        let benign: [TextKind] = [.message(.cancel), .message(.played(reply: 9, complete: true)),
                                  .message(.status(battery: 80, charging: false, rssi: -50)), .unknown]
        let hello = TextKind.message(.hello(fw: "1.0", board: "1.75c", channels: UInt32(1 + random.below(4)), frameMs: 60))
        guard random.chance(clean ? 0 : 5) else {
            if model.hello == nil { return random.chance(70) ? hello : random.pick(benign) }
            if let open = model.openStream {
                return random.chance(30) ? .message(.pttEnd(stream: open, frames: 3, dropped: 0)) : random.pick(benign)
            }
            return random.chance(40) ? .message(.pttStart(stream: model.lastStream &+ UInt32(1 + random.below(3))))
                : random.pick(benign)
        }
        let open = model.openStream ?? model.lastStream
        return random.pick([
            hello, .malformed, .oversized, .unknown,
            .message(.pttStart(stream: max(model.lastStream, 1))), .message(.pttStart(stream: model.lastStream &+ 1)),
            .message(.pttEnd(stream: max(open, 1), frames: 0, dropped: 0)),
            .message(.pttEnd(stream: max(open, 1) &+ 1, frames: 0, dropped: 0)),
        ])
    }

    mutating func seqFor() -> UInt32 {
        guard random.chance(clean ? 0 : 2) else { return model.received + 1 }
        return random.pick([model.received, model.received + 2, 0, UInt32(random.below(1000))])
    }

    func encodeText(_ kind: TextKind, seq: UInt32) -> [UInt8] {
        switch kind {
        case .message(let message): wire({ try LokiCodec.encode(message, seq: $0) }, seq: seq)
        case .unknown: ascii(#"{"t":"selfie","seq":\#(seq),"note":{"a":[1,2]}}"#)
        case .malformed: ascii(#"{"t":"cancel","seq":\#(seq)"#)
        case .oversized: ascii(#"{"t":"selfie","seq":\#(seq),"pad":""# + String(repeating: "x", count: 520) + #""}"#)
        }
    }

    /// The model's prediction for a text message, applied to the model.
    mutating func expectText(_ kind: TextKind, seq: UInt32) -> Outcome<BridgeSession.Input> {
        guard model.control.admit(now) else { return .close(1002) }
        switch kind {
        case .malformed: return .close(1002)
        case .oversized: return .close(1009)
        default: break
        }
        guard seq == model.received + 1 else { return .close(1002) }
        model.received += 1
        guard case .message(let message) = kind else { return model.hello == nil ? .close(1002) : .input(.nothing) }
        if case .hello(let fw, let board, let channels, let frameMs) = message {
            guard model.hello == nil else { return .close(1002) }
            let hello = DeviceHello(fw: fw, board: board, channels: Int(channels), frameMs: Int(frameMs))
            model.hello = hello
            return .input(.hello(hello))
        }
        guard model.hello != nil else { return .close(1002) }
        switch message {
        case .pttStart(let stream):
            guard model.openStream == nil, stream > model.lastStream else { return .close(1002) }
            model.openStream = stream
            model.lastStream = stream
        case .pttEnd(let stream, _, _):
            guard model.openStream == stream else { return .close(1002) }
            model.openStream = nil
        default: break
        }
        return .input(.message(message))
    }

    /// A board audio frame: mostly well formed, for the open stream or another, and
    /// now and then broken in one of the ways the protocol names.
    mutating func binary() -> (bytes: [UInt8], valid: AudioFrame?) {
        let channels = model.hello?.channels ?? 1
        let stream = random.pick([model.openStream ?? model.lastStream &+ 1, model.openStream ?? 1, model.lastStream,
                                  UInt32(1 + random.below(20))])
        let samples = 1 + random.below(40)
        let frame = AudioFrame(kind: .mic, final: random.chance(10), stream: max(stream, 1), seq: UInt32(random.below(9)),
                               payload: (0..<(2 * channels * samples)).map { UInt8(truncatingIfNeeded: $0) })
        guard random.chance(clean ? 0 : 4) else { return (frame.bytes, frame) }
        var broken = frame.bytes
        switch random.below(6) {
        case 0: broken.append(0)  // a partial sample
        case 1: broken[2] = AudioFrame.Kind.reply.rawValue
        case 2: broken[3] = 2  // a reserved flag bit
        case 3: broken = Array(broken.prefix(AudioFrame.headerSize))  // no samples
        case 4: broken.replaceSubrange(4..<8, with: [0, 0, 0, 0])  // stream 0
        default: broken = [0x4C, 0x4B, 1, 0] + [UInt8](repeating: 1, count: 8 + 4802)  // too long
        }
        return (broken, nil)
    }

    mutating func expectBinary(_ bytes: [UInt8], _ valid: AudioFrame?) -> Outcome<BridgeSession.Input> {
        guard model.hello != nil else { return .close(1002) }
        guard model.audio.admit(now) else { return .close(1003) }
        guard bytes.count <= 12 + 4800 else { return .close(1009) }  // too long, as for text
        guard let frame = valid else { return .close(1003) }
        return .input(frame.stream == model.openStream ? .audio(frame) : .nothing)
    }

    /// Bridge messages to encode: welcome, ordinary ones, and ones the codec refuses.
    mutating func outgoing() -> (message: BridgeMessage, valid: Bool) {
        random.pick([
            (.welcome(bridge: "Loki Bridge", voice: "Scar"), true),
            (.welcome(bridge: String(repeating: "w", count: 41)), false),
            (.stopAudio, true), (.state(.thinking, caption: "Working"), true),
            (.settings(volume: 101), false), (.replyBegin(reply: 0), false), (.replyEnd(reply: 4, frames: 2), true),
        ])
    }

    mutating func encodeStep() {
        let (message, valid) = outgoing()
        let isWelcome = if case .welcome = message { true } else { false }
        let refused = model.hello == nil || isWelcome != (model.sent == 0) || !valid
        log.append("encode \(message)")
        do {
            let bytes = try session.encode(message)
            #expect(!refused, "\(log)")
            model.sent += 1
            let decoded = try? LokiCodec.decodeBridgeMessage(bytes)
            #expect(decoded == .message(seq: model.sent, message), "\(log)")
            seen["encoded", default: 0] += 1
        } catch {
            #expect(refused, "\(log): \(error)")
            seen["refused", default: 0] += 1
        }
    }

    /// Runs one step; false once the connection would close. A bursty trace sends
    /// many messages in the same instant, to reach the rate limits.
    mutating func step(bursty: Bool) -> Bool {
        now += Double(bursty ? random.pick([0, 0, 0, 1]) : random.pick([0, 0, 1, 2, 16, 63, 64, 65, 128])) / 64
        let roll = random.below(100)
        if roll < 10 {
            encodeStep()
            return true
        }
        let expected: Outcome<BridgeSession.Input>
        var actual: Outcome<BridgeSession.Input>
        var reason = ""
        if roll < 55 {
            let kind = textKind()
            let seq = seqFor()
            let bytes = encodeText(kind, seq: seq)
            log.append("t=\(now) text \(String(decoding: bytes.prefix(80), as: UTF8.self))")
            expected = expectText(kind, seq: seq)
            do { actual = .input(try session.receiveText(bytes, at: now)) } catch {
                actual = .close(error.code)
                reason = error.reason
            }
        } else {
            let (bytes, valid) = binary()
            log.append("t=\(now) binary \(bytes.count) bytes \(valid?.canonical ?? "invalid")")
            expected = expectBinary(bytes, valid)
            do { actual = .input(try session.receiveBinary(bytes, at: now)) } catch {
                actual = .close(error.code)
                reason = error.reason
            }
        }
        #expect(actual == expected, "\(log) \(reason)")
        #expect(session.hello == model.hello && session.openStream == model.openStream, "\(log)")
        switch actual {
        case .close(let code):
            seen[reason.hasPrefix("too many") ? "rate \(code)" : "close \(code)", default: 0] += 1
            if reason == "audio frame too long" { seen["audio 1009", default: 0] += 1 }
        case .input(let input): seen[String("\(input)".prefix(while: { $0 != "(" })), default: 0] += 1
        }
        return actual == expected && !isClose(actual)
    }
}

private func isClose<T: Equatable>(_ outcome: Outcome<T>) -> Bool { if case .close = outcome { true } else { false } }

@Test func bridgeSessionMatchesItsModelOverRandomTraces() {
    var seen: [String: Int] = [:]
    var steps = 0
    for seed in UInt64(1)...1500 {
        var trace = BridgeTrace(seed: seed)
        trace.clean = seed % 3 == 0
        // Most traces begin with hello, so they reach the rules after it.
        if seed % 8 != 0 {
            let hello = DeviceMessage.hello(fw: "1.0", board: "1.75c", channels: UInt32(1 + seed % 4), frameMs: 60)
            _ = trace.expectText(.message(hello), seq: 1)
            _ = try? trace.session.receiveText(LokiCodec.encode(hello, seq: 1), at: 0)
        }
        var length = 0
        while length < 400, trace.step(bursty: seed % 3 == 0) { length += 1 }
        steps += length
        for (key, count) in trace.seen { seen[key, default: 0] += count }
    }
    // Every rule was reached, so a green run means each was checked.
    for key in ["close 1002", "close 1003", "close 1009", "audio 1009", "rate 1002", "rate 1003", "hello", "message", "audio",
                "nothing", "encoded", "refused"] {
        #expect(seen[key, default: 0] > 10, "the generator rarely reaches \(key): \(seen)")
    }
    #expect(steps > 20_000, "\(seen)")
}

@Test func rateLimitsHoldAtTheWindowBoundary() throws {
    let hello = DeviceMessage.hello(fw: "1.0", board: "1.75c", channels: 1, frameMs: 60)
    for start in [0.0, 0.25, 1e6 + 0.5] {
        // Control: 50 a window. hello opens the window at `start`.
        var session = BridgeSession()
        _ = try session.receiveText(LokiCodec.encode(hello, seq: 1), at: start)
        for seq in 2...50 { _ = try session.receiveText(LokiCodec.encode(.cancel, seq: UInt32(seq)), at: start + 0.999) }
        #expect(throws: SessionError(code: 1002, reason: "too many control messages")) {
            try session.receiveText(LokiCodec.encode(.cancel, seq: 51), at: start + 0.999)
        }
        // Exactly a second later a new window opens.
        var boundary = BridgeSession()
        _ = try boundary.receiveText(LokiCodec.encode(hello, seq: 1), at: start)
        for seq in 2...50 { _ = try boundary.receiveText(LokiCodec.encode(.cancel, seq: UInt32(seq)), at: start) }
        #expect(try boundary.receiveText(LokiCodec.encode(.cancel, seq: 51), at: start + 1) == .message(.cancel))
        // The window opened at `start + 1` admits 50 in all.
        for seq in 52...100 { _ = try boundary.receiveText(LokiCodec.encode(.cancel, seq: UInt32(seq)), at: start + 1.5) }
        #expect(throws: SessionError.self) { try boundary.receiveText(LokiCodec.encode(.cancel, seq: 101), at: start + 1.99) }

        // Audio: 60 a window, counted separately from control messages.
        var audio = BridgeSession()
        _ = try audio.receiveText(LokiCodec.encode(hello, seq: 1), at: start)
        let frame = AudioFrame(kind: .mic, final: false, stream: 1, seq: 0, payload: [0, 0]).bytes
        for _ in 0..<60 { #expect(try audio.receiveBinary(frame, at: start + 0.5) == .nothing) }
        #expect(throws: SessionError(code: 1003, reason: "too many audio frames")) {
            try audio.receiveBinary(frame, at: start + 1.49)
        }
        var audioBoundary = BridgeSession()
        _ = try audioBoundary.receiveText(LokiCodec.encode(hello, seq: 1), at: start)
        for _ in 0..<60 { _ = try audioBoundary.receiveBinary(frame, at: start + 0.5) }
        #expect(try audioBoundary.receiveBinary(frame, at: start + 1.5) == .nothing)
        // Control messages still flow while audio is at its limit.
        #expect(try audioBoundary.receiveText(LokiCodec.encode(.cancel, seq: 2), at: start + 1.5) == .message(.cancel))
    }
}

@Test func audioBeforeHelloIsNotCountedAgainstTheLimit() throws {
    var session = BridgeSession()
    let frame = AudioFrame(kind: .mic, final: false, stream: 1, seq: 0, payload: [0, 0]).bytes
    #expect(throws: SessionError(code: 1002, reason: "hello must come first")) { try session.receiveBinary(frame, at: 0) }
    _ = try session.receiveText(LokiCodec.encode(.hello(fw: "1", board: "b", channels: 2, frameMs: 20), seq: 1), at: 0)
    // Two channels: a frame must hold whole sample pairs.
    #expect(throws: SessionError.self) { try session.receiveBinary(frame, at: 0) }
}

@Test func sessionErrorsCarryTheProtocolCloseCodes() {
    var oversized = BridgeSession()
    let long = ascii(#"{"t":"selfie","seq":1,"pad":""# + String(repeating: "x", count: 600) + #""}"#)
    do {
        _ = try oversized.receiveText(long, at: 0)
        Issue.record("an oversized message was accepted")
    } catch {
        #expect(error.code == 1009)
    }
    // 512 bytes is the board's limit: one byte less is judged on its content.
    var atLimit = BridgeSession()
    let pad = 512 - ascii(#"{"t":"hello","seq":1,"pad":""}"#).count
    let exact = ascii(#"{"t":"hello","seq":1,"pad":""# + String(repeating: "x", count: pad) + #""}"#)
    #expect(exact.count == 512)
    do {
        _ = try atLimit.receiveText(exact, at: 0)
        Issue.record("a hello without its fields was accepted")
    } catch {
        #expect(error.code == 1002)  // judged on content: hello lacks its fields
    }
    #expect("\(SessionError(code: 1002, reason: "sequence"))" == "sequence (1002)")
}

// MARK: DeviceSession

private struct DeviceModel {
    var welcomed = false
    var openReply: UInt32?
    var received: UInt32 = 0
    var sent: UInt32 = 0
}

private enum BridgeTextKind { case message(BridgeMessage), unknown, malformed }

@Test func deviceSessionMatchesItsModelOverRandomTraces() {
    var seen: [String: Int] = [:]
    for seed in UInt64(1)...1500 {
        var random = Xorshift(seed: seed)
        var model = DeviceModel()
        var session = DeviceSession()
        var log: [String] = []
        for _ in 0..<300 {
            let roll = random.below(100)
            if roll < 15 {
                // Encoding: hello first and once; a refused message keeps its sequence number.
                let (message, valid): (DeviceMessage, Bool) = random.pick([
                    (.hello(fw: "1", board: "1.75c", channels: 1, frameMs: 60), true),
                    (.hello(fw: "1", board: "1.75c", channels: 0, frameMs: 60), false),
                    (.cancel, true), (.pttStart(stream: 3), true), (.pttStart(stream: 0), false),
                    (.status(battery: 101, charging: false, rssi: 0), false),
                ])
                let isHello = if case .hello = message { true } else { false }
                let refused = isHello != (model.sent == 0) || !valid
                log.append("encode \(message)")
                do {
                    let bytes = try session.encode(message)
                    #expect(!refused, "seed \(seed): \(log)")
                    model.sent += 1
                    #expect(try LokiCodec.decodeDeviceMessage(bytes) == .message(seq: model.sent, message), "seed \(seed)")
                    seen["encoded", default: 0] += 1
                } catch {
                    #expect(refused, "seed \(seed): \(log)")
                    seen["refused", default: 0] += 1
                }
                continue
            }
            let expected: Outcome<DeviceSession.Input>
            let actual: Outcome<DeviceSession.Input>
            if roll < 70 {
                let reply = UInt32(1 + random.below(4))
                let ordinary: [BridgeMessage] = [.state(.idle), .settings(volume: 40), .askCancel(id: "q1"), .stopAudio,
                                                 .replyBegin(reply: reply), .replyEnd(reply: model.openReply ?? reply, frames: 1),
                                                 .replyEnd(reply: reply, frames: 1)]
                let kind: BridgeTextKind = if !random.chance(4) {
                    model.welcomed || random.chance(20) ? (random.chance(15) ? .unknown : .message(random.pick(ordinary)))
                        : .message(.welcome(bridge: "B", voice: "Scar"))
                } else {
                    random.pick([.malformed, .unknown, .message(.welcome(bridge: "again"))])
                }
                let seq = random.chance(2) ? random.pick([model.received, model.received + 2, 0]) : model.received + 1
                let bytes: [UInt8] = switch kind {
                case .message(let message): wire({ try LokiCodec.encode(message, seq: $0) }, seq: seq)
                case .unknown: ascii(#"{"t":"future","seq":\#(seq)}"#)
                case .malformed: ascii(#"{"t":"stop_audio","seq":\#(seq),}"#)
                }
                log.append("text \(String(decoding: bytes.prefix(60), as: UTF8.self))")
                expected = {
                    if case .malformed = kind { return .close(1002) }
                    guard seq == model.received + 1 else { return .close(1002) }
                    model.received += 1
                    guard case .message(let message) = kind else { return model.welcomed ? .input(.nothing) : .close(1002) }
                    if case .welcome(let bridge, let voice) = message {
                        guard !model.welcomed else { return .close(1002) }
                        model.welcomed = true
                        return .input(.welcome(bridge: bridge, voice: voice))
                    }
                    guard model.welcomed else { return .close(1002) }
                    switch message {
                    case .replyBegin(let reply): model.openReply = reply
                    case .replyEnd(let reply, _) where model.openReply == reply: model.openReply = nil
                    case .stopAudio: model.openReply = nil
                    default: break
                    }
                    return .input(.message(message))
                }()
                do { actual = .input(try session.receiveText(bytes)) } catch { actual = .close(error.code) }
            } else {
                let frame = AudioFrame(kind: .reply, final: false, stream: UInt32(1 + random.below(4)),
                                       seq: 0, payload: [1, 2, 3, 4])
                let broken = random.chance(3)
                let bytes = broken ? AudioFrame(kind: .mic, final: false, stream: frame.stream, seq: 0,
                                                payload: [1, 2]).bytes : frame.bytes
                log.append("binary stream \(frame.stream)\(broken ? " (mic)" : "")")
                expected = !model.welcomed ? .close(1002) : broken ? .close(1003)
                    : .input(frame.stream == model.openReply ? .audio(frame) : .nothing)
                do { actual = .input(try session.receiveBinary(bytes)) } catch { actual = .close(error.code) }
            }
            #expect(actual == expected, "seed \(seed): \(log)")
            #expect(session.welcomed == model.welcomed && session.openReply == model.openReply, "seed \(seed): \(log)")
            switch actual {
            case .close(let code): seen["close \(code)", default: 0] += 1
            case .input(let input): seen[String("\(input)".prefix(while: { $0 != "(" })), default: 0] += 1
            }
            if actual != expected || isClose(actual) { break }
        }
    }
    for key in ["close 1002", "close 1003", "welcome", "message", "audio", "nothing", "encoded", "refused"] {
        #expect(seen[key, default: 0] > 10, "the generator rarely reaches \(key): \(seen)")
    }
}

@Test func sessionsAreDeterministic() throws {
    // The same inputs give the same outputs and the same state: the sessions hide no clock.
    for seed in UInt64(1)...50 {
        var first = BridgeTrace(seed: seed), second = BridgeTrace(seed: seed)
        for _ in 0..<100 {
            let a = first.step(bursty: seed % 2 == 0), b = second.step(bursty: seed % 2 == 0)
            #expect(a == b && first.seen == second.seen && first.log == second.log)
            if !a { break }
        }
    }
}
