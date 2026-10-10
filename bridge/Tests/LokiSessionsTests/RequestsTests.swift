import Foundation
import Testing
@testable import LokiSessions

// Requests over phrasing variants and recognizer spellings: seeded, so a failure
// replays from the seed in its message. A stop must never swallow work, status and
// repeat carry the words as said, and anything else goes to Claude word for word.

/// A small seeded generator (xorshift64*), so every case replays from its seed.
struct Seeded: RandomNumberGenerator {
    private var state: UInt64
    init(_ seed: Int) { state = UInt64(bitPattern: Int64(seed)) &* 0x9E37_79B9_7F4A_7C15 | 1 }
    mutating func next() -> UInt64 {
        state ^= state >> 12
        state ^= state << 25
        state ^= state >> 27
        return state &* 0x2545_F491_4F6C_DD1D
    }
    mutating func below(_ bound: Int) -> Int { Int(next() % UInt64(bound)) }
    mutating func chance(_ percent: Int) -> Bool { below(100) < percent }
    mutating func pick<T>(_ items: [T]) -> T { items[below(items.count)] }
}

/// A projects folder with these folders, under the temp folder.
func projectRoot(_ names: [String]) -> URL {
    let root = FileManager.default.temporaryDirectory.appendingPathComponent("loki-requests-\(UUID())")
    for name in names {
        try! FileManager.default.createDirectory(at: root.appendingPathComponent(name), withIntermediateDirectories: true)
    }
    return root
}

private let requestsRoot = projectRoot(["codergotchi", "chatter", "atrium"])
private let resolver = ProjectResolver(roots: [requestsRoot])
private func project(_ name: String) -> Project {
    Project(name: name, url: requestsRoot.appendingPathComponent(name).standardizedFileURL)
}
private func kind(_ text: String) -> Request { Requests.classify(text, resolver: resolver) }

/// How recognizers spell each project; every one must find it.
private let spellings = [
    "codergotchi": ["codergotchi", "Coder Gotchi", "coder-gotchi", "CoderGotchi", "codergotchy", "Coder gotcha"],
    "chatter": ["chatter", "Chatter", "CHATTER", "chater"],
    "atrium": ["atrium", "Atrium", "atrium's"],
]

/// Words a stop may carry; none of them is a project or near one.
private let fillers = Array(Requests.stopFillers).sorted()
/// Ordinary words of developer requests: no stop word, filler or project near miss.
private let vocabulary = ["server", "migration", "rebase", "deploy", "tests", "build", "watcher", "daemon",
                          "timer", "flaky", "pipeline", "container", "branch", "release", "logging", "cache",
                          "linter", "warnings", "readme", "benchmark", "profile", "bluetooth", "firmware"]

/// The phrase dressed the way a recognizer or a person might: case, punctuation,
/// curly apostrophes and extra spaces.
private func dressed(_ words: [String], _ random: inout Seeded) -> String {
    var text = words.map { word -> String in
        let word = ["whats": "what's", "hows": "how's", "nevermind": "never-mind"][word].flatMap { random.chance(60) ? $0 : nil }
            ?? word
        return switch random.below(4) {
        case 0: word.uppercased()
        case 1: word.prefix(1).uppercased() + word.dropFirst()
        default: word
        }
    }.joined(separator: random.chance(20) ? "  " : " ")
    if random.chance(30) { text = text.replacingOccurrences(of: "'", with: "\u{2019}") }
    if random.chance(20) { text = text.replacingOccurrences(of: " ", with: ", ", options: [], range: text.range(of: " ")) }
    return text + random.pick(["", ".", "?", "!", "...", " "])
}

// MARK: Stops

@Test(arguments: 0..<40)
func aShortStopOfFillersStopsEverything(_ seed: Int) {
    var random = Seeded(seed)
    let lead = random.pick([["stop"], ["cancel"], ["halt"], ["abort"], ["never", "mind"], ["nevermind"]])
    let words = lead + (0..<random.below(5)).map { _ in random.pick(fillers) }
    let text = dressed(words, &random)
    #expect(kind(text) == .stop(nil), "seed \(seed): \(text)")
}

@Test(arguments: 0..<60)
func aStopNamingAProjectInAnySpellingStopsThatProject(_ seed: Int) {
    var random = Seeded(seed)
    let name = random.pick(["codergotchi", "chatter", "atrium"])
    let spoken = random.pick(spellings[name]!).split(separator: " ").map(String.init)
    let before = (0..<random.below(3)).map { _ in random.pick(fillers) }
    let after = (0..<random.below(2)).map { _ in random.pick(fillers) }
    let words = [random.pick(["stop", "cancel", "halt", "abort"])] + before + spoken + after
    let text = dressed(words, &random)
    #expect(kind(text) == .stop(project(name)), "seed \(seed): \(text)")
}

/// Property: a request led by a stop word with four or more other words is work,
/// however long, and goes to Claude exactly as said.
@Test(arguments: 0..<60)
func aStopWordNeverSwallowsALongerRequest(_ seed: Int) {
    var random = Seeded(seed)
    var words = [random.pick(["stop", "cancel", "halt", "abort", "never mind", "nevermind"])]
    let content = 4 + random.below(20)
    for _ in 0..<content {
        if random.chance(30) { words.append(random.pick(fillers)) }
        words.append(random.pick(vocabulary))
    }
    if random.chance(30) { words.insert(random.pick(["in codergotchi", "on chatter", "for atrium"]), at: 1 + random.below(words.count)) }
    let text = dressed(words, &random)
    #expect(kind(text) == .work(text), "seed \(seed): \(text)")
}

@Test func stopsThatNameNoKnownProjectAreWorkNotAStopOfEverything() {
    // "Kotter Gotchi" is three edits from codergotchi: no project, so it is work for
    // Claude rather than a stop of everything or of the wrong project.
    for text in ["stop Kotter Gotchi", "cancel the server", "abort the rebase", "halt the timer please",
                 "stop the dev server in chatter", "cancel my meeting with Bob"] {
        #expect(kind(text) == .work(text), "\(text)")
    }
    #expect(kind("stop the work in chatter now please") == .stop(project("chatter")))
    #expect(kind("Never mind that.") == .stop(nil))
    #expect(kind("never") == .work("never"))
    #expect(kind("mind the cache") == .work("mind the cache"))
}

// MARK: Status and repeat

@Test(arguments: 0..<40)
func everyStatusPhraseAsksForStatusWithTheWordsAsSaid(_ seed: Int) {
    var random = Seeded(seed)
    let phrase = random.pick(Requests.statusPhrases.sorted()).split(separator: " ").map(String.init)
    let words = (random.chance(30) ? ["loki"] : []) + phrase
    let text = dressed(words, &random)
    #expect(kind(text) == .status(nil, text), "seed \(seed): \(text)")
}

@Test(arguments: 0..<60)
func statusOfAProjectFindsItInAnySpelling(_ seed: Int) {
    var random = Seeded(seed)
    let name = random.pick(["codergotchi", "chatter", "atrium"])
    let spoken = random.pick(spellings[name]!.filter { !$0.contains("'") })
    let template = random.pick(["how's X going", "how is X doing", "what's X doing", "status of X",
                                "how is X going", "what is X doing", "what's the status of X"])
    let words = template.replacingOccurrences(of: "X", with: spoken).split(separator: " ").map(String.init)
    let text = dressed(words, &random)
    #expect(kind(text) == .status(project(name), text), "seed \(seed): \(text)")
}

@Test(arguments: 0..<30)
func repeatPhrasesRepeatOfEverythingOrOfTheProjectNamed(_ seed: Int) {
    var random = Seeded(seed)
    if random.chance(50) {
        let text = dressed(random.pick(Requests.repeatPhrases.sorted()).split(separator: " ").map(String.init), &random)
        #expect(kind(text) == .repeatLast(nil), "seed \(seed): \(text)")
    } else {
        let name = random.pick(["codergotchi", "chatter", "atrium"])
        let spoken = random.pick(spellings[name]!.filter { !$0.contains("'") })
        let text = dressed(["what", "did"] + spoken.split(separator: " ").map(String.init) + ["say"], &random)
        #expect(kind(text) == .repeatLast(project(name)), "seed \(seed): \(text)")
    }
}

/// Property: more than eight words, not led by a stop, is always work, even when it
/// holds every status and repeat phrase.
@Test(arguments: 0..<40)
func longRequestsAreAlwaysWork(_ seed: Int) {
    var random = Seeded(seed)
    let pool = vocabulary + ["status", "repeat", "whats", "running", "say", "that", "again", "codergotchi", "how", "is"]
    let words = (0..<(9 + random.below(30))).map { _ in random.pick(pool) }
    let text = dressed(words, &random)
    #expect(kind(text) == .work(text), "seed \(seed): \(text)")
}

// MARK: Fuzz and metamorphic relations

private let alphabet: [Character] = Array("abcdefghijklmnopqrstuvwxyz ABCXYZ0123456789 .,?!'\u{2019}-_/\\\"\n\t")
    + ["\u{0000}", "\u{202E}", "\u{200B}", "é", "ß", "ﬁ", "😀", "👩‍💻", "e\u{301}", "中", "ع", "\u{FEFF}"]

/// Property: any text classifies without trapping, the same way twice, and work and
/// status carry it unchanged.
@Test(arguments: 0..<200)
func anyTextClassifiesDeterministicallyAndKeepsItsWords(_ seed: Int) {
    var random = Seeded(seed)
    var text = String((0..<random.below(120)).map { _ in random.pick(alphabet) })
    if random.chance(30) { text = random.pick(["stop ", "how is ", "what did ", "never mind "]) + text }
    let first = kind(text)
    #expect(first == kind(text), "seed \(seed)")
    switch first {
    case .work(let words): #expect(words == text, "seed \(seed)")
    case .status(_, let words): #expect(words == text, "seed \(seed)")
    case .stop, .repeatLast: break
    }
}

/// The shape of a request, without the words it carries.
private func shape(_ request: Request) -> String {
    switch request {
    case .stop(let project): "stop \(project?.name ?? "-")"
    case .status(let project, _): "status \(project?.name ?? "-")"
    case .repeatLast(let project): "repeat \(project?.name ?? "-")"
    case .work: "work"
    }
}

/// A repeated or politely led stop is still a stop.
@Test(arguments: ["stop stop", "Stop, stop!", "cancel cancel", "please stop", "okay, stop", "Loki, stop"])
func commonWaysOfSayingStopStop(_ text: String) {
    #expect(kind(text) == .stop(nil))
}

/// A longer name that starts with a project's ("the chatter server") is something in
/// that project to stop, which is work for Claude, not a stop of the project's job.
@Test(arguments: ["stop the chatter server", "stop the codergotchi watcher", "cancel chatter's build"])
func stoppingSomethingInAProjectIsWork(_ text: String) {
    #expect(kind(text) == .work(text))
}

/// Metamorphic: case, punctuation, curly apostrophes and spacing never change what a
/// request is.
@Test(arguments: 0..<80)
func dressingAPhraseNeverChangesWhatItIs(_ seed: Int) {
    var random = Seeded(seed)
    let phrases = ["stop", "stop chatter", "cancel everything", "never mind", "what's running", "how's codergotchi going",
                   "status of atrium", "say that again", "what did chatter say", "in codergotchi run the tests",
                   "now commit it", "what's the latest Swift release", "abort the rebase in chatter"]
    let plain = random.pick(phrases)
    let words = plain.split(separator: " ").map(String.init)
    #expect(shape(kind(plain)) == shape(kind(dressed(words, &random))), "seed \(seed): \(plain)")
}

// MARK: Status and repeat are whole requests

/// Short work that merely mentions a status or repeat word goes to Claude.
@Test(arguments: ["Show me the git status", "Fix the status bar colour", "Why does the build fail repeatedly",
                  "Explain HTTP status codes", "repeat the last migration", "subscribe me to any newsletter"])
func shortWorkThatMentionsAStatusOrRepeatWordGoesToClaude(_ text: String) {
    #expect(kind(text) == .work(text))
}
