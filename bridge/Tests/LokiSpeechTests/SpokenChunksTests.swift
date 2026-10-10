import Testing
@testable import LokiSpeech

@Test func shortRepliesAreOnePiece() {
    #expect(SpokenChunks.split("Two plus two is four.") == ["Two plus two is four."])
    #expect(SpokenChunks.split("Done. All tests pass.") == ["Done. All tests pass."])
    #expect(SpokenChunks.split("") == [])
    #expect(SpokenChunks.split("   \n ") == [])
}

@Test func piecesAreSentencesWithShortOnesJoined() {
    let text = "I fixed the flaky test in the link edge suite. The close frame now arrives with an "
        + "error, and the raw board keeps it. Everything else was already passing. I also widened two "
        + "timing margins so slow runners stop failing. Nothing was committed yet."
    let pieces = SpokenChunks.split(text)
    #expect(pieces == ["I fixed the flaky test in the link edge suite.",
                       "The close frame now arrives with an error, and the raw board keeps it.",
                       "Everything else was already passing.",
                       // A short last sentence joins the piece before it.
                       "I also widened two timing margins so slow runners stop failing. Nothing was committed yet."])
    #expect(pieces.joined(separator: " ") == text)
    // Sentences too short for a render of their own join the next.
    #expect(SpokenChunks.split("Yes. Done. The tests pass now, all two hundred of them. Okay.")
        == ["Yes. Done. The tests pass now, all two hundred of them. Okay."])
    #expect(SpokenChunks.split("Okay. I ran every test in the project and they all pass. Lint is clean too.")
        == ["Okay. I ran every test in the project and they all pass. Lint is clean too."])
    #expect(SpokenChunks.split("Okay. I ran every test in the project and they all pass. The lint check is clean, "
                               + "with no warnings.")
        == ["Okay. I ran every test in the project and they all pass.", "The lint check is clean, with no warnings."])
}

@Test func sentencesEndAtPunctuationQuotesAndLineBreaks() {
    #expect(SpokenChunks.sentences("He said \"stop.\" Then it stopped! Why? Because.\nNext line")
        == ["He said \"stop.\"", "Then it stopped!", "Why?", "Because.", "Next line"])
    // A full stop inside a word or number is not the end of a sentence.
    #expect(SpokenChunks.sentences("Version 2.1.296 runs on macOS 26.1 fine. Yes.")
        == ["Version 2.1.296 runs on macOS 26.1 fine.", "Yes."])
}

@Test func nothingIsLostOrReorderedOnAnyInput() {
    var generator = SystemRandomNumberGenerator()
    let words = ["alpha", "beta.", "gamma!", "delta?", "eps\"", "zeta.)", "\n", "  ", "eta", "theta,"]
    for _ in 0..<500 {
        let text = (0..<Int.random(in: 0...40, using: &generator))
            .map { _ in words.randomElement(using: &generator)! }.joined(separator: " ")
        let pieces = SpokenChunks.split(text)
        let squeezed = text.split(whereSeparator: \.isWhitespace).joined(separator: " ")
        #expect(pieces.joined(separator: " ") == squeezed)
        #expect(pieces.allSatisfy { !$0.isEmpty })
    }
}
