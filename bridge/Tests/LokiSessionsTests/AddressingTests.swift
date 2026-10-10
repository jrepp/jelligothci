import Foundation
import Testing
@testable import LokiSessions

// "Ask Claude to …", "ask Ollama …" (ADR-018) over what recognizers make of it: seeded
// properties, so a failure replays from the seed in its message, and tables of the
// spellings a recognizer gives.

private let resolver = ProjectResolver(roots: [projectRoot(["codergotchi", "chatter"])])
private func kind(_ text: String) -> Request { Requests.classify(text, resolver: resolver) }

/// The pair as comparable values.
private func addressed(_ text: String) -> (Assistant, String)? { Requests.addressed(text) }
private func same(_ a: (Assistant, String)?, _ b: (Assistant, String)?) -> Bool {
    switch (a, b) {
    case (nil, nil): true
    case let (x?, y?): x.0 == y.0 && x.1 == y.1
    default: false
    }
}

private let names = ["Claude", "claude", "CLOUD", "clawed", "Claud", "Ollama", "ollama", "o-llama", "oh llama", "O llama", "a llama",
                     "Lama", "llama", "Olama", "Ollamma", "the local model", "local model", "the local", "local", "Obama"]
private let separators = [" ", ", ", "! ", "... ", "\n", " — ", ": ", "\t", "?? "]
private let restWords = ["what", "is", "the", "weather", "to", "stop", "tell", "me", "about", "chatter", "run", "tests", "Claude",
                         "ask", "ollama", "it's", "café", "😀", "中文", "42", "e\u{301}", "don\u{2019}t", "?", ".", ","]

/// A phrase from random words and separators.
private func phrase(_ count: Int, _ random: inout Seeded) -> String {
    var text = ""
    for index in 0..<count {
        if index > 0 { text += random.pick(separators) }
        text += random.pick(restWords)
    }
    return text
}

/// An "ask …" request: some addressing words, ask, a name, maybe "to", and a rest.
private func request(_ random: inout Seeded) -> String {
    var text = ""
    for _ in 0..<random.below(3) { text += random.pick(Requests.addressing.sorted()) + random.pick(separators) }
    text += random.pick(["ask", "Ask", "ASK"]) + random.pick(separators) + random.pick(names)
    if random.chance(50) { text += random.pick(separators) + random.pick(["to", "To"]) }
    if random.chance(85) { text += random.pick(separators) + phrase(1 + random.below(8), &random) }
    if random.chance(20) { text += random.pick([" ", "  \n", "\t"]) }
    return text
}

// MARK: Properties

/// Property: any text gives the same answer twice; an answer's rest is a suffix of the
/// text as said (less trailing whitespace), with no whitespace at either end; and it
/// only ever fires when the first word after the addressing words is "ask".
@Test(arguments: 0..<300)
func addressingIsDeterministicKeepsTheRestAsSaidAndNeedsALeadingAsk(_ seed: Int) {
    var random = Seeded(seed)
    let alphabet: [Character] = Array("abcdefghijklmnopqrstuvwxyz ASKCLOUDE .,?!'\u{2019}-_/\\\"\n\t") + ["\u{0000}", "\u{202E}", "\u{200B}",
        "é", "ß", "ﬁ", "😀", "👩‍💻", "e\u{301}", "中", "ع", "\u{FEFF}", "\u{00A0}", "\r\n"]
    let text = random.chance(60) ? request(&random) : String((0..<random.below(80)).map { _ in random.pick(alphabet) })
    let first = addressed(text)
    #expect(same(first, addressed(text)), "seed \(seed): \(text.debugDescription)")
    let leading = Requests.tokens(text).map(\.word).first { !Requests.addressing.contains($0) }
    guard let (_, rest) = first else { return }
    #expect(leading == "ask", "seed \(seed): \(text.debugDescription)")
    let said = text.replacingOccurrences(of: #"\s+$"#, with: "", options: .regularExpression)
    #expect(said.hasSuffix(rest), "seed \(seed): \(text.debugDescription) gave \(rest.debugDescription)")
    #expect(rest == rest.trimmingCharacters(in: .whitespacesAndNewlines), "seed \(seed)")
    // classify agrees.
    if case .ask(let who, let words) = kind(text) {
        #expect(who == first?.0 && words == rest, "seed \(seed)")
    } else {
        Issue.record("seed \(seed): classify did not see \(text.debugDescription) as asking")
    }
}

/// Metamorphic: addressing words before "ask" never change who is asked or the rest.
@Test(arguments: 0..<200)
func addressingWordsBeforeAskChangeNothing(_ seed: Int) {
    var random = Seeded(seed)
    let text = random.chance(80) ? request(&random) : phrase(1 + random.below(10), &random)
    var prefix = ""
    for _ in 0..<(1 + random.below(4)) { prefix += random.pick(Requests.addressing.sorted()) + random.pick(separators) }
    if random.chance(30) { prefix = random.pick(["Hey", "OK", "Loki"]) + random.pick([",", "!", ":"]) + " " + prefix }
    #expect(same(addressed(text), addressed(prefix + text)), "seed \(seed): \((prefix + text).debugDescription)")
}

/// Metamorphic: case does not matter (ASCII text, so the rest maps letter for letter),
/// nor does punctuation between ask and the name, nor "to" after the name.
@Test(arguments: 0..<150)
func caseAndPunctuationAroundTheNameChangeNothing(_ seed: Int) {
    var random = Seeded(seed)
    let name = random.pick(["Claude", "cloud", "Ollama", "oh llama", "Lama", "the local model", "local"])
    // An ASCII rest that starts with a word other than "to".
    let rest = phrase(1 + random.below(6), &random).filter(\.isASCII).trimmingCharacters(in: .whitespacesAndNewlines)
    guard let lead = rest.first, lead.isLetter || lead.isNumber, Requests.tokens(rest).first?.word != "to" else { return }
    let plain = "ask \(name) \(rest)"
    guard let base = addressed(plain) else {
        Issue.record("seed \(seed): \(plain) asked no one")
        return
    }
    let loud = addressed(plain.uppercased())
    #expect(loud?.0 == base.0 && loud?.1 == base.1.uppercased(), "seed \(seed): \(plain)")
    let punctuated = addressed("ask\(random.pick([",", ":", " -", "..."])) \(name)\(random.pick([",", ":", " -", ""])) \(rest)")
    #expect(same(punctuated, base), "seed \(seed): \(plain)")
    #expect(same(addressed("ask \(name) to \(rest)"), base), "seed \(seed): \(plain)")
}

/// Property: a request whose first word (after addressing words) is not "ask" never asks
/// an assistant, whatever names follow.
@Test(arguments: 0..<200)
func withoutALeadingAskNoAssistantIsAsked(_ seed: Int) {
    var random = Seeded(seed)
    let lead = random.pick(["task", "asked", "asking", "asks", "can you ask", "please tell", "Claude", "Ollama", "mask", "flask",
                            "then ask", "I'll ask", "don't ask", "ask'd", "a sk", "as k"])
    let text = lead + random.pick(separators) + random.pick(names) + " " + phrase(1 + random.below(6), &random)
    #expect(addressed(text) == nil, "seed \(seed): \(text.debugDescription)")
    if case .ask = kind(text) { Issue.record("seed \(seed): \(text.debugDescription) asked an assistant") }
}

// MARK: Tables

@Test(arguments: [
    // What recognizers wrote for "Claude"
    ("Ask Claude to check the build", Assistant.claude, "check the build"),
    ("ask claud to check the build", .claude, "check the build"),
    ("Ask Clawed what's running", .claude, "what's running"),
    ("ask Cloud, what's the status?", .claude, "what's the status?"),
    ("ask 'Claude' to x", .claude, "x"),
    ("Ask Claude... to stop", .claude, "stop"),
    ("ask claude to to the moon", .claude, "to the moon"),
    // ... and for "Ollama"
    ("Ask Olama about whales", .ollama, "about whales"),
    ("Ask Ollamma about whales", .ollama, "about whales"),
    ("Ask O'Llama about whales", .ollama, "about whales"),
    ("Ask Oh, llama, about whales", .ollama, "about whales"),
    ("ask uh llama about whales", .ollama, "about whales"),
    ("ask llamas about whales", .ollama, "about whales"),
    ("ASK OLLAMA ABOUT WHALES", .ollama, "ABOUT WHALES"),
    ("ask Ollama\u{2019}s view on whales", .ollama, "view on whales"),
    ("ask ollama\u{2014}what about whales", .ollama, "what about whales"),
    ("ask llama\u{301} what about whales", .ollama, "what about whales"),
    ("ask local model to summarize", .ollama, "summarize"),
    // Separators a recognizer or a keyboard may give
    ("ask\u{00A0}Claude to x", .claude, "x"),
    ("ask\u{200B}Claude to x", .claude, "x"),
    ("ask\nClaude\nto\nx", .claude, "x"),
    ("Loki: ask Claude to x", .claude, "x"),
    ("um, okay, so, ask Claude to x", .claude, "x"),
    ("ask Claude to x\u{0000}y", .claude, "x\u{0000}y"),
    ("ask Claude to\u{0000}x", .claude, "x"),
    ("ask Claude 2 questions", .claude, "2 questions"),
])
func recognizerSpellingsNameTheAssistant(_ text: String, _ who: Assistant, _ rest: String) {
    #expect(kind(text) == .ask(who, rest), "\(text.debugDescription)")
}

@Test(arguments: [
    "ask Claudia to sing", "ask clouds to part", "ask Clyde", "ask Google for directions", "ask Siri to call", "ask Alexa",
    "ask my mom", "ask them to wait", "ask whether it rains", "ask for the local news", "ask about llamas", "asked Claude to",
    "ask the local to summarize", "ask the local library when it opens", "ask mama", "ask Lima", "ask drama class",
    "asking Claude for help", "ＡＳＫ Claude to x", "ask ask Claude to x", "ask to Claude x", "ask Claude's opinion",
    "ask Lambda about it", "ask Allan", "ask Alan", "ask Emma", "ask the team", "ask a friend", "ask nobody",
    "ask 'em", "ask me", "ask", "ask ", "ask?", "ask, please", "Ask Lola", "ask Colin to call",
])
func otherWordsAfterAskNameNoAssistant(_ text: String) {
    #expect(Requests.addressed(text) == nil, "\(text.debugDescription)")
}

/// Fillers between "ask" and the name are passed over, for either assistant.
@Test func aFillerBetweenAskAndTheNameIsSkippedForEitherAssistant() {
    #expect(kind("ask uh llama what time it is") == .ask(.ollama, "what time it is"))
    #expect(kind("ask, um, Claude to fix it") == .ask(.claude, "fix it"))
    #expect(kind("ask uh the local model what time it is") == .ask(.ollama, "what time it is"))
    
}

// MARK: Tokens

@Test func tokensAreLowercaseWordsWithoutApostrophesAndTheirPlaceInTheText() {
    let text = "Hey—Loki, what's O'Brien's 2nd café\u{2019}s e\u{301}tat?"
    let tokens = Requests.tokens(text)
    #expect(tokens.map(\.word) == ["hey", "loki", "whats", "obriens", "2nd", "cafés", "e\u{301}tat"])
    #expect(tokens.map { String(text[$0.range]) } == ["Hey", "Loki", "what's", "O'Brien's", "2nd", "café\u{2019}s", "e\u{301}tat"])
    #expect(Requests.tokens("").isEmpty && Requests.tokens(" ,.!? \n").isEmpty)
    #expect(Requests.tokens("'").map(\.word) == [""])  // an apostrophe alone is an empty word
    #expect(Requests.tokens("x").map(\.word) == ["x"])
}

/// Property: tokens are disjoint, in order, cover only letters, digits and apostrophes,
/// and every letter or digit of the text is in one.
@Test(arguments: 0..<150)
func tokensCoverEveryLetterOnceInOrder(_ seed: Int) {
    var random = Seeded(seed)
    let alphabet: [Character] = Array("ab Z09'\u{2019} .,-\n") + ["é", "e\u{301}", "😀", "中", "\u{200B}", "ß", "\r\n"]
    let text = String((0..<random.below(60)).map { _ in random.pick(alphabet) })
    let tokens = Requests.tokens(text)
    var covered = text.startIndex
    for token in tokens {
        #expect(token.range.lowerBound >= covered, "seed \(seed)")
        covered = token.range.upperBound
        #expect(text[token.range].allSatisfy { $0.isLetter || $0.isNumber || "'\u{2019}".contains($0) }, "seed \(seed)")
    }
    let inside = tokens.reduce(0) { $0 + text[$1.range].filter { $0.isLetter || $0.isNumber }.count }
    #expect(inside == text.filter { $0.isLetter || $0.isNumber }.count, "seed \(seed): \(text.debugDescription)")
}

@Test func theAssistantIsNamedByItsFirstWordsAndHowManyItTook() {
    func named(_ words: [String]) -> String {
        Requests.assistant(named: words).map { "\($0.0) \($0.1)" } ?? "none"
    }
    #expect(named([]) == "none")
    #expect(named(["claude"]) == "claude 1" && named(["cloud", "llama"]) == "claude 1")
    #expect(named(["the", "local", "model", "x"]) == "ollama 3" && named(["the", "local"]) == "none")
    #expect(named(["local", "model"]) == "ollama 2" && named(["local", "models"]) == "ollama 1")
    #expect(named(["oh", "llama"]) == "ollama 2" && named(["o", "lama", "x"]) == "ollama 2")
    #expect(named(["ollama", "llama"]) == "ollama 1")
    // A short first word only joins the next when together they are close; alone it is too short.
    #expect(named(["oh"]) == "none" && named(["the"]) == "none" && named(["olla"]) == "none" && named(["lam"]) == "none")
    #expect(named(["llama", "to"]) == "ollama 1")
    #expect(named(["", "llama"]) == "ollama 2")  // an empty word (an apostrophe alone) joins too
}
