import AVFoundation
import Foundation
import Testing
@testable import LokiSpeech

/// Speech made on the spot with macOS's `say`, at the board's 24 kHz mono 16-bit.
private func spoken(_ text: String) throws -> [Int16] {
    let file = FileManager.default.temporaryDirectory.appendingPathComponent("loki-say-\(UUID()).wav")
    defer { try? FileManager.default.removeItem(at: file) }
    let say = Process()
    say.executableURL = URL(fileURLWithPath: "/usr/bin/say")
    say.arguments = ["--file-format=WAVE", "--data-format=LEI16@24000", "-o", file.path, text]
    try say.run()
    say.waitUntilExit()
    let audio = try AVAudioFile(forReading: file, commonFormat: .pcmFormatInt16, interleaved: true)
    let buffer = AVAudioPCMBuffer(pcmFormat: audio.processingFormat, frameCapacity: AVAudioFrameCount(audio.length))!
    try audio.read(into: buffer)
    return Array(UnsafeBufferPointer(start: buffer.int16ChannelData![0], count: Int(buffer.frameLength)))
}

final class Partials: @unchecked Sendable {
    private var texts: [String] = []
    private let lock = NSLock()
    func add(_ text: String) { lock.withLock { texts.append(text) } }
    var all: [String] { lock.withLock { texts } }
}

@Test func hearsSpeechFedLikeTheBoardsFrames() async throws {
    guard await LiveTranscriber.isReady() else { return }  // The model is not installed on this Mac.
    let samples = try spoken("Please run the tests in codergotchi.")
    #expect(samples.count > 24_000)
    let partials = Partials()
    let transcriber = LiveTranscriber(contextualStrings: ["codergotchi"])
    try await transcriber.begin { partials.add($0) }
    for start in stride(from: 0, to: samples.count, by: 1440) {  // 60 ms frames
        await transcriber.feed(Array(samples[start..<min(samples.count, start + 1440)]))
    }
    let text = try await transcriber.finish().lowercased()
    #expect(text.contains("tests"), "heard: \(text)")
    #expect(!partials.all.isEmpty)
}

@Test func anUtteranceCanBeCancelledAndANewOneBegun() async throws {
    guard await LiveTranscriber.isReady() else { return }
    let transcriber = LiveTranscriber()
    try await transcriber.begin { _ in }
    await transcriber.feed([Int16](repeating: 0, count: 2400))
    await transcriber.cancel()
    #expect(try await transcriber.finish() == "")
    try await transcriber.begin { _ in }
    await transcriber.feed(try spoken("Hello."))
    #expect(try await transcriber.finish().lowercased().contains("hello"))
}

@Test func spacingIsTidied() {
    #expect(LiveTranscriber.tidy("  run   the\ttests \n") == "run the tests")
}
