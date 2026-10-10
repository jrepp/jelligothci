import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// The conductor keeps at most 30 seconds of an utterance, like the board, whatever a
// board sends (memo-027: the link's security review).

extension Board: BoardLink {}

/// Records what the conductor feeds the recognizer; `fails` makes `begin` throw.
final class FakeTranscriber: Transcribing, @unchecked Sendable {
    private let lock = NSLock()
    private var fed: [[Int16]] = []
    private var counts = (begin: 0, finish: 0, cancel: 0)
    let fails: Bool
    let text: String

    init(fails: Bool = false, text: String = "") {
        self.fails = fails
        self.text = text
    }

    func begin(partial: @escaping @Sendable (String) -> Void) async throws {
        lock.withLock { counts.begin += 1 }
        if fails { throw CancellationError() }
    }

    func feed(_ samples: [Int16]) async { lock.withLock { fed.append(samples) } }

    func finish() async throws -> String {
        lock.withLock { counts.finish += 1 }
        return text
    }

    func cancel() async { lock.withLock { counts.cancel += 1 } }

    var chunks: [[Int16]] { lock.withLock { fed } }
    var samples: [Int16] { chunks.flatMap { $0 } }
    var calls: (begin: Int, finish: Int, cancel: Int) { lock.withLock { counts } }
}

final class FakeSpeaker: Speaking, @unchecked Sendable {
    private let lock = NSLock()
    private var said: [String] = []
    private var stops = 0
    func say(_ text: String) async { lock.withLock { said.append(text) } }
    func interrupt() async { lock.withLock { stops += 1 } }
    var lines: [String] { lock.withLock { said } }
    var interrupts: Int { lock.withLock { stops } }
    /// What was said besides "Okay, in X.", which starts each job in a project.
    var announced: [String] { lines.filter { !isAcknowledgement($0) } }
}

/// The line that starts a job in a project ("Okay, in chatter.").
func isAcknowledgement(_ line: String) -> Bool { line.hasPrefix("Okay, in ") }

/// A sidecar that never answers and records the lines it was sent.
final class MemorySidecar: SidecarTransport, @unchecked Sendable {
    let lines: AsyncStream<Data>
    private let lock = NSLock()
    private var sent: [Data] = []
    init() { lines = AsyncStream { _ in } }
    func send(_ line: Data) { lock.withLock { sent.append(line) } }
    var count: Int { lock.withLock { sent.count } }
}

private struct Rig {
    let board = Board()
    let transcriber: FakeTranscriber
    let speaker = FakeSpeaker()
    let sidecar = MemorySidecar()
    let conductor: Conductor

    init(transcriber: FakeTranscriber = FakeTranscriber()) {
        self.transcriber = transcriber
        let nowhere = FileManager.default.temporaryDirectory.appendingPathComponent("loki-no-projects-\(UUID())")
        conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar), transcriber: transcriber,
                              speaker: speaker, resolver: ProjectResolver(roots: [nowhere]))
    }
}

private let cap = Conductor.maxUtteranceSamples

/// Samples numbered from `start`, so what was fed can be checked as a prefix.
private func samples(_ count: Int, from start: Int) -> [Int16] {
    (0..<count).map { Int16(truncatingIfNeeded: start + $0) }
}

@Test func theCapIsThirtySecondsOfBoardAudio() {
    #expect(cap == 24_000 * 30)
}

@Test func anUtteranceKeepsExactlyThirtySecondsInOrder() async {
    let rig = Rig()
    await rig.conductor.handle(.pttStart)
    // 1,234-sample frames do not divide the cap: the frame that crosses it is cut.
    var sent = 0
    while sent < cap + 10 * 1234 {
        await rig.conductor.handle(.audio(samples(1234, from: sent)))
        sent += 1234
    }
    let fed = rig.transcriber.samples
    #expect(fed.count == cap)
    #expect(fed == samples(cap, from: 0))
    #expect(rig.transcriber.chunks.count == (cap + 1233) / 1234)
    #expect(rig.transcriber.chunks.last?.count == cap - (cap / 1234) * 1234)
    // The utterance still ends normally once full.
    await rig.conductor.handle(.pttEnd)
    #expect(rig.transcriber.calls.finish == 1)
    #expect(rig.board.messages.contains { if case .caption(_, true) = $0 { true } else { false } })
}

@Test func oneHugeFrameIsCutToTheCapAndEmptyFramesAreIgnored() async {
    let rig = Rig()
    await rig.conductor.handle(.pttStart)
    await rig.conductor.handle(.audio([]))
    #expect(rig.transcriber.chunks.isEmpty)
    await rig.conductor.handle(.audio(samples(cap + 1, from: 0)))
    await rig.conductor.handle(.audio([1]))
    #expect(rig.transcriber.chunks.map(\.count) == [cap])
}

@Test func eachUtteranceGetsItsOwnThirtySeconds() async {
    let rig = Rig()
    for _ in 0..<2 {
        await rig.conductor.handle(.pttStart)
        await rig.conductor.handle(.audio(samples(cap, from: 0)))
        await rig.conductor.handle(.audio([7]))
        await rig.conductor.handle(.pttEnd)
    }
    #expect(rig.transcriber.chunks.map(\.count) == [cap, cap])
    // A cancelled utterance also starts the next afresh.
    await rig.conductor.handle(.pttStart)
    await rig.conductor.handle(.audio(samples(cap - 1, from: 0)))
    await rig.conductor.handle(.cancel)
    await rig.conductor.handle(.pttStart)
    await rig.conductor.handle(.audio(samples(2, from: 0)))
    #expect(rig.transcriber.chunks.map(\.count) == [cap, cap, cap - 1, 2])
}

@Test func audioOutsideAnUtteranceIsNeverFed() async {
    let rig = Rig()
    await rig.conductor.handle(.audio([1, 2, 3]))  // before any press
    await rig.conductor.handle(.pttStart)
    await rig.conductor.handle(.pttEnd)
    await rig.conductor.handle(.audio([1, 2, 3]))  // after the release
    await rig.conductor.handle(.pttStart)
    await rig.conductor.handle(.gone)
    await rig.conductor.handle(.audio([1, 2, 3]))  // after the board left
    #expect(rig.transcriber.chunks.isEmpty)
    #expect(rig.transcriber.calls.cancel == 1)
    // A recognizer that cannot start is fed nothing either.
    let broken = Rig(transcriber: FakeTranscriber(fails: true))
    await broken.conductor.handle(.pttStart)
    await broken.conductor.handle(.audio([1, 2, 3]))
    await broken.conductor.handle(.pttEnd)
    #expect(broken.transcriber.chunks.isEmpty && broken.transcriber.calls.finish == 0)
}

/// Property: for any seeded mix of frame sizes (empty to over the cap), what is fed is
/// exactly the first min(total, cap) samples, in order, and never an empty chunk.
@Test(arguments: 0..<24)
func whatIsFedIsAlwaysThePrefixUpToTheCap(_ seed: Int) async {
    var state = UInt64(seed) &* 0x9E37_79B9_7F4A_7C15 | 1
    func below(_ bound: Int) -> Int {
        state ^= state << 13
        state ^= state >> 7
        state ^= state << 17
        return Int(state % UInt64(bound))
    }
    let rig = Rig()
    await rig.conductor.handle(.pttStart)
    var total = 0
    for _ in 0..<(1 + below(40)) {
        let size = switch below(6) {
        case 0: 0
        case 1: below(cap + 50_000)
        default: below(80_000)
        }
        await rig.conductor.handle(.audio(samples(size, from: total)))
        total += size
    }
    let fed = rig.transcriber.samples
    #expect(fed.count == min(total, cap), "seed \(seed)")
    #expect(fed == samples(min(total, cap), from: 0), "seed \(seed)")
    #expect(rig.transcriber.chunks.allSatisfy { !$0.isEmpty }, "seed \(seed)")
}
