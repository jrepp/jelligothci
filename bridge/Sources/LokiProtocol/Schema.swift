/// The table-driven core of loki/1, mirroring core/agent/loki.c: each message type
/// lists its fields in protocol order, which is also the order of the canonical form
/// and of encoded keys (docs/loki-protocol.md).
enum FieldKind: Sendable {
    case unsigned(ClosedRange<Int64>)
    case signed(ClosedRange<Int64>)
    case flag
    case text(maxBytes: Int)        // line feeds allowed
    case line(maxBytes: Int)        // no line feeds
    case identifier                 // 1...32 of A-Z a-z 0-9 _ -
    case hex(length: Int)           // lowercase
    case word([String])             // an enumeration, written bare in the canonical form
    case constant(String)           // one exact string, written quoted
    case options(ClosedRange<Int>)  // strings of at most 40 bytes, no line feeds
}

enum FieldValue: Equatable, Sendable {
    case number(Int64)
    case flag(Bool)
    case string(String)
    case strings([String])
}

struct FieldSpec: Sendable {
    let name: String
    let kind: FieldKind
    /// nil when the field is required.
    let fallback: FieldValue?
}

struct TypeSpec: Sendable {
    let name: String
    let fields: [FieldSpec]
}

/// A decoded message before it becomes a Swift enum case: every field present, with
/// defaults applied.
struct RawMessage: Equatable, Sendable {
    let type: String
    let seq: UInt32
    let values: [String: FieldValue]
}

enum RawResult: Equatable, Sendable {
    case message(RawMessage)
    case ignored(seq: UInt32)
}

enum Schema {
    static let keyBytes = 32
    static let valueDepth = 4

    static func field(_ name: String, _ kind: FieldKind, _ fallback: FieldValue? = nil) -> FieldSpec {
        FieldSpec(name: name, kind: kind, fallback: fallback)
    }

    /// Decodes one control message whose types are `specs`.
    static func decode(_ bytes: [UInt8], specs: [String: TypeSpec], maxBytes: Int) throws(LokiError)
        -> RawResult {
        guard bytes.count <= maxBytes else { throw LokiError("message too long") }
        var reader = JSONReader(bytes)
        try reader.expect(0x7B, "an object")
        guard try reader.string(maxBytes: keyBytes) == Array("t".utf8) else {
            throw LokiError("the first key is not t")
        }
        try reader.expect(0x3A, "a colon")
        // Names are matched by their bytes, as the board's decoder does: Swift's String
        // equality would also match canonically equivalent text.
        let name = try reader.string(maxBytes: keyBytes)
        let spec = specs.values.first { Array($0.name.utf8) == name }
        var seq: UInt32?
        var values: [String: FieldValue] = [:]
        while reader.take(0x2C) {
            let key = try reader.string(maxBytes: keyBytes)
            try reader.expect(0x3A, "a colon")
            if key == Array("t".utf8) { throw LokiError("repeated t") }
            if key == Array("seq".utf8) {
                guard seq == nil else { throw LokiError("repeated seq") }
                seq = UInt32(try reader.integer(1...Int64(UInt32.max)))
            } else if let field = spec?.fields.first(where: { Array($0.name.utf8) == key }) {
                guard values[field.name] == nil else { throw LokiError("repeated \(field.name)") }
                values[field.name] = try read(field.kind, &reader)
            } else {
                try reader.skip(depth: valueDepth)
            }
        }
        try reader.expect(0x7D, "the end of the object")
        guard reader.atEnd() else { throw LokiError("data after the object") }
        guard let seq else { throw LokiError("missing seq") }
        guard let spec else { return .ignored(seq: seq) }
        for field in spec.fields where values[field.name] == nil {
            guard let fallback = field.fallback else { throw LokiError("missing \(field.name)") }
            values[field.name] = fallback
        }
        return .message(RawMessage(type: spec.name, seq: seq, values: values))
    }

    private static func read(_ kind: FieldKind, _ reader: inout JSONReader) throws(LokiError) -> FieldValue {
        switch kind {
        case .unsigned(let range), .signed(let range):
            return .number(try reader.integer(range))
        case .flag:
            return .flag(try reader.bool())
        case .text(let maxBytes), .line(let maxBytes):
            let value = try reader.string(maxBytes: maxBytes)
            return .string(try checked(value, kind))
        case .identifier, .hex:
            return .string(try checked(try reader.string(maxBytes: 64), kind))
        case .word(let words):
            let word = try reader.string(maxBytes: keyBytes)
            guard let match = words.first(where: { Array($0.utf8) == word }) else { throw LokiError("unknown value") }
            return .string(match)
        case .constant(let expected):
            guard try reader.string(maxBytes: keyBytes) == Array(expected.utf8) else {
                throw LokiError("unexpected value")
            }
            return .string(expected)
        case .options(let count):
            try reader.expect(0x5B, "an array")
            var options: [String] = []
            if !reader.take(0x5D) {
                repeat {
                    guard options.count < count.upperBound else { throw LokiError("too many options") }
                    options.append(try checked(try reader.string(maxBytes: 40), .line(maxBytes: 40)))
                } while reader.take(0x2C)
                try reader.expect(0x5D, "the end of the options")
            }
            guard count.contains(options.count) else { throw LokiError("wrong number of options") }
            return .strings(options)
        }
    }

    /// Applies a string field's rules to validated UTF-8 bytes.
    static func checked(_ bytes: [UInt8], _ kind: FieldKind) throws(LokiError) -> String {
        switch kind {
        case .line(let maxBytes), .text(let maxBytes):
            guard bytes.count <= maxBytes else { throw LokiError("string too long") }
            if case .line = kind, bytes.contains(0x0A) { throw LokiError("line feed not allowed") }
        case .identifier:
            guard (1...32).contains(bytes.count), bytes.allSatisfy(isIdentifierByte) else {
                throw LokiError("bad identifier")
            }
        case .hex(let length):
            guard bytes.count == length, bytes.allSatisfy(isLowerHex) else { throw LokiError("bad hex") }
        default:
            break
        }
        return String(decoding: bytes, as: UTF8.self)
    }

    static func isIdentifierByte(_ c: UInt8) -> Bool {
        (0x61...0x7A).contains(c) || (0x41...0x5A).contains(c) || (0x30...0x39).contains(c) ||
            c == 0x5F || c == 0x2D
    }

    static func isLowerHex(_ c: UInt8) -> Bool {
        (0x30...0x39).contains(c) || (0x61...0x66).contains(c)
    }

    // MARK: Canonical form

    static func quoted(_ text: String) -> String {
        var out = "\""
        for byte in text.utf8 {
            switch byte {
            case 0x5C: out += "\\\\"
            case 0x22: out += "\\\""
            case 0x0A: out += "\\n"
            case 0x20...0x7E: out.unicodeScalars.append(Unicode.Scalar(byte))
            default: out += "\\x" + hex2(byte)
            }
        }
        return out + "\""
    }

    private static func hex2(_ byte: UInt8) -> String {
        let digits = Array("0123456789ABCDEF")
        return String([digits[Int(byte >> 4)], digits[Int(byte & 0x0F)]])
    }

    static func canonicalValue(_ value: FieldValue, _ kind: FieldKind) -> String {
        switch (value, kind) {
        case (.number(let n), _): String(n)
        case (.flag(let flag), _): flag ? "true" : "false"
        case (.string(let word), .word): word
        case (.string(let text), _): quoted(text)
        case (.strings(let list), _): "[" + list.map(quoted).joined(separator: ",") + "]"
        }
    }

    static func canonical(_ message: RawMessage, specs: [String: TypeSpec]) -> String {
        guard let spec = specs[message.type] else { return "" }
        var out = "\(spec.name) seq=\(message.seq)"
        for field in spec.fields {
            out += " \(field.name)=" + canonicalValue(message.values[field.name] ?? field.fallback!, field.kind)
        }
        return out
    }

    // MARK: Encoding

    /// JSON string with only ", \ and line feed escaped; other text is raw UTF-8.
    static func jsonString(_ text: String) -> String {
        var out = "\""
        for scalar in text.unicodeScalars {
            switch scalar {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            default: out.unicodeScalars.append(scalar)
            }
        }
        return out + "\""
    }

    /// Checks a value against its field's rules before it is sent.
    static func validate(_ value: FieldValue, _ field: FieldSpec) throws(LokiError) {
        switch (field.kind, value) {
        case (.unsigned(let range), .number(let n)), (.signed(let range), .number(let n)):
            guard range.contains(n) else { throw LokiError("\(field.name) out of range") }
        case (.flag, .flag):
            break
        case (.text, .string(let s)), (.line, .string(let s)), (.identifier, .string(let s)),
             (.hex, .string(let s)):
            let bytes = Array(s.utf8)
            guard bytes.allSatisfy({ $0 >= 0x20 || $0 == 0x0A }) else {
                throw LokiError("\(field.name) has a control character")
            }
            _ = try checked(bytes, field.kind)
        case (.word(let words), .string(let s)):
            guard words.contains(s) else { throw LokiError("\(field.name) has an unknown value") }
        case (.constant(let expected), .string(let s)):
            guard s == expected else { throw LokiError("\(field.name) has an unexpected value") }
        case (.options(let count), .strings(let list)):
            guard count.contains(list.count) else { throw LokiError("wrong number of options") }
            for option in list {
                let bytes = Array(option.utf8)
                guard bytes.allSatisfy({ $0 >= 0x20 }) else { throw LokiError("bad option") }
                _ = try checked(bytes, .line(maxBytes: 40))
            }
        default:
            throw LokiError("\(field.name) has the wrong type")
        }
    }

    /// Encodes in protocol order, leaving out optional fields at their default.
    static func encode(_ message: RawMessage, specs: [String: TypeSpec], maxBytes: Int) throws(LokiError)
        -> [UInt8] {
        guard let spec = specs[message.type] else { throw LokiError("unknown type") }
        guard message.seq >= 1 else { throw LokiError("seq starts at 1") }
        var out = "{\"t\":\(jsonString(spec.name)),\"seq\":\(message.seq)"
        for field in spec.fields {
            guard let value = message.values[field.name] ?? field.fallback else {
                throw LokiError("missing \(field.name)")
            }
            try validate(value, field)
            if value == field.fallback { continue }
            out += ",\(jsonString(field.name)):"
            switch value {
            case .number(let n): out += String(n)
            case .flag(let flag): out += flag ? "true" : "false"
            case .string(let s): out += jsonString(s)
            case .strings(let list): out += "[" + list.map(jsonString).joined(separator: ",") + "]"
            }
        }
        let bytes = Array((out + "}").utf8)
        guard bytes.count <= maxBytes else { throw LokiError("message too long") }
        return bytes
    }
}
