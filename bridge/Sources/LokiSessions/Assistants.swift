import Foundation
import LokiProtocol

/// Who answers a request (ADR-018): Claude, through Claude Code, or a model Ollama
/// runs on the Mac.
public enum Assistant: Equatable, Sendable {
    case claude, ollama
}

/// The user's choice of model: the default for requests that name no one, and the
/// Ollama model "ask Ollama" uses. Kept by the bridge, outside any session's reach.
public struct ModelChoice: Codable, Equatable, Sendable {
    /// What the board's list calls Claude.
    public static let claude = "Claude"
    /// The Ollama model used until the user picks one, Michael's choice (ADR-018).
    public static let firstOllamaModel = "qwen3.8:27b-mlx"

    /// "Claude" or an Ollama model's name.
    public var defaultModel: String
    /// The last Ollama model picked.
    public var ollamaModel: String

    public init(defaultModel: String = ModelChoice.claude, ollamaModel: String = ModelChoice.firstOllamaModel) {
        self.defaultModel = defaultModel
        self.ollamaModel = ollamaModel
    }

    /// Who a request that names no assistant goes to.
    public var defaultAssistant: Assistant { defaultModel == Self.claude ? .claude : .ollama }

    /// The user picked `model` from the board's list.
    public mutating func pick(_ model: String) {
        defaultModel = model
        if model != Self.claude { ollamaModel = model }
    }

    /// The board's list: Claude, then the Ollama models offered, at most 16 in all and
    /// each a valid model name, with the default kept in the last place when it would
    /// be cut off; and the index of the default, Claude's when it is not offered.
    public func offer(_ ollama: [String]) -> (names: [String], current: UInt32) {
        var names = [Self.claude]
        for name in ollama where Self.valid(name) && !names.contains(name) { names.append(name) }
        if names.count > Self.maxOffered {
            let cut = names.firstIndex(of: defaultModel).map { $0 >= Self.maxOffered } ?? false
            names = Array(names.prefix(Self.maxOffered))
            if cut { names[Self.maxOffered - 1] = defaultModel }
        }
        return (names, UInt32(names.firstIndex(of: defaultModel) ?? 0))
    }

    public static let maxOffered = 16

    /// A name loki/1 can carry: 1 to 40 of A-Z a-z 0-9 . _ : / + -.
    public static func valid(_ name: String) -> Bool { Loki.isModelName(name) }
}

/// Where the choice is kept between runs.
public protocol ModelChoiceStore: Sendable {
    func load() -> ModelChoice
    func save(_ choice: ModelChoice) throws
}

/// The choice in memory only: for tests, and when no file is given.
public final class MemoryModelChoiceStore: ModelChoiceStore, @unchecked Sendable {
    private let lock = NSLock()
    private var choice: ModelChoice
    public init(_ choice: ModelChoice = ModelChoice()) { self.choice = choice }
    public func load() -> ModelChoice { lock.withLock { choice } }
    public func save(_ choice: ModelChoice) throws { lock.withLock { self.choice = choice } }
}

/// The choice in a JSON file of the user's, readable by no one else (0600), written
/// whole through a temporary file. A missing or unreadable file is the first choice.
public struct FileModelChoiceStore: ModelChoiceStore {
    public let file: URL
    public init(file: URL) { self.file = file }

    public func load() -> ModelChoice {
        guard let data = try? Data(contentsOf: file), data.count <= 4096,
              let choice = try? JSONDecoder().decode(ModelChoice.self, from: data),
              choice.defaultModel == ModelChoice.claude || ModelChoice.valid(choice.defaultModel),
              ModelChoice.valid(choice.ollamaModel)
        else { return ModelChoice() }
        return choice
    }

    public func save(_ choice: ModelChoice) throws {
        let data = try JSONEncoder().encode(choice)
        let folder = file.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true,
                                                attributes: [.posixPermissions: 0o700])
        let temporary = folder.appendingPathComponent(".\(file.lastPathComponent).\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: temporary) }  // gone after a replace; left by a failure
        guard FileManager.default.createFile(atPath: temporary.path, contents: data, attributes: [.posixPermissions: 0o600])
        else { throw CocoaError(.fileWriteUnknown) }
        // The new file's own metadata (0600), not an old file's, which a replace would keep.
        _ = try FileManager.default.replaceItemAt(file, withItemAt: temporary, options: .usingNewMetadataOnly)
    }
}

/// Models Ollama runs on the Mac, as the conductor needs them (OllamaRunner).
public protocol LocalModels: Sendable {
    /// The models that run on the Mac and can use tools, most recently changed first.
    func models() async -> [String]
    /// Answers `text` in `conversation` (kept for follow-ups) with `model`, calling the
    /// web search tools as it needs; `report` hears a short line for each tool call.
    func answer(_ text: String, model: String, conversation: String,
                report: @escaping @Sendable (String) async -> Void) async throws -> String
    /// Forgets a conversation.
    func forget(_ conversation: String) async
    /// Loads a model, so the request that follows does not wait for it.
    func warm(_ model: String) async
}
