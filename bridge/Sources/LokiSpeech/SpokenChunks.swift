import Foundation

/// A reply split for speaking while the rest still renders: a sentence at a time, so the
/// board starts talking soon after Claude answers. Chatter renders the Scar voice at
/// about 0.85 times real time plus some half a second a piece, so a piece is ready in
/// time only if it is no longer than the one playing before it: pieces stay about a
/// sentence long, and only sentences too short to be worth a render of their own join
/// the next. Pieces end at sentence boundaries; nothing is dropped or reordered, and
/// joining them with spaces gives the text back with its runs of whitespace made single.
/// (A combining mark right after a space is one whitespace Character to Swift, and goes
/// with the space: nothing a voice would say.)
public enum SpokenChunks {
    /// The first piece holds sentences until it is at least this many characters.
    public static let firstAtLeast = 30
    /// Each later piece holds sentences until it is at least this many characters.
    public static let laterAtLeast = 30
    /// Text shorter than this is one piece: splitting it would only add a render.
    public static let wholeBelow = 60

    public static func split(_ text: String) -> [String] {
        let sentences = Self.sentences(text)
        let whole = sentences.joined(separator: " ")
        guard whole.count >= wholeBelow, sentences.count > 1 else { return whole.isEmpty ? [] : [whole] }
        var pieces: [String] = []
        var current = ""
        for sentence in sentences {
            current = current.isEmpty ? sentence : current + " " + sentence
            if current.count >= (pieces.isEmpty ? firstAtLeast : laterAtLeast) {
                pieces.append(current)
                current = ""
            }
        }
        if !current.isEmpty {
            // A short tail joins the piece before it rather than costing its own render.
            if let last = pieces.last, current.count < firstAtLeast {
                pieces[pieces.count - 1] = last + " " + current
            } else {
                pieces.append(current)
            }
        }
        return pieces
    }

    /// Sentences: text up to and including ".", "!" or "?" (and any closing quotes or
    /// brackets) that is followed by whitespace or the end, or up to a line break.
    static func sentences(_ text: String) -> [String] {
        var sentences: [String] = []
        var current = ""
        let characters = Array(text)
        var index = 0
        func close() {
            let sentence = current.split(whereSeparator: \.isWhitespace).joined(separator: " ")
            if !sentence.isEmpty { sentences.append(sentence) }
            current = ""
        }
        while index < characters.count {
            let character = characters[index]
            if character.isNewline {
                close()
            } else {
                current.append(character)
                if ".!?".contains(character) {
                    var next = index + 1
                    while next < characters.count, "\"'”’)]".contains(characters[next]) {
                        current.append(characters[next])
                        next += 1
                    }
                    index = next - 1
                    if next >= characters.count || characters[next].isWhitespace { close() }
                }
            }
            index += 1
        }
        close()
        return sentences
    }
}
