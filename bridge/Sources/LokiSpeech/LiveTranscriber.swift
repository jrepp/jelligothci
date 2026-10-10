@preconcurrency import AVFoundation
import Foundation
import Speech

/// On-device transcription of one utterance as it arrives (RFC-003): the board's
/// 24 kHz mono frames go into SpeechAnalyzer while the user holds, partial text comes
/// back for live captions, and the final text follows soon after the user lets go.
/// Nothing leaves the Mac. Project names given as contextual strings help it hear
/// them ("codergotchi", "chatter").
public actor LiveTranscriber {
    public static let defaultLocale = Locale(identifier: "en-US")

    private let locale: Locale
    private let contextualStrings: [String]
    private var analyzer: SpeechAnalyzer?
    private var input: AsyncStream<AnalyzerInput>.Continuation?
    private var reader: Task<String, Error>?
    private var converter: AVAudioConverter?
    private var target: AVAudioFormat?
    private let source = AVAudioFormat(commonFormat: .pcmFormatInt16, sampleRate: 24_000, channels: 1,
                                       interleaved: true)!

    public init(locale: Locale = defaultLocale, contextualStrings: [String] = []) {
        self.locale = locale
        self.contextualStrings = contextualStrings
    }

    /// Whether the on-device model for the locale is installed. The bridge never
    /// downloads it on its own.
    public static func isReady(_ locale: Locale = defaultLocale) async -> Bool {
        let identifier = locale.identifier(.bcp47)
        return await SpeechTranscriber.installedLocales.contains { $0.identifier(.bcp47) == identifier }
    }

    /// Starts an utterance. `partial` receives the text so far (final segments and the
    /// current guess) each time it changes.
    public func begin(partial: @escaping @Sendable (String) -> Void) async throws {
        await cancel()
        let transcriber = SpeechTranscriber(locale: locale, transcriptionOptions: [],
                                            reportingOptions: [.volatileResults], attributeOptions: [])
        guard await Self.isReady(locale),
              (try? await AssetInventory.assetInstallationRequest(supporting: [transcriber])) == nil
        else { throw SpeechError("the on-device speech model for \(locale.identifier) is not installed") }
        guard let format = await SpeechAnalyzer.bestAvailableAudioFormat(compatibleWith: [transcriber]),
              let converter = AVAudioConverter(from: source, to: format)
        else { throw SpeechError("SpeechAnalyzer has no audio format for this utterance") }
        let analyzer = SpeechAnalyzer(modules: [transcriber])
        if !contextualStrings.isEmpty {
            let context = AnalysisContext()
            context.contextualStrings[.general] = contextualStrings
            try await analyzer.setContext(context)
        }
        let (stream, continuation) = AsyncStream.makeStream(of: AnalyzerInput.self)
        reader = Task {
            var final = ""
            for try await result in transcriber.results {
                let text = String(result.text.characters)
                if result.isFinal {
                    final += text
                    partial(Self.tidy(final))
                } else {
                    partial(Self.tidy(final + text))
                }
            }
            return Self.tidy(final)
        }
        try await analyzer.start(inputSequence: stream)
        self.analyzer = analyzer
        self.input = continuation
        self.converter = converter
        self.target = format
    }

    /// Adds 16-bit samples at 24 kHz.
    public func feed(_ samples: [Int16]) {
        guard let input, let converter, let target, !samples.isEmpty,
              let buffer = AVAudioPCMBuffer(pcmFormat: source, frameCapacity: AVAudioFrameCount(samples.count))
        else { return }
        buffer.frameLength = AVAudioFrameCount(samples.count)
        samples.withUnsafeBufferPointer { buffer.int16ChannelData![0].update(from: $0.baseAddress!, count: samples.count) }
        let capacity = AVAudioFrameCount(Double(samples.count) * target.sampleRate / 24_000) + 64
        guard let output = AVAudioPCMBuffer(pcmFormat: target, frameCapacity: capacity) else { return }
        var fed = false
        var failure: NSError?
        _ = converter.convert(to: output, error: &failure) { _, state in
            if fed {
                state.pointee = .noDataNow
                return nil
            }
            fed = true
            state.pointee = .haveData
            return buffer
        }
        if failure == nil, output.frameLength > 0 { input.yield(AnalyzerInput(buffer: output)) }
    }

    /// Ends the utterance and returns its text.
    public func finish() async throws -> String {
        guard let analyzer, let reader else { return "" }
        input?.finish()
        input = nil
        defer { self.analyzer = nil; self.reader = nil; converter = nil }
        do {
            try await analyzer.finalizeAndFinishThroughEndOfInput()
        } catch {
            await analyzer.cancelAndFinishNow()
            _ = try? await reader.value
            throw SpeechError("transcription failed: \(error.localizedDescription)")
        }
        return try await reader.value
    }

    /// Drops the utterance, for "cancel" or a board that went away.
    public func cancel() async {
        input?.finish()
        input = nil
        if let analyzer { await analyzer.cancelAndFinishNow() }
        reader?.cancel()
        analyzer = nil
        reader = nil
        converter = nil
    }

    static func tidy(_ text: String) -> String {
        text.split(whereSeparator: \.isWhitespace).joined(separator: " ")
    }
}
