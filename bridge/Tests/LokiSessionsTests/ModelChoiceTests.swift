import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// The user's choice of model (ADR-018): the list the board is offered, seeded so a
// failure replays from its seed, and the file the choice is kept in.

/// A model name Ollama might report: valid, at a limit, or one loki/1 cannot carry.
private func ollamaName(_ random: inout Seeded) -> String {
    switch random.below(10) {
    case 0: return random.pick(["", "a b", "gemma\u{e9}:1", "x,y", "q\"1", String(repeating: "z", count: 41), "tab\there", "Claude"])
    case 1: return String(repeating: random.pick(["m", "+", "/"]), count: random.pick([1, 40]))
    default: return "\(random.pick(["qwen3", "gemma4", "granite4.1", "llama3.2", "hf.co/org/m"])):\(random.below(30))b"
    }
}

/// Property: the list is Claude first, then valid unique names in the order given, at
/// most 16; the index names the default when the default is offered (and Claude
/// otherwise); a default that would be cut takes the last place; the protocol carries
/// it, and the board decodes the same list.
@Test(arguments: 0..<300)
func theOfferedListHoldsEveryRule(_ seed: Int) throws {
    var random = Seeded(seed)
    let ollama = (0..<random.below(40)).map { _ in ollamaName(&random) }
    let valid = ollama.reduce(into: [String]()) { list, name in
        if ModelChoice.valid(name), name != ModelChoice.claude, !list.contains(name) { list.append(name) }
    }
    let defaultModel = switch random.below(4) {
    case 0: ModelChoice.claude
    case 1: valid.isEmpty ? "gone:1" : random.pick(valid)
    case 2: valid.isEmpty ? "gone:1" : valid[valid.count - 1 - random.below(min(valid.count, 5))]  // often past 16
    default: random.pick(["gone:1", "not valid", ""])
    }
    let choice = ModelChoice(defaultModel: defaultModel, ollamaModel: "qwen3:8b")
    let (names, current) = choice.offer(ollama)
    let context = "seed \(seed): default \(defaultModel), given \(ollama)"
    #expect(names.first == ModelChoice.claude, "\(context)")
    #expect(names.count <= ModelChoice.maxOffered && names.count == min(ModelChoice.maxOffered, valid.count + 1), "\(context)")
    #expect(Set(names).count == names.count, "\(context)")
    #expect(names.allSatisfy(ModelChoice.valid), "\(context)")
    #expect(Int(current) < names.count, "\(context)")
    let offered = defaultModel == ModelChoice.claude || valid.contains(defaultModel)
    if offered {
        #expect(names[Int(current)] == defaultModel, "\(context)")
    } else {
        #expect(current == 0 && !names.contains(defaultModel), "\(context)")
    }
    // The order given is kept: all but a last place taken by the default are a prefix.
    let kept = Array(names.dropFirst())
    let replaced = offered && defaultModel != ModelChoice.claude && (valid.firstIndex(of: defaultModel) ?? 0) >= ModelChoice.maxOffered - 1
    let prefix = replaced ? Array(kept.dropLast()) : kept
    #expect(Array(valid.prefix(prefix.count)) == prefix, "\(context)")
    // Deterministic, and what the protocol carries.
    #expect(choice.offer(ollama) == (names, current) as ([String], UInt32), "\(context)")
    let bytes = try LokiCodec.encode(.models(names: names, current: current), seq: 1)
    #expect(try LokiCodec.decodeBridgeMessage(bytes) == .message(seq: 1, .models(names: names, current: current)), "\(context)")
}

@Test func theDefaultAtTheSixteenthPlaceIsKeptWithoutReplacingAnything() {
    let many = (1...20).map { "m\($0):x" }
    // m15 is the 16th name (after Claude): kept in place, the list as given.
    let atEdge = ModelChoice(defaultModel: "m15:x", ollamaModel: "m15:x").offer(many)
    #expect(atEdge.names == ["Claude"] + (1...15).map { "m\($0):x" } && atEdge.current == 15)
    // m16 would be cut: it takes the last place, m15 goes.
    let past = ModelChoice(defaultModel: "m16:x", ollamaModel: "m16:x").offer(many)
    #expect(past.names.last == "m16:x" && !past.names.contains("m15:x") && past.current == 15)
    // Exactly 15 Ollama models: nothing cut.
    let fifteen = ModelChoice(defaultModel: "m15:x", ollamaModel: "m15:x").offer(Array(many.prefix(15)))
    #expect(fifteen.names.count == 16 && fifteen.current == 15)
    // An Ollama model called Claude is Claude.
    #expect(ModelChoice().offer(["Claude", "a:1", "Claude"]).names == ["Claude", "a:1"])
}

/// Property: after any picks, the default is the last pick (Claude at first), and the
/// Ollama model the last pick that was not Claude (the first model until one is).
@Test(arguments: 0..<100)
func picksSetTheDefaultAndRememberTheLastOllamaModel(_ seed: Int) {
    var random = Seeded(seed)
    var choice = ModelChoice()
    var lastOllama = ModelChoice.firstOllamaModel, last = ModelChoice.claude
    for _ in 0..<random.below(12) {
        let model = random.pick([ModelChoice.claude, "a:1", "b:2", "hf.co/x/y:3", ModelChoice.claude])
        choice.pick(model)
        last = model
        if model != ModelChoice.claude { lastOllama = model }
    }
    #expect(choice.defaultModel == last && choice.ollamaModel == lastOllama, "seed \(seed)")
    #expect(choice.defaultAssistant == (last == ModelChoice.claude ? .claude : .ollama), "seed \(seed)")
}

@Test func theChoiceIsStoredUnderStableKeys() throws {
    let data = try JSONEncoder().encode(ModelChoice(defaultModel: "a:1", ollamaModel: "b:2"))
    let object = try #require(try JSONSerialization.jsonObject(with: data) as? [String: String])
    #expect(object == ["defaultModel": "a:1", "ollamaModel": "b:2"])
}

// MARK: The file

private func folder() -> URL {
    FileManager.default.temporaryDirectory.appendingPathComponent("loki-choice-\(UUID())")
}

private func permissions(_ url: URL) -> Int {
    ((try? FileManager.default.attributesOfItem(atPath: url.path))?[.posixPermissions] as? NSNumber)?.intValue ?? -1
}

private func write(_ text: String, to url: URL) throws {
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data(text.utf8).write(to: url)
}

@Test func savingIntoMissingFoldersMakesThemPrivate() throws {
    let root = folder()
    let file = root.appendingPathComponent("a/b/models.json")
    let store = FileModelChoiceStore(file: file)
    try store.save(ModelChoice(defaultModel: "a:1", ollamaModel: "a:1"))
    #expect(store.load() == ModelChoice(defaultModel: "a:1", ollamaModel: "a:1"))
    #expect(permissions(root) == 0o700 && permissions(root.appendingPathComponent("a")) == 0o700)
    #expect(permissions(root.appendingPathComponent("a/b")) == 0o700 && permissions(file) == 0o600)
    #expect(try FileManager.default.contentsOfDirectory(atPath: file.deletingLastPathComponent().path) == ["models.json"])
}

@Test func whatIsReadIsCheckedAndAnythingWrongIsTheFirstChoice() throws {
    let file = folder().appendingPathComponent("models.json")
    let store = FileModelChoiceStore(file: file)
    func loaded(_ text: String) throws -> ModelChoice {
        try write(text, to: file)
        return store.load()
    }
    let good = ModelChoice(defaultModel: "Claude", ollamaModel: "gemma4:12b")
    #expect(try loaded(#"{"defaultModel":"Claude","ollamaModel":"gemma4:12b"}"#) == good)
    #expect(try loaded(#"{"ollamaModel":"gemma4:12b","defaultModel":"Claude","extra":[1,2]}"#) == good)
    for bad in [#"{"defaultModel":"Claude"}"#, #"{"defaultModel":"x:1","ollamaModel":"x y"}"#, #"{"defaultModel":"","ollamaModel":"x"}"#,
                #"{"defaultModel":1,"ollamaModel":"x"}"#, #"["Claude","x"]"#, "", "null",
                #"{"defaultModel":"Claude","ollamaModel":"\#(String(repeating: "z", count: 41))"}"#] {
        #expect(try loaded(bad) == ModelChoice(), "\(bad)")
    }
    // At most 4096 bytes are read: a valid file padded to the limit loads, one byte more does not.
    let body = #"{"defaultModel":"a:1","ollamaModel":"a:1"}"#
    let padded = body + String(repeating: " ", count: 4096 - body.utf8.count)
    #expect(try loaded(padded) == ModelChoice(defaultModel: "a:1", ollamaModel: "a:1"))
    #expect(try loaded(padded + " ") == ModelChoice())
}

/// Many saves at once (the conductor saves on its actor, but nothing stops two
/// bridges): every save lands whole, each load sees a complete choice, and no
/// temporary file is left.
@Test func concurrentSavesAndLoadsNeverSeeAPartialFile() async throws {
    let file = folder().appendingPathComponent("models.json")
    let store = FileModelChoiceStore(file: file)
    let saved = (0..<48).map { ModelChoice(defaultModel: "m\($0):x", ollamaModel: "m\($0):x") }
    let failures = await withTaskGroup(of: Int.self) { group in
        for choice in saved {
            group.addTask { (try? store.save(choice)) == nil ? 1 : 0 }
            group.addTask { saved.contains(store.load()) || store.load() == ModelChoice() ? 0 : 1 }
        }
        return await group.reduce(0, +)
    }
    #expect(failures == 0)
    #expect(saved.contains(store.load()))
    #expect(try FileManager.default.contentsOfDirectory(atPath: file.deletingLastPathComponent().path) == ["models.json"])
    #expect(permissions(file) == 0o600)
}

@Test func aFileThatExistedReadableByOthersIsPrivateOnceSaved() throws {
    let file = folder().appendingPathComponent("models.json")
    try write("{}", to: file)
    try FileManager.default.setAttributes([.posixPermissions: 0o644], ofItemAtPath: file.path)
    try FileModelChoiceStore(file: file).save(ModelChoice())
    #expect(FileModelChoiceStore(file: file).load() == ModelChoice())
    // The new file's own metadata is kept, not the old file's mode.
    #expect(permissions(file) == 0o600)
    
}

/// A symbolic link where the file should be: read through, never written through.
@Test func aSymlinkAtThePathIsReadButNeverWrittenThrough() throws {
    let root = folder()
    let target = root.appendingPathComponent("elsewhere.json"), link = root.appendingPathComponent("models.json")
    try write(#"{"defaultModel":"b:1","ollamaModel":"b:1"}"#, to: target)
    try FileManager.default.createSymbolicLink(at: link, withDestinationURL: target)
    let store = FileModelChoiceStore(file: link)
    #expect(store.load() == ModelChoice(defaultModel: "b:1", ollamaModel: "b:1"))
    #expect(throws: (any Error).self) { try store.save(ModelChoice(defaultModel: "c:1", ollamaModel: "c:1")) }
    #expect(try String(contentsOf: target, encoding: .utf8) == #"{"defaultModel":"b:1","ollamaModel":"b:1"}"#)
    #expect(try FileManager.default.destinationOfSymbolicLink(atPath: link.path) == target.path)
    // The save that failed leaves no temporary file behind.
    let left = try FileManager.default.contentsOfDirectory(atPath: root.path).sorted()
    #expect(left == ["elsewhere.json", "models.json"])
    
}

@Test func aFolderThatIsAFileFailsTheSave() throws {
    let root = folder()
    try write("x", to: root.appendingPathComponent("blocker"))
    let store = FileModelChoiceStore(file: root.appendingPathComponent("blocker/models.json"))
    #expect(throws: (any Error).self) { try store.save(ModelChoice()) }
    #expect(store.load() == ModelChoice())
    #expect(try String(contentsOf: root.appendingPathComponent("blocker"), encoding: .utf8) == "x")
}

@Test func theMemoryStoreKeepsTheLastSave() throws {
    let store = MemoryModelChoiceStore(ModelChoice(defaultModel: "a:1", ollamaModel: "a:1"))
    #expect(store.load().defaultModel == "a:1")
    try store.save(ModelChoice())
    #expect(store.load() == ModelChoice())
}
