import Foundation
import Testing
@testable import LokiProtocol

// `models` and `set_model` (ADR-018): one rule for a model name, applied alike by the
// check the bridge uses before offering a name, the encoder, and the decoder.

private func bytes(_ text: String) -> [UInt8] { Array(text.utf8) }

/// Property: for any candidate name, Loki.isModelName, encoding set_model, encoding a
/// one-name models list and decoding set_model all agree; what is accepted round-trips.
@Test func theModelNameRuleIsOneRuleEverywhere() throws {
    var random = Random(state: 0x5EED_0018)
    let pieces = ["a", "Z", "0", "9", ".", "_", ":", "/", "+", "-", " ", "\"", "\\", ",", "é", "@", "*", "\t", "~", "%", "#",
                  "\u{7F}", "\u{0}", "🦄", "A", "z"]
    for round in 0..<3000 {
        let length = random.pick([0, 1, 2, 5, 39, 40, 41, random.below(45)])
        let name = String((0..<length).map { _ in Character(random.pick(random.below(4) == 0 ? pieces : Array(pieces.prefix(10)))) })
        let valid = Loki.isModelName(name)
        let context = "round \(round): \(name.debugDescription)"
        let device = Result { try LokiCodec.encode(.setModel(name), seq: 1) }
        let bridge = Result { try LokiCodec.encode(.models(names: [name], current: 0), seq: 1) }
        #expect(((try? device.get()) != nil) == valid, "\(context)")
        #expect(((try? bridge.get()) != nil) == valid, "\(context)")
        // The decoder judges the same name, written by hand with JSON escapes as needed.
        var escaped = ""
        for scalar in name.unicodeScalars {
            switch scalar {
            case "\"": escaped += "\\\""
            case "\\": escaped += "\\\\"
            case _ where scalar.value < 0x20: escaped += "\\u" + String(format: "%04x", scalar.value)
            default: escaped.unicodeScalars.append(scalar)
            }
        }
        let decoded = Result { try LokiCodec.decodeDeviceMessage(bytes(#"{"t":"set_model","seq":1,"model":"\#(escaped)"}"#)) }
        if valid {
            #expect((try? decoded.get()) == .message(seq: 1, .setModel(name)), "\(context)")
            #expect(try LokiCodec.decodeDeviceMessage(device.get()) == .message(seq: 1, .setModel(name)), "\(context)")
            #expect(try LokiCodec.decodeBridgeMessage(bridge.get()) == .message(seq: 1, .models(names: [name], current: 0)),
                    "\(context)")
        } else {
            #expect((try? decoded.get()) == nil, "\(context)")
        }
    }
}

@Test func anEscapedCharacterCountsAsTheCharacterItStandsFor() throws {
    // JSON escapes of allowed characters decode to a valid name; the encoder never
    // writes them (the canonical form is plain).
    #expect(try LokiCodec.decodeDeviceMessage(bytes(#"{"t":"set_model","seq":1,"model":"\u0041:\/x"}"#))
            == .message(seq: 1, .setModel("A:/x")))
    #expect(try LokiCodec.encode(.setModel("A:/x"), seq: 1) == bytes(#"{"t":"set_model","seq":1,"model":"A:/x"}"#))
    // An escape of a forbidden character is still forbidden.
    for name in [#"a\u0020b"#, #"a\u00e9"#, #"a\nb"#, #"\u0000"#, #"a\u002cb"#] {
        #expect(throws: LokiError.self, "\(name)") {
            try LokiCodec.decodeDeviceMessage(bytes(#"{"t":"set_model","seq":1,"model":"\#(name)"}"#))
        }
    }
}

@Test func aListMayRepeatANameAndCurrentMustNameOneOfIt() throws {
    // The bridge never repeats a name (ModelChoice.offer), but the schema allows it.
    let repeated = BridgeMessage.models(names: ["a:1", "a:1"], current: 1)
    #expect(try LokiCodec.decodeBridgeMessage(LokiCodec.encode(repeated, seq: 3)) == .message(seq: 3, repeated))
    for (names, current) in [(["a"], UInt32(1)), (["a", "b"], 2), (Array(repeating: "m", count: 16), 16), (["a"], UInt32.max)] {
        #expect(throws: LokiError.self) { try LokiCodec.encode(.models(names: names, current: current), seq: 1) }
    }
}
