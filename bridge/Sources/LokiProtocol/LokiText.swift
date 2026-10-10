/// Makes free text (transcripts, captions, tool descriptions) fit a loki/1 string
/// field: control characters become spaces (line feeds stay where the field allows
/// them), and text past the byte limit is cut at a character boundary and ends with
/// an ellipsis. Never for what an approval card asks about: a command cut short
/// could hide its dangerous end, so approvals show the whole action or refuse.
public enum LokiText {
    public static func fitted(_ text: String, maxBytes: Int, lineFeeds: Bool = false) -> String {
        var scalars = String.UnicodeScalarView()
        for scalar in text.unicodeScalars {
            let control = scalar.value < 0x20 && !(lineFeeds && scalar == "\n")
            scalars.append(control ? " " : scalar)
        }
        let clean = String(scalars)
        guard clean.utf8.count > maxBytes else { return clean }
        let ellipsis = "\u{2026}"
        guard maxBytes >= ellipsis.utf8.count else { return "" }
        var out = ""
        for character in clean {
            if out.utf8.count + character.utf8.count + ellipsis.utf8.count > maxBytes { break }
            out.append(character)
        }
        return out + ellipsis
    }

    /// Free text fit for a terminal or a log line: every control character (C0, DEL
    /// and C1, such as CSI U+009B), every invisible format character (bidi controls,
    /// zero-width characters) and every line or paragraph separator becomes a space,
    /// so the text can neither drive the terminal nor rearrange or forge a line; then
    /// it is cut to `maxBytes` like `fitted`. Not for the board, whose captions keep
    /// format characters such as the joiners in emoji.
    public static func loggable(_ text: String, maxBytes: Int) -> String {
        let hidden: Set<Unicode.GeneralCategory> = [.control, .format, .lineSeparator, .paragraphSeparator]
        var scalars = String.UnicodeScalarView()
        for scalar in text.unicodeScalars {
            scalars.append(hidden.contains(scalar.properties.generalCategory) ? " " : scalar)
        }
        return fitted(String(scalars), maxBytes: maxBytes)
    }
}
