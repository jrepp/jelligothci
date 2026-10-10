/// Builds loki/1 control messages as JSON text for the differential tests: mostly
/// valid, often exactly at a limit, sometimes one step past it, with whitespace in
/// every legal position and unknown values nested up to six deep. The field table
/// is written out here from docs/loki-protocol.md rather than taken from Schema, so
/// the generator does not share the codec's mistakes.
struct MessageGrammar {
    var random: Random

    enum Value {
        case integer(ClosedRange<Int64>)
        case text(maxBytes: Int, lineFeeds: Bool)
        case identifier, nonce, flag, options, models
        case word([String])
    }

    static let states = ["idle", "listening", "heard", "thinking", "working", "asking", "speaking",
                         "success", "partial", "failure"]
    static let u32 = Int64(UInt32.max)

    /// Bridge-to-board types: field, value, required.
    static let types: [(name: String, fields: [(String, Value, Bool)])] = [
        ("welcome", [("proto", .integer(1...1), true), ("bridge", .text(maxBytes: 40, lineFeeds: false), true),
                     ("voice", .text(maxBytes: 40, lineFeeds: false), false)]),
        ("state", [("state", .word(states), true), ("caption", .text(maxBytes: 200, lineFeeds: true), false),
                   ("progress", .integer(-1...1000), false), ("queue", .integer(0...999), false)]),
        ("caption", [("text", .text(maxBytes: 400, lineFeeds: true), true), ("final", .flag, true)]),
        ("ask", [("id", .identifier, true), ("nonce", .nonce, true), ("kind", .word(["approve", "choose"]), true),
                 ("title", .text(maxBytes: 120, lineFeeds: false), true),
                 ("detail", .text(maxBytes: 600, lineFeeds: true), false), ("options", .options, true),
                 ("timeout_ms", .integer(1000...600_000), true)]),
        ("ask_cancel", [("id", .identifier, true)]),
        ("reply_begin", [("reply", .integer(1...u32), true), ("rate", .integer(24_000...24_000), true)]),
        ("reply_end", [("reply", .integer(1...u32), true), ("frames", .integer(0...u32), true)]),
        ("stop_audio", []),
        ("settings", [("volume", .integer(0...100), true)]),
        // current must also be below the number of models; the generator often breaks that.
        ("models", [("models", .models, true), ("current", .integer(0...15), true)]),
    ]

    static let unknownTypes = ["confetti", "hello", "answer", "", "STATE", " state", "st\\u0061te",
                               String(repeating: "y", count: 32), String(repeating: "y", count: 33),
                               String(repeating: "\u{e9}", count: 16), "\\ud83e\\udd84"]

    static let unknownKeys = ["x", "future", "", "seq2", "volume", "options", "\\u0073eq", "t", "Seq",
                              String(repeating: "k", count: 32), String(repeating: "k", count: 33),
                              String(repeating: "\u{e9}", count: 16), String(repeating: "\u{e9}", count: 16) + "k",
                              "\\ud83e\\udd84", "\\ud83e"]

    /// String source and the decoded bytes it adds.
    static let pieces: [(String, Int)] = [
        ("a", 1), ("Z", 1), (" ", 1), ("\u{e9}", 2), ("\\u00e9", 2), ("\u{2026}", 3), ("\\u2026", 3),
        ("\u{1F984}", 4), ("\\ud83e\\udd84", 4), ("\\uD83E\\uDD84", 4), ("\\\"", 1), ("\\\\", 1), ("\\/", 1),
        ("/", 1), ("\\u0041", 1), ("\\u007f", 1), ("\u{7f}", 1), ("\\u0080", 2), ("\\uffff", 3),
        ("\\udbff\\udfff", 4), ("{", 1), (":", 1), ("\\u0800", 3), ("\u{10FFFF}", 4),
    ]
    static let lineFeeds = ["\\n", "\\u000a", "\\u000A"]
    static let broken = ["\\t", "\\b", "\\r", "\\f", "\\ud83e", "\\udd84", "\t", "\\x41", "\\u00G0", "\\a",
                         "\u{1}", "\\u0000", "\\u001f", "\\ud83e\\u0041", "\\U0041", "\\ud83e\\ud83e"]

    mutating func space() -> String {
        random.below(5) == 0 ? random.pick([" ", "\t", "\n", "\r\n", "  ", " \t\r\n"]) : ""
    }

    mutating func integer(_ range: ClosedRange<Int64>) -> String {
        switch random.below(16) {
        case 0: return String(range.lowerBound - 1)
        case 1: return String(range.upperBound + 1)
        case 2:
            return random.pick(["-0", "01", "1.0", "1e3", "\"1\"", "null", "true", "1000000000000000000",
                                "- 1", "+1", "0x1", "4294967296", "-2147483649", "9223372036854775808"])
        case 3, 4: return String(range.lowerBound)
        case 5, 6: return String(range.upperBound)
        default:
            let width = UInt64(range.upperBound - range.lowerBound) + 1
            return String(range.lowerBound + Int64(random.next() % width))
        }
    }

    /// A quoted string of about maxBytes decoded bytes: often exactly at the limit or
    /// one past it, with multi-byte characters and escapes that may straddle it.
    mutating func text(maxBytes: Int, lineFeeds allowed: Bool) -> String {
        let target = switch random.below(7) {
        case 0: 0
        case 1, 2: maxBytes
        case 3: maxBytes + 1
        case 4: max(0, maxBytes - 1)
        default: random.below(maxBytes + 1)
        }
        var source = "", size = 0
        while size < target {
            let (piece, bytes): (String, Int) = random.below(12) == 0
                ? (random.pick(Self.lineFeeds), 1) : random.pick(Self.pieces)
            if size + bytes > target {
                // Sometimes let the last character straddle the limit.
                if random.below(3) == 0 { source += piece; size += bytes }
                break
            }
            source += piece
            size += bytes
        }
        if !allowed, random.below(6) != 0 { source = source.replacingOccurrences(of: "\\n", with: "n") }
        if random.below(25) == 0 {
            let at = source.index(source.startIndex, offsetBy: random.below(source.count + 1))
            source.insert(contentsOf: random.pick(Self.broken), at: at)
        }
        return "\"" + source + "\""
    }

    mutating func identifier() -> String {
        let chars = Array("abcXYZ019_-")
        switch random.below(12) {
        case 0: return "\"\""
        case 1: return "\"" + String(repeating: "i", count: 33) + "\""
        case 2: return "\"" + random.pick(["a b", "a.b", "\u{e9}", "a/b", "\\u0041b", "a\\n"]) + "\""
        case 3: return "\"" + String(repeating: "I", count: 32) + "\""
        default: return "\"" + String((0..<(1 + random.below(32))).map { _ in random.pick(chars) }) + "\""
        }
    }

    mutating func nonce() -> String {
        let hex = String((0..<32).map { _ in random.pick(Array("0123456789abcdef")) })
        switch random.below(12) {
        case 0: return "\"" + hex.uppercased() + "\""
        case 1: return "\"" + hex.dropLast() + "\""
        case 2: return "\"" + hex + "0\""
        case 3: return "\"\\u0030" + hex.dropFirst() + "\""
        default: return "\"" + hex + "\""
        }
    }

    mutating func options() -> String {
        let count = random.pick([2, 2, 2, 3, 4, 4, 0, 1, 5])
        var items: [String] = []
        for _ in 0..<count { items.append(space() + text(maxBytes: 40, lineFeeds: false) + space()) }
        if random.below(30) == 0 { items.append("1") }
        return "[" + space() + items.joined(separator: ",") + space() + "]"
    }

    /// A model name: 1 to 40 of A-Z a-z 0-9 . _ : / + -, often at a limit or past one,
    /// sometimes with a character outside the set or an escape.
    mutating func modelName() -> String {
        let chars = Array("aZ09._:/+-")
        let count = random.pick([1, 1, 8, 20, 40, 40, 0, 41])
        var name = String((0..<count).map { _ in random.pick(chars) })
        if random.below(8) == 0 {
            let at = name.index(name.startIndex, offsetBy: random.below(name.count + 1))
            name.insert(contentsOf: random.pick([" ", ",", "\\\\", "\\\"", "\\u0041", "\u{e9}", "\\n", "@", "*"]), at: at)
        }
        return "\"" + name + "\""
    }

    mutating func models() -> String {
        if random.below(20) == 0 { return random.pick(["[]", "null", "\"a\"", "[[\"a\"]]", "[1]"]) }
        let count = random.pick([1, 1, 2, 3, 5, 16, 16, 17])
        let items = (0..<count).map { _ in space() + modelName() + space() }
        return "[" + space() + items.joined(separator: ",") + space() + "]"
    }

    mutating func value(_ kind: Value) -> String {
        switch kind {
        case .integer(let range): return integer(range)
        case .text(let maxBytes, let lineFeeds): return text(maxBytes: maxBytes, lineFeeds: lineFeeds)
        case .identifier: return identifier()
        case .nonce: return nonce()
        case .flag: return random.pick(["true", "false", "true", "false", "null", "1", "\"true\"", "True"])
        case .options: return random.below(20) == 0 ? random.pick(["[]", "null", "\"a\"", "[[\"a\"],\"b\"]"]) : options()
        case .models: return models()
        case .word(let words):
            if random.below(10) == 0 {
                return random.pick(["\"IDLE\"", "\"\"", "\"dancing\"", "null", "\"\\u0069dle\"", "\"ch\\u006fose\""])
            }
            return "\"" + random.pick(words) + "\""
        }
    }

    /// Any JSON value; containers nest at most six deep, past the protocol's four.
    mutating func unknown(depth: Int = 0) -> String {
        switch random.below(depth >= 6 ? 4 : 7) {
        case 0:
            return random.pick(["0", "-0", "1.5", "-1e-2", "1E+2", "123456789012345678901234567890", "0e0",
                                "01", "1.", ".5", "1e", "-", "+1", "NaN"])
        case 1: return random.pick(["true", "false", "null", "nul", "nullx"])
        case 2, 3: return text(maxBytes: random.pick([0, 8, 40]), lineFeeds: true)
        case 4, 5:
            let items = (0..<random.below(4)).map { _ in space() + unknown(depth: depth + 1) + space() }
            return "[" + space() + items.joined(separator: ",") + "]"
        default:
            let members = (0..<random.below(4)).map { _ in
                space() + "\"" + random.pick(Self.unknownKeys) + "\"" + space() + ":" + space()
                    + unknown(depth: depth + 1)
            }
            return "{" + space() + members.joined(separator: ",") + space() + "}"
        }
    }

    mutating func member(_ key: String, _ value: String) -> String {
        space() + "\"" + key + "\"" + space() + ":" + space() + value + space()
    }

    mutating func message() -> [UInt8] {
        let known = random.below(10) < 8
        let type: (name: String, fields: [(String, Value, Bool)]) =
            known ? random.pick(Self.types) : (random.pick(Self.unknownTypes), [])
        var members: [String] = []
        if random.below(40) != 0 {
            let seq = integer(1...Self.u32)
            members.append(member("seq", seq))
        }
        for (name, kind, required) in type.fields {
            let include = required ? random.below(40) != 0 : random.below(2) == 0
            let repeated = random.below(60) == 0
            for _ in 0..<((include ? 1 : 0) + (repeated ? 1 : 0)) {
                let field = value(kind)
                members.append(member(name, field))
            }
        }
        for _ in 0..<random.pick([0, 0, 0, 1, 1, 2]) {
            let key = random.pick(Self.unknownKeys), field = unknown()
            members.append(member(key, field))
        }
        for i in members.indices.reversed() where i > 0 { members.swapAt(i, random.below(i + 1)) }
        var text = space() + "{" + space() + "\"t\"" + space() + ":" + space() + "\"" + type.name + "\"" + space()
        for member in members { text += "," + member }
        text += "}" + space()
        if random.below(10) == 0 {
            let size = random.pick([512, 2048]) - 8 + random.below(17)
            pad(&text, to: size)
        }
        return Array(text.utf8)
    }

    /// Grows a message to exactly size bytes with trailing whitespace or a final
    /// unknown member, when it is short enough.
    mutating func pad(_ text: inout String, to size: Int) {
        let length = text.utf8.count
        if random.below(2) == 0 {
            if length <= size { text += String(repeating: " ", count: size - length) }
            return
        }
        let trimmed = text.trimmingCharacters(in: [" ", "\t", "\n", "\r"])
        let shell = trimmed.utf8.count + ",\"p\":\"\"".utf8.count
        guard trimmed.hasSuffix("}"), shell <= size else { return }
        text = String(trimmed.dropLast()) + ",\"p\":\"" + String(repeating: "p", count: size - shell) + "\"}"
    }
}

/// Byte-level damage to an existing message.
struct Mutator {
    var random: Random

    static let tokens: [[UInt8]] = ([
        "\"", "\\", "\\u", "\\ud83e", "\\udd84", "\\u0000", "\\n", ",\"x\":1", ",\"seq\":2", ",\"t\":\"x\"",
        "[", "]", "{", "}", "-0", "1e5", "0", "4294967296", " ", "\t", "\r\n", "null", "true", ",", ":",
        "\u{e9}", "\u{1F984}", "[[[[[", "]]]]]", "\"\"", ",\"\":[]",
    ].map { Array($0.utf8) }) + [[0xC0, 0xAF], [0xED, 0xA0, 0x80], [0xF4, 0x90, 0x80, 0x80], [0xFF], [0x00], [0x7F],
                                 [0xF0, 0x9F, 0xA6], [0xE2, 0x80], [0xC2, 0xA0], [0xEF, 0xBB, 0xBF]]

    mutating func mutate(_ input: [UInt8]) -> [UInt8] {
        var bytes = input
        for _ in 0..<random.pick([1, 1, 1, 2, 3, 4]) {
            let at = random.below(bytes.count + 1)
            switch random.below(7) {
            case 0 where !bytes.isEmpty:
                let token = random.pick(Self.tokens)
                bytes[min(at, bytes.count - 1)] = random.pick(token)
            case 1: bytes.insert(contentsOf: random.pick(Self.tokens), at: at)
            case 2 where !bytes.isEmpty: bytes.remove(at: min(at, bytes.count - 1))
            case 3: bytes.insert(contentsOf: bytes[at..<min(bytes.count, at + 1 + random.below(24))], at: at)
            case 4: bytes = Array(bytes.prefix(at))
            case 5 where bytes.count > 1:
                let first = random.below(bytes.count), second = random.below(bytes.count)
                bytes.swapAt(first, second)
            default: bytes.insert(random.pick([0x20, 0x09, 0x0A, 0x0D]), at: at)
            }
        }
        return Array(bytes.prefix(4096))
    }
}
