import Foundation
@testable import LokiProtocol

/// The board's C codec, driven through tests/loki_diff.c so the differential tests can
/// give both implementations the same inputs. It is compiled once per test run from
/// the repository's sources, with address and undefined-behavior sanitizers, so a
/// stale build cannot hide a difference and a memory error in the C decoder fails the
/// run. LOKI_DIFF_TOOL names a prebuilt driver instead.
enum BoardCodec {
    struct Failure: Error, CustomStringConvertible, Sendable {
        let description: String
    }

    static let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../..").standardized

    static let tool: Result<URL, Failure> = {
        if let path = ProcessInfo.processInfo.environment["LOKI_DIFF_TOOL"], !path.isEmpty {
            return .success(URL(fileURLWithPath: path))
        }
        do {
            let directory = try scratchDirectory()
            let output = directory.appendingPathComponent("loki_diff")
            let sources = ["core/agent/loki.c", "core/agent/loki_canonical.c", "core/agent/loki_json.c",
                           "core/agent/loki_out.c", "core/agent/loki_schema.c", "core/agent/sha256.c",
                           "tests/loki_diff.c"]
            let arguments = ["clang", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                             "-I", root.appendingPathComponent("include").path]
                + sources.map { root.appendingPathComponent($0).path } + ["-o", output.path]
            let status = try execute(URL(fileURLWithPath: "/usr/bin/xcrun"), arguments,
                                     input: Data(), in: directory)
            guard status.code == 0 else {
                return .failure(Failure(description: "cannot build the C driver:\n\(status.errors)"))
            }
            return .success(output)
        } catch {
            return .failure(Failure(description: "cannot build the C driver: \(error)"))
        }
    }()

    /// One result line per command (see tests/loki_diff.c).
    static func run(_ commands: [String]) throws -> [String] {
        let executable = try tool.get()
        let directory = try scratchDirectory()
        defer { try? FileManager.default.removeItem(at: directory) }
        let input = Data((commands.joined(separator: "\n") + "\n").utf8)
        let result = try execute(executable, [], input: input, in: directory)
        guard result.code == 0 else {
            throw Failure(description: "the C driver failed (\(result.code)):\n\(result.errors)")
        }
        var lines = result.output.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        if lines.last == "" { lines.removeLast() }
        guard lines.count == commands.count else {
            throw Failure(description: "the C driver answered \(lines.count) of \(commands.count) commands")
        }
        return lines
    }

    private static func scratchDirectory() throws -> URL {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("loki-diff-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        return directory
    }

    /// Runs a program with stdin, stdout and stderr in files, so neither side can block
    /// on a full pipe.
    private static func execute(_ program: URL, _ arguments: [String], input: Data, in directory: URL) throws
        -> (code: Int32, output: String, errors: String) {
        let files = ["in", "out", "err"].map { directory.appendingPathComponent("io.\($0)") }
        try input.write(to: files[0])
        for file in files.dropFirst() { try Data().write(to: file) }
        let process = Process()
        process.executableURL = program
        process.arguments = arguments
        process.standardInput = try FileHandle(forReadingFrom: files[0])
        process.standardOutput = try FileHandle(forWritingTo: files[1])
        process.standardError = try FileHandle(forWritingTo: files[2])
        try process.run()
        process.waitUntilExit()
        let read = { (file: URL) in String(decoding: (try? Data(contentsOf: file)) ?? Data(), as: UTF8.self) }
        return (process.terminationStatus, read(files[1]), read(files[2]))
    }

    // MARK: The command and result formats

    private static let digits = Array("0123456789abcdef".utf8)

    /// "x" and lowercase hex.
    static func hex(_ bytes: [UInt8]) -> String {
        var out: [UInt8] = [0x78]
        out.reserveCapacity(1 + 2 * bytes.count)
        for byte in bytes {
            out.append(digits[Int(byte >> 4)])
            out.append(digits[Int(byte & 0x0F)])
        }
        return String(decoding: out, as: UTF8.self)
    }

    /// What the bridge's codec makes of a bridge-to-board message, written as the C
    /// driver writes it.
    static func swiftDecode(_ bytes: [UInt8]) -> String {
        do {
            switch try LokiCodec.decodeBridgeMessage(bytes) {
            case .ignored(let seq):
                return "ignore\t\(seq)"
            case .message(let seq, let message):
                var line = "ok\t" + LokiCodec.canonical(message, seq: seq)
                if case .ask(let ask) = message { line += "\t" + ask.digest }
                return line
            }
        } catch {
            return "reject"
        }
    }

    static func swiftAudio(_ bytes: [UInt8], kind: AudioFrame.Kind, channels: Int) -> String {
        guard let frame = try? AudioFrame(parsing: bytes, expected: kind, channels: channels) else {
            return "reject"
        }
        return "ok\t" + frame.canonical
    }

    /// The encode command for a message the board sends.
    static func encodeCommand(_ message: DeviceMessage, seq: UInt32) -> String {
        let text = { (value: String) in hex(Array(value.utf8)) }
        let fields: String = switch message {
        case .hello(let fw, let board, let channels, let frameMs):
            "hello \(seq) \(text(fw)) \(text(board)) \(channels) \(frameMs)"
        case .pttStart(let stream): "ptt_start \(seq) \(stream)"
        case .pttEnd(let stream, let frames, let dropped): "ptt_end \(seq) \(stream) \(frames) \(dropped)"
        case .cancel: "cancel \(seq)"
        case .answer(let id, let nonce, let digest, let choice):
            "answer \(seq) \(text(id)) \(text(nonce)) \(text(digest)) \(choice)"
        case .played(let reply, let complete): "played \(seq) \(reply) \(complete ? 1 : 0)"
        case .status(let battery, let charging, let rssi): "status \(seq) \(battery) \(charging ? 1 : 0) \(rssi)"
        }
        return "e " + fields
    }

    static func unhex(_ text: Substring) -> [UInt8] {
        let digits = Array(text.utf8)
        return stride(from: 0, to: digits.count - 1, by: 2).map { i in
            UInt8(JSONReader.hexValue(digits[i])! << 4 | JSONReader.hexValue(digits[i + 1])!)
        }
    }
}
