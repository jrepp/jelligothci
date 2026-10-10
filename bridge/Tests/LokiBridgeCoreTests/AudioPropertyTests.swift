import Foundation
import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

// LokiAudio.read's limits at their edges, a write-then-read round trip, mic payload
// decoding, and seeded header fuzz: a reply file is input the bridge did not write.

/// A 16-bit PCM WAV header for `frames` frames, followed by `body` (silence when nil).
private func wavData(rate: UInt32, channels: UInt16, frames: Int, bits: UInt16 = 16, body: Data? = nil) -> Data {
    var data = Data()
    func put<T: FixedWidthInteger>(_ value: T) { withUnsafeBytes(of: value.littleEndian) { data.append(contentsOf: $0) } }
    let block = UInt32(channels) * UInt32(bits / 8)
    let bytes = UInt32(frames) * block
    data.append(contentsOf: Array("RIFF".utf8)); put(36 + bytes); data.append(contentsOf: Array("WAVEfmt ".utf8))
    put(UInt32(16)); put(UInt16(1)); put(channels); put(rate); put(rate &* block)
    put(UInt16(block)); put(bits); data.append(contentsOf: Array("data".utf8)); put(bytes)
    data.append(body ?? Data(count: Int(bytes)))
    return data
}

private func file(_ data: Data) throws -> URL {
    let url = scratchDirectory().appendingPathComponent("audio.wav")
    try data.write(to: url)
    return url
}

@Test func rateChannelAndLengthLimitsHoldAtTheirEdges() throws {
    let accepted: [(UInt32, UInt16, Int, Int)] = [
        (8_000, 1, 800, 2_400), (192_000, 1, 19_200, 2_400), (24_000, 2, 2_400, 2_400),
        (8_000, 1, 8_000 * 300, 24_000 * 300),  // exactly five minutes
    ]
    for (rate, channels, frames, samples) in accepted {
        let read = try LokiAudio.read(try file(wavData(rate: rate, channels: channels, frames: frames)))
        // Conversion may hold back a little of the resampler's tail, never more.
        #expect(abs(read.count - samples) <= 64, "\(rate) Hz × \(channels): \(read.count)")
    }
    let refused: [(UInt32, UInt16, Int)] = [
        (7_999, 1, 800), (192_001, 1, 19_200), (24_000, 3, 240), (8_000, 1, 8_000 * 300 + 1),
    ]
    for (rate, channels, frames) in refused {
        #expect(throws: (any Error).self, "\(rate) Hz × \(channels), \(frames) frames") {
            try LokiAudio.read(try file(wavData(rate: rate, channels: channels, frames: frames)))
        }
    }
}

@Test func whatWriteWAVWritesReadsBackExactly() throws {
    var state: UInt64 = 0x1234_5678_9ABC_DEF1
    for length in [1, 2, 2_399, 2_400, 24_001] {
        let samples: [Int16] = (0..<length).map { index in
            state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
            return index == 0 ? .min : index == 1 ? .max : Int16(truncatingIfNeeded: state >> 40)
        }
        let url = scratchDirectory().appendingPathComponent("round.wav")
        try LokiAudio.writeWAV(samples, to: url)
        #expect(try LokiAudio.read(url) == samples, "\(length) samples")
    }
}

@Test func micPayloadsDecodeLittleEndianAndDropAnOddByte() {
    func frame(_ payload: [UInt8]) -> AudioFrame { AudioFrame(kind: .mic, final: false, stream: 1, seq: 0, payload: payload) }
    #expect(LokiAudio.samples(of: frame([])).isEmpty)
    #expect(LokiAudio.samples(of: frame([0x7F])).isEmpty)
    #expect(LokiAudio.samples(of: frame([0x01, 0x80])) == [Int16(bitPattern: 0x8001)])
    #expect(LokiAudio.samples(of: frame([0xFF, 0x7F, 0x00, 0x80, 0x05])) == [.max, .min])
}

/// Seeded fuzz of the header and body: whatever the file says, reading it either
/// refuses or returns at most five minutes of samples, and never traps.
@Test func corruptedHeadersAreRefusedOrBounded() throws {
    let interesting: [UInt32] = [0, 1, 2, 3, 7_999, 8_000, 24_000, 192_000, 192_001, 0x7FFF_FFFF, 0xFFFF_FFFF]
    var state: UInt64 = 0xDEAD_BEEF_CAFE_F00D
    func next(_ bound: Int) -> Int {
        state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
        return Int((state >> 33) % UInt64(bound))
    }
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let limit = Int(LokiAudio.maxSeconds * LokiAudio.rate) + 1_024
    for seed in 0..<120 {
        var data = wavData(rate: 24_000, channels: 1, frames: 480 + next(4_000))
        for _ in 0...next(3) {
            // A field of the header (4-byte or 2-byte) set to an interesting value, or a byte flipped.
            let value = interesting[next(interesting.count)]
            switch next(3) {
            case 0: let at = [4, 16, 24, 28, 40][next(5)]; withUnsafeBytes(of: value.littleEndian) { data.replaceSubrange(at..<at + 4, with: $0) }
            case 1: let at = [20, 22, 32, 34][next(4)]; withUnsafeBytes(of: UInt16(truncatingIfNeeded: value).littleEndian) { data.replaceSubrange(at..<at + 2, with: $0) }
            default: data[next(44)] ^= UInt8(1 << next(8))
            }
        }
        if next(4) == 0 { data = data.prefix(next(data.count)) }  // truncated
        let url = directory.appendingPathComponent("fuzz-\(seed).wav")
        try data.write(to: url)
        if let samples = try? LokiAudio.read(url) {
            #expect(samples.count <= limit, "seed \(seed)")
        }
    }
    #expect(throws: (any Error).self) { try LokiAudio.read(try file(Data((0..<512).map { UInt8(truncatingIfNeeded: $0 &* 37) }))) }
    #expect(throws: (any Error).self) { try LokiAudio.read(directory.appendingPathComponent("missing.wav")) }
}
