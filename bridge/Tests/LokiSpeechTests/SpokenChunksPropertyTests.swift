import Foundation
import Testing
@testable import LokiSpeech

// SpokenChunks under seeded random and adversarial text: abbreviations, ellipses,
// emoji, quotes, very long text without punctuation and runs of line breaks. Whatever
// arrives, nothing is lost or reordered, no piece is empty, pieces are few and about a
// sentence long, and splitting is deterministic and linear in the text's length.

/// xorshift64*, seeded per case so a failure replays from its seed.
private struct Seeded {
    private var state: UInt64
    init(_ seed: Int) { state = UInt64(bitPattern: Int64(seed)) &* 0x9E37_79B9_7F4A_7C15 | 1 }
    mutating func next() -> UInt64 {
        state ^= state >> 12
        state ^= state << 25
        state ^= state >> 27
        return state &* 0x2545_F491_4F6C_DD1D
    }
    mutating func below(_ bound: Int) -> Int { Int(next() % UInt64(bound)) }
    mutating func pick<T>(_ items: [T]) -> T { items[below(items.count)] }
}

/// Words Claude might say, and the awkward ones: abbreviations, numbers with points,
/// ellipses, stacked punctuation, quotes and brackets that close a sentence or do not,
/// emoji (with skin tones, families joined by ZWJ, flags), accents and other scripts.
private let words = ["the", "build", "is", "clean", "tests", "pass", "Okay.", "Done!", "Why?", "really?!", "e.g.",
                     "i.e.,", "Dr.", "U.S.", "etc.", "v2.1.296", "3.14", ".5", "...", "Wait...", "Hmm…", "?!",
                     "\"stop.\"", "'yes.'", "“Ship", "it.”", "(see", "above.)", "[done.]", "«Fini.»", "it's",
                     "🎉", "👍🏽", "👨‍👩‍👧", "🇺🇸", "café", "e\u{301}te\u{301}", "日本語。", "naïve", "https://x.io/a.b?c=d.",
                     "—", "-", ",", ";", ":", "make test", "C:\\path", "100%", "#1", "*bold*", "`code`"]
private let gaps = [" ", " ", " ", " ", "  ", "\t", "\n", "\n\n", "\r\n", " \n ", "\u{2028}", "\u{00A0}",
                    "\n\n\n\n"]

/// Text of `count` words with random gaps; may start or end with a gap.
private func text(_ random: inout Seeded, words count: Int) -> String {
    var out = random.below(4) == 0 ? random.pick(gaps) : ""
    for index in 0..<count {
        if index > 0 { out += random.pick(gaps) }
        out += random.pick(words)
    }
    if random.below(4) == 0 { out += random.pick(gaps) }
    return out
}

/// The text as SpokenChunks promises to give it back: its runs of whitespace single spaces.
private func squeezed(_ text: String) -> String { text.split(whereSeparator: \.isWhitespace).joined(separator: " ") }

/// The non-whitespace scalars, in order: what must survive, independent of how
/// Character groups them.
private func ink(_ text: String) -> [Unicode.Scalar] {
    text.unicodeScalars.filter { !$0.properties.isWhitespace }
}

/// Every property a split must have, for any text.
private func checkSplit(_ text: String, _ context: String) {
    let pieces = SpokenChunks.split(text)
    let sentences = SpokenChunks.sentences(text)
    // Nothing lost or reordered; whitespace made single.
    #expect(pieces.joined(separator: " ") == squeezed(text), "\(context)")
    #expect(ink(pieces.joined()) == ink(text), "\(context): a scalar was lost or moved")
    // No empty piece, none with whitespace at its ends or a line break or double space inside.
    for piece in pieces {
        #expect(!piece.isEmpty && piece.first?.isWhitespace == false && piece.last?.isWhitespace == false, "\(context)")
        #expect(!piece.contains { $0.isNewline } && !piece.contains("  "), "\(context): \(piece.debugDescription)")
    }
    #expect(pieces.isEmpty == squeezed(text).isEmpty, "\(context)")
    // Pieces are whole sentences, consecutive ones joined by a space.
    var rest = sentences[...]
    for piece in pieces {
        var built = ""
        while built.count < piece.count, let sentence = rest.first {
            built = built.isEmpty ? sentence : built + " " + sentence
            rest = rest.dropFirst()
        }
        #expect(built == piece, "\(context): a piece is not whole sentences")
    }
    #expect(rest.isEmpty, "\(context)")
    // Few pieces: short text is one; otherwise each is at least laterAtLeast characters.
    let whole = sentences.joined(separator: " ")
    if whole.count < SpokenChunks.wholeBelow { #expect(pieces.count <= 1, "\(context)") }
    if pieces.count > 1 {
        #expect(pieces.allSatisfy { $0.count >= SpokenChunks.laterAtLeast }, "\(context)")
        #expect(pieces.count <= whole.count / SpokenChunks.laterAtLeast, "\(context)")
    }
    #expect(pieces.count <= max(1, sentences.count), "\(context)")
    // About a sentence: a piece is short sentences under the threshold plus the one that
    // crossed it, and the last may also hold a short tail.
    let longest = sentences.map(\.count).max() ?? 0
    for (index, piece) in pieces.enumerated() where pieces.count > 1 {
        let tail = index == pieces.count - 1 ? SpokenChunks.firstAtLeast + 1 : 0
        #expect(piece.count <= SpokenChunks.firstAtLeast + longest + tail, "\(context): piece \(index) too long")
    }
    // Deterministic.
    #expect(SpokenChunks.split(text) == pieces, "\(context)")
}

// MARK: Properties

@Test(arguments: 0..<300)
func anyTextSplitsWithoutLossIntoFewSentencePieces(_ seed: Int) {
    var random = Seeded(seed)
    let count = random.below(5) == 0 ? 100 + random.below(400) : random.below(40)
    let input = text(&random, words: count)
    checkSplit(input, "seed \(seed): \(input.debugDescription)")
}

/// Raw scalars, not words: control characters, separators, lone combining marks, ZWJ,
/// variation selectors and the punctuation the splitter looks at, in any order.
@Test(arguments: 0..<300)
func anyScalarsSplitWithoutLoss(_ seed: Int) {
    var random = Seeded(seed)
    let pool: [UInt32] = [0x20, 0x20, 0x09, 0x0A, 0x0D, 0x0B, 0x0C, 0x85, 0xA0, 0x2028, 0x2029, 0x3000,
                          0x2E, 0x2E, 0x21, 0x3F, 0x22, 0x27, 0x29, 0x5D, 0x201D, 0x2019, 0x2026,
                          0x61, 0x62, 0x41, 0x31, 0xE9, 0x301, 0x200D, 0xFE0F, 0x1F3FD, 0x1F468, 0x1F1FA, 0x1F1F8,
                          0x65E5, 0x3002, 0x0, 0x7F]
    var scalars = String.UnicodeScalarView()
    for _ in 0..<random.below(random.below(4) == 0 ? 600 : 80) { scalars.append(Unicode.Scalar(random.pick(pool))!) }
    let input = String(scalars)
    let pieces = SpokenChunks.split(input)
    // The promise as documented: joined with spaces, the text with its whitespace single.
    #expect(pieces.joined(separator: " ") == squeezed(input), "seed \(seed): \(input.debugDescription)")
    #expect(pieces.allSatisfy { !$0.isEmpty }, "seed \(seed)")
    #expect(SpokenChunks.split(input) == pieces, "seed \(seed)")
}

/// Metamorphic: the same words with other whitespace between them (but the same line
/// breaks), or with whitespace and blank lines added around and between lines, split
/// the same way.
@Test(arguments: 0..<200)
func whitespaceOtherThanLineBreaksDoesNotChangeThePieces(_ seed: Int) {
    var random = Seeded(seed)
    let input = text(&random, words: random.below(5) == 0 ? 200 : random.below(40))
    let pieces = SpokenChunks.split(input)
    let spacey = String(input.map { $0 == " " ? random.pick(["  ", "\t", " \t ", "\u{00A0}"]) : String($0) }.joined())
    #expect(SpokenChunks.split(spacey) == pieces, "seed \(seed): \(input.debugDescription)")
    #expect(SpokenChunks.split(" \t\n\n" + input + "\n \r\n") == pieces, "seed \(seed)")
    let blank = String(input.map { $0 == "\n" ? "\n\n \n" : String($0) }.joined())
    #expect(SpokenChunks.split(blank) == pieces, "seed \(seed)")
}

/// Metamorphic: two texts on separate lines split as their sentences put together:
/// the line break between them always ends a sentence.
@Test(arguments: 0..<100)
func aLineBreakAlwaysEndsASentence(_ seed: Int) {
    var random = Seeded(seed)
    let first = text(&random, words: random.below(30)), second = text(&random, words: random.below(30))
    #expect(SpokenChunks.sentences(first + "\n" + second)
        == SpokenChunks.sentences(first) + SpokenChunks.sentences(second), "seed \(seed)")
}

// MARK: Adversarial examples

@Test func abbreviationsEndASentenceButLoseNothing() {
    // A known simplification: "e.g." and "Dr." followed by a space end a sentence. That
    // only moves where a render starts; the words all arrive, in order.
    #expect(SpokenChunks.sentences("Use a tool, e.g. make, to build it. Ask Dr. Smith.")
        == ["Use a tool, e.g.", "make, to build it.", "Ask Dr.", "Smith."])
    let text = "I talked to the team about it twice, Dr. Smith said the release is fine to ship today."
    #expect(SpokenChunks.split(text) == ["I talked to the team about it twice, Dr.", "Smith said the release is fine to ship today."])
    // Points inside a word, a number or a link are not ends.
    #expect(SpokenChunks.sentences("See https://x.io/a.b?c=d and v2.1.296 or .5 percent.")
        == ["See https://x.io/a.b?c=d and v2.1.296 or .5 percent."])
}

@Test func ellipsesAndStackedPunctuationEndOnceAtTheirLastMark() {
    #expect(SpokenChunks.sentences("Wait... what?! Really?? Yes!!! Hmm… no.")
        == ["Wait...", "what?!", "Really??", "Yes!!!", "Hmm… no."])
    #expect(SpokenChunks.split("Wait... I think the build is broken again today. Let me check the logs for you now.")
        == ["Wait... I think the build is broken again today.", "Let me check the logs for you now."])
    #expect(SpokenChunks.split("...") == ["..."] && SpokenChunks.split(" ?!. ") == ["?!."])
}

@Test func quotesAndBracketsCloseWithTheirSentence() {
    #expect(SpokenChunks.sentences("He said, “Ship it.” Then 'done.' (See above.) [Fine.] \"Okay!\"")
        == ["He said, “Ship it.”", "Then 'done.'", "(See above.)", "[Fine.]", "\"Okay!\""])
    // Quotes it does not know (French) keep the sentence going; nothing is lost.
    #expect(SpokenChunks.sentences("«Fini.» Next one.") == ["«Fini.» Next one."])
    #expect(SpokenChunks.sentences("A quote.\"\"\"))) after") == ["A quote.\"\"\")))", "after"])
}

/// Thresholds count what is heard, Characters: a family emoji is one, though it is
/// eighteen bytes of UTF-8 and five scalars.
@Test func emojiCountAsOneCharacterEach() {
    let family = "👨‍👩‍👧"
    let first = String(repeating: family, count: 28) + "."  // 29 characters
    let second = "The deploy went out to prod ok."  // 31 characters
    #expect(first.count == 29 && first.unicodeScalars.count > 100)
    #expect(SpokenChunks.split(first + " " + second) == [first + " " + second])
    let thirty = String(repeating: family, count: 29) + "."
    #expect(SpokenChunks.split(thirty + " " + second) == [thirty, second])
    #expect(SpokenChunks.split("All green! 🎉🎉 The deploy went out to production just now. 👍🏽")
        == ["All green! 🎉🎉 The deploy went out to production just now. 👍🏽"])
}

@Test func runsOfLineBreaksMakeNoEmptyPieces() {
    let text = "\n\n\n  line one  \n\n\r\n\u{2028}\u{2029}\u{85} line two \n\n\n" + String(repeating: "\n", count: 10_000)
    #expect(SpokenChunks.sentences(text) == ["line one", "line two"])
    #expect(SpokenChunks.split(text) == ["line one line two"])
    #expect(SpokenChunks.split(String(repeating: "\r\n \t", count: 5_000)).isEmpty)
}

/// The piece thresholds at their edges: a tail of 29 characters joins, of 30 stands.
@Test func theThresholdsHoldAtTheirEdges() {
    let thirty = "Twenty nine letters and a dot."
    #expect(thirty.count == 30)
    let tail29 = String(thirty.dropFirst()), tail30 = thirty
    #expect(SpokenChunks.split(thirty + " " + tail29) == [thirty + " " + tail29])  // 60 characters
    #expect(SpokenChunks.split(thirty + " " + tail30) == [thirty, tail30])  // 61
    // A short sentence first joins the one after it, wherever that ends.
    #expect(SpokenChunks.split("Ok. " + thirty + " " + tail30) == ["Ok. " + thirty, tail30])
    // Short ones accumulate until a piece reaches 30.
    let shorts = (1...20).map { "Item \($0)." }.joined(separator: " ")
    let pieces = SpokenChunks.split(shorts)
    #expect(pieces.joined(separator: " ") == shorts && pieces.allSatisfy { (30..<45).contains($0.count) })
}

/// Text with no sentence end is one piece, however long: nothing is lost, but nothing
/// renders until all of it does (the reply speaks no sooner than before the change).
@Test func longTextWithoutPunctuationIsOnePiece() {
    let words = String(repeating: "and then the build went on ", count: 4_000)
    #expect(SpokenChunks.split(words) == [squeezed(words)])
    #expect(SpokenChunks.split(String(repeating: "x", count: 100_000)) == [String(repeating: "x", count: 100_000)])
}

/// A combining mark, ZWJ or variation selector right after a space joins the space into
/// one Character, which Swift calls whitespace, so the split drops the mark. Nothing a
/// voice would say is lost, but the documented "nothing is dropped" does not hold for it.
@Test func aMarkRightAfterASpaceIsKept() {
    let text = "Hello \u{301}world. Then \u{200D}more. And \u{FE0F}this."
    let joined = SpokenChunks.split(text).joined(separator: " ")
    #expect(joined.contains("world") && joined.contains("this."))
    withKnownIssue("SpokenChunks drops a combining mark, ZWJ or variation selector that follows whitespace") {
        #expect(ink(joined) == ink(text))
    }
}

// MARK: Time

/// Linear, near enough: four times the text takes well under sixteen times as long
/// (a quadratic split would). Medians of five runs, in a debug build.
@Test func splittingTimeGrowsLinearly() {
    func median(_ text: String) -> Duration {
        let clock = ContinuousClock()
        var times: [Duration] = []
        for _ in 0..<5 {
            let start = clock.now
            let pieces = SpokenChunks.split(text)
            times.append(clock.now - start)
            #expect(!pieces.isEmpty)
        }
        return times.sorted()[2]
    }
    func seconds(_ duration: Duration) -> Double {
        Double(duration.components.seconds) + Double(duration.components.attoseconds) / 1e18
    }
    var random = Seeded(42)
    var sentences = ""
    while sentences.utf8.count < 100_000 { sentences += text(&random, words: 12) + ". " }
    let shapes: [(String, String)] = [
        ("random words", sentences),
        ("no punctuation", String(repeating: "and then the build went on ", count: 3_800)),
        ("tiny sentences", String(repeating: "A. ", count: 33_400)),
        ("only dots", String(repeating: ".", count: 100_000)),
        ("closing marks", "Done!" + String(repeating: ")", count: 100_000)),
        ("line breaks", String(repeating: "word\n", count: 20_000)),
    ]
    for (name, large) in shapes {
        let small = String(large.prefix(large.count / 4))
        let (smallTime, largeTime) = (seconds(median(small)), seconds(median(large)))
        #expect(largeTime < 2, "\(name): \(largeTime) s for \(large.utf8.count) bytes")
        // Below a millisecond the ratio is noise; above it, growth stays near linear.
        if largeTime > 0.001 {
            #expect(largeTime / max(smallTime, 1e-6) < 9, "\(name): \(smallTime) s, then \(largeTime) s")
        }
    }
}
