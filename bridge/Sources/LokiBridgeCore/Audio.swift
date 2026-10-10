import AVFoundation
import Foundation
import LokiProtocol

public struct AudioError: Error, CustomStringConvertible, Sendable {
    public let description: String
    init(_ description: String) { self.description = description }
}

/// loki/1 audio is 24 kHz, 16-bit, mono in both directions (docs/loki-protocol.md).
public enum LokiAudio {
    public static let rate = Double(Loki.sampleRate)

    /// Longest reply read: 5 minutes. Files outside 8 to 192 kHz, or with more than two
    /// channels, are refused before any arithmetic on their length.
    public static let maxSeconds = 300.0

    /// Reads an audio file as 24 kHz mono 16-bit samples. Chatter writes 24 kHz 24-bit
    /// WAV, so its replies are read with `pcmOnly`, which refuses compressed formats
    /// before any codec sees them; `play FILE` accepts anything AVFoundation opens.
    public static func read(_ url: URL, pcmOnly: Bool = false) throws -> [Int16] {
        let file = try AVAudioFile(forReading: url)
        if pcmOnly, file.fileFormat.settings[AVFormatIDKey] as? UInt32 != kAudioFormatLinearPCM {
            throw AudioError("\(url.lastPathComponent) is not linear PCM")
        }
        let format = file.processingFormat
        guard (8_000...192_000).contains(format.sampleRate), (1...2).contains(format.channelCount), file.length > 0,
              Double(file.length) / format.sampleRate <= maxSeconds
        else { throw AudioError("\(url.lastPathComponent) is empty, too long, or in an unexpected format") }
        guard let target = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: rate, channels: 1,
                                          interleaved: true),
              let converter = AVAudioConverter(from: file.processingFormat, to: target)
        else { throw AudioError("cannot convert \(url.lastPathComponent)") }
        let frames = AVAudioFrameCount(file.length)
        guard let input = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: max(frames, 1)) else {
            throw AudioError("cannot read \(url.lastPathComponent)")
        }
        try file.read(into: input)
        let capacity = AVAudioFrameCount(Double(frames) * rate / file.processingFormat.sampleRate) + 1024
        guard let output = AVAudioPCMBuffer(pcmFormat: target, frameCapacity: capacity) else {
            throw AudioError("cannot convert \(url.lastPathComponent)")
        }
        var fed = false
        var failure: NSError?
        let status = converter.convert(to: output, error: &failure) { _, state in
            if fed {
                state.pointee = .endOfStream
                return nil
            }
            fed = true
            state.pointee = .haveData
            return input
        }
        guard status != .error, let samples = output.int16ChannelData else {
            throw AudioError("cannot convert \(url.lastPathComponent): \(failure?.localizedDescription ?? "unknown")")
        }
        return Array(UnsafeBufferPointer(start: samples[0], count: Int(output.frameLength)))
    }

    /// Writes 24 kHz mono 16-bit samples as a WAV file.
    public static func writeWAV(_ samples: [Int16], to url: URL) throws {
        var data = Data()
        func append<T: FixedWidthInteger>(_ value: T) { withUnsafeBytes(of: value.littleEndian) { data.append(contentsOf: $0) } }
        let bytes = UInt32(samples.count * 2)
        data.append(contentsOf: Array("RIFF".utf8)); append(36 + bytes)
        data.append(contentsOf: Array("WAVEfmt ".utf8)); append(UInt32(16)); append(UInt16(1)); append(UInt16(1))
        append(UInt32(rate)); append(UInt32(rate) * 2); append(UInt16(2)); append(UInt16(16))
        data.append(contentsOf: Array("data".utf8)); append(bytes)
        samples.withUnsafeBufferPointer { buffer in
            for sample in buffer { append(sample) }
        }
        try data.write(to: url, options: .atomic)
    }

    /// The samples of a mic frame's payload.
    public static func samples(of frame: AudioFrame) -> [Int16] {
        stride(from: 0, to: frame.payload.count - 1, by: 2).map {
            Int16(bitPattern: UInt16(frame.payload[$0]) | UInt16(frame.payload[$0 + 1]) << 8)
        }
    }
}

/// Sends a spoken reply to the board in 100 ms frames, a second ahead of real time
/// and then at its pace, so the board's 10-second queue never overflows.
public enum ReplyStreamer {
    public static let frameSamples = 2400

    /// Returns false when the board went away part way.
    public static func stream(_ samples: [Int16], reply: UInt32, through server: LinkServer,
                              lead: Double = 1.0) async -> Bool {
        guard server.send(.replyBegin(reply: reply)) else { return false }
        let start = ContinuousClock.now
        var frames: UInt32 = 0
        for (index, offset) in stride(from: 0, to: samples.count, by: frameSamples).enumerated() {
            let chunk = samples[offset..<min(samples.count, offset + frameSamples)]
            var payload = [UInt8]()
            payload.reserveCapacity(chunk.count * 2)
            for sample in chunk {
                payload.append(UInt8(truncatingIfNeeded: sample))
                payload.append(UInt8(truncatingIfNeeded: sample >> 8))
            }
            let last = offset + frameSamples >= samples.count
            guard server.sendAudio(AudioFrame(kind: .reply, final: last, stream: reply, seq: UInt32(index),
                                              payload: payload))
            else { return false }
            frames += 1
            // Frame n is due n frame-lengths after the start, less the lead.
            let due = Double(index + 1) * Double(frameSamples) / LokiAudio.rate - lead
            if due > 0 { try? await Task.sleep(until: start + .milliseconds(Int(due * 1000)), clock: .continuous) }
        }
        return server.send(.replyEnd(reply: reply, frames: frames))
    }
}
