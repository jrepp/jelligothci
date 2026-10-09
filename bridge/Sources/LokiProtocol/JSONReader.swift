/// Why a loki/1 message was refused. Any of these closes the connection.
public struct LokiError: Error, Equatable, Sendable, CustomStringConvertible {
    public let reason: String
    public init(_ reason: String) { self.reason = reason }
    public var description: String { reason }
}

/// The strict JSON reader behind the codec, the Swift twin of core/agent/loki_json.c:
/// the same grammar, limits, and refusals, so the board and the bridge never read one
/// message two ways (docs/loki-protocol.md).
struct JSONReader {
    private let bytes: [UInt8]
    private var at = 0

    init(_ bytes: [UInt8]) { self.bytes = bytes }

    private mutating func space() {
        while at < bytes.count, [0x20, 0x09, 0x0A, 0x0D].contains(bytes[at]) { at += 1 }
    }

    /// Consumes `c` after optional whitespace.
    mutating func take(_ c: UInt8) -> Bool {
        space()
        guard at < bytes.count, bytes[at] == c else { return false }
        at += 1
        return true
    }

    mutating func expect(_ c: UInt8, _ what: String) throws(LokiError) {
        guard take(c) else { throw LokiError("expected \(what)") }
    }

    /// True when only whitespace remains.
    mutating func atEnd() -> Bool {
        space()
        return at == bytes.count
    }

    private func digit(_ index: Int) -> Bool {
        index < bytes.count && bytes[index] >= 0x30 && bytes[index] <= 0x39
    }

    /// A string of at most `maxBytes` UTF-8 bytes. Refuses invalid UTF-8, unpaired
    /// surrogates, escapes other than \" \\ \/ \n \uXXXX, and control characters other
    /// than line feed.
    mutating func string(maxBytes: Int) throws(LokiError) -> [UInt8] {
        try expect(0x22, "a string")
        var out: [UInt8] = []
        while true {
            guard at < bytes.count else { throw LokiError("unterminated string") }
            let c = bytes[at]
            if c == 0x22 {
                at += 1
                return out
            }
            if c == 0x5C {
                at += 1
                out += try escape()
            } else {
                let length = c < 0x20 ? 0 : Self.utf8Length(bytes, at)
                guard length > 0 else { throw LokiError("invalid character in a string") }
                out += bytes[at..<(at + length)]
                at += length
            }
            guard out.count <= maxBytes else { throw LokiError("string too long") }
        }
    }

    private mutating func escape() throws(LokiError) -> [UInt8] {
        guard at < bytes.count else { throw LokiError("unterminated escape") }
        let c = bytes[at]
        at += 1
        let code: UInt32
        switch c {
        case 0x22, 0x5C, 0x2F: code = UInt32(c)
        case 0x6E: code = 0x0A
        case 0x75: code = try unicodeEscape()
        default: throw LokiError("unsupported escape")
        }
        guard code >= 0x20 || code == 0x0A else { throw LokiError("control character in a string") }
        return Self.utf8(code)
    }

    private mutating func hex4() throws(LokiError) -> UInt32 {
        guard bytes.count - at >= 4 else { throw LokiError("short \\u escape") }
        var value: UInt32 = 0
        for offset in 0..<4 {
            guard let digit = Self.hexValue(bytes[at + offset]) else { throw LokiError("bad \\u escape") }
            value = value * 16 + digit
        }
        at += 4
        return value
    }

    private mutating func unicodeEscape() throws(LokiError) -> UInt32 {
        let high = try hex4()
        if (0xDC00...0xDFFF).contains(high) { throw LokiError("unpaired low surrogate") }
        guard (0xD800...0xDBFF).contains(high) else { return high }
        guard bytes.count - at >= 2, bytes[at] == 0x5C, bytes[at + 1] == 0x75 else {
            throw LokiError("unpaired high surrogate")
        }
        at += 2
        let low = try hex4()
        guard (0xDC00...0xDFFF).contains(low) else { throw LokiError("unpaired high surrogate") }
        return 0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00)
    }

    static func hexValue(_ c: UInt8) -> UInt32? {
        switch c {
        case 0x30...0x39: UInt32(c - 0x30)
        case 0x61...0x66: UInt32(c - 0x61 + 10)
        case 0x41...0x46: UInt32(c - 0x41 + 10)
        default: nil
        }
    }

    static func utf8(_ code: UInt32) -> [UInt8] {
        switch code {
        case ..<0x80: [UInt8(code)]
        case ..<0x800: [UInt8(0xC0 | code >> 6), UInt8(0x80 | code & 0x3F)]
        case ..<0x10000:
            [UInt8(0xE0 | code >> 12), UInt8(0x80 | code >> 6 & 0x3F), UInt8(0x80 | code & 0x3F)]
        default:
            [UInt8(0xF0 | code >> 18), UInt8(0x80 | code >> 12 & 0x3F),
             UInt8(0x80 | code >> 6 & 0x3F), UInt8(0x80 | code & 0x3F)]
        }
    }

    /// Length of the well-formed UTF-8 sequence at `index` (RFC 3629), or 0.
    static func utf8Length(_ bytes: [UInt8], _ index: Int) -> Int {
        let c = bytes[index]
        var low: UInt8 = 0x80, high: UInt8 = 0xBF
        let length: Int
        switch c {
        case ..<0x80: return 1
        case 0xC2...0xDF: length = 2
        case 0xE0...0xEF: length = 3
        case 0xF0...0xF4: length = 4
        default: return 0
        }
        switch c {
        case 0xE0: low = 0xA0
        case 0xED: high = 0x9F
        case 0xF0: low = 0x90
        case 0xF4: high = 0x8F
        default: break
        }
        guard bytes.count - index >= length, (low...high).contains(bytes[index + 1]) else { return 0 }
        for offset in 2..<max(2, length) where !(0x80...0xBF).contains(bytes[index + offset]) {
            return 0
        }
        return length
    }

    /// An integer without fraction, exponent, leading zero, or "-0", within `range`.
    mutating func integer(_ range: ClosedRange<Int64>) throws(LokiError) -> Int64 {
        space()
        var index = at
        let negative = index < bytes.count && bytes[index] == 0x2D
        if negative { index += 1 }
        let first = index
        var magnitude: Int64 = 0
        while digit(index) {
            guard index - first < 18 else { throw LokiError("number too long") }
            magnitude = magnitude * 10 + Int64(bytes[index] - 0x30)
            index += 1
        }
        let digits = index - first
        guard digits > 0, !(bytes[first] == 0x30 && (digits > 1 || negative)),
              !(index < bytes.count && [0x2E, 0x65, 0x45].contains(bytes[index]))
        else { throw LokiError("not an integer") }
        let value = negative ? -magnitude : magnitude
        guard range.contains(value) else { throw LokiError("number out of range") }
        at = index
        return value
    }

    private mutating func literal(_ word: String) -> Bool {
        space()
        let utf8 = Array(word.utf8)
        guard bytes.count - at >= utf8.count, Array(bytes[at..<(at + utf8.count)]) == utf8 else {
            return false
        }
        at += utf8.count
        return true
    }

    mutating func bool() throws(LokiError) -> Bool {
        if literal("true") { return true }
        if literal("false") { return false }
        throw LokiError("not a boolean")
    }

    private mutating func skipDigits(_ index: Int) -> Int {
        var index = index
        while digit(index) { index += 1 }
        return index
    }

    /// Any JSON number: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
    private mutating func skipNumber() throws(LokiError) {
        var index = at
        if index < bytes.count, bytes[index] == 0x2D { index += 1 }
        guard digit(index) else { throw LokiError("bad value") }
        index = bytes[index] == 0x30 ? index + 1 : skipDigits(index)
        if index < bytes.count, bytes[index] == 0x2E {
            index += 1
            guard digit(index) else { throw LokiError("bad number") }
            index = skipDigits(index)
        }
        if index < bytes.count, bytes[index] == 0x65 || bytes[index] == 0x45 {
            index += 1
            if index < bytes.count, bytes[index] == 0x2B || bytes[index] == 0x2D { index += 1 }
            guard digit(index) else { throw LokiError("bad number") }
            index = skipDigits(index)
        }
        at = index
    }

    /// Validates and skips any value, nesting arrays and objects at most `depth` deep.
    mutating func skip(depth: Int) throws(LokiError) {
        space()
        guard at < bytes.count else { throw LokiError("missing value") }
        switch bytes[at] {
        case 0x22: _ = try string(maxBytes: .max)
        case 0x7B, 0x5B:
            guard depth > 0 else { throw LokiError("nested too deeply") }
            try skipContainer(depth: depth - 1)
        case 0x74: guard literal("true") else { throw LokiError("bad literal") }
        case 0x66: guard literal("false") else { throw LokiError("bad literal") }
        case 0x6E: guard literal("null") else { throw LokiError("bad literal") }
        default: try skipNumber()
        }
    }

    private mutating func skipContainer(depth: Int) throws(LokiError) {
        let object = bytes[at] == 0x7B
        let close: UInt8 = object ? 0x7D : 0x5D
        at += 1
        if take(close) { return }
        repeat {
            if object {
                _ = try string(maxBytes: .max)
                try expect(0x3A, "a colon")
            }
            try skip(depth: depth)
        } while take(0x2C)
        try expect(close, "the end of a container")
    }
}
