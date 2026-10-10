import Foundation
import Testing
@testable import LokiBridgeCore

/// A 16-bit PCM WAV of silence with any rate and channel count.
private func wav(rate: UInt32, channels: UInt16, frames: Int) throws -> URL {
    var data = Data()
    func put<T: FixedWidthInteger>(_ value: T) { withUnsafeBytes(of: value.littleEndian) { data.append(contentsOf: $0) } }
    let bytes = UInt32(frames) * UInt32(channels) * 2
    data.append(contentsOf: Array("RIFF".utf8)); put(36 + bytes); data.append(contentsOf: Array("WAVEfmt ".utf8))
    put(UInt32(16)); put(UInt16(1)); put(channels); put(rate); put(rate * UInt32(channels) * 2)
    put(channels * 2); put(UInt16(16)); data.append(contentsOf: Array("data".utf8)); put(bytes)
    data.append(Data(count: Int(bytes)))
    let url = scratchDirectory().appendingPathComponent("audio.wav")
    try data.write(to: url)
    return url
}

@Test func repliesInOrdinaryFormatsAreRead() throws {
    #expect(try LokiAudio.read(try wav(rate: 24_000, channels: 1, frames: 2_400)).count == 2_400)
    #expect(try LokiAudio.read(try wav(rate: 48_000, channels: 2, frames: 4_800)).count == 2_400)
}

@Test func extremeFilesAreRefusedBeforeTheirLengthIsUsed() throws {
    let refused: [URL] = [
        try wav(rate: 4_000, channels: 1, frames: 400),                        // too slow a rate
        try wav(rate: 8_000, channels: 1, frames: 8_000 * 301),                // longer than five minutes
        try wav(rate: 24_000, channels: 4, frames: 240),                       // more than two channels
        try wav(rate: 24_000, channels: 1, frames: 0),                         // empty
    ]
    for url in refused { #expect(throws: (any Error).self) { try LokiAudio.read(url) } }
}
