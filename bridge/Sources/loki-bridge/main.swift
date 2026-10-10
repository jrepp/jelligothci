import Foundation
import LokiBridgeCore
import LokiProtocol

/// Loki Bridge from the terminal (RFC-003 phase 1), until the menu-bar app exists:
///   loki-bridge identity                         this bridge's certificate fingerprint
///   loki-bridge pair SERIAL [--host H] [--port P] pair the board on that USB port
///   loki-bridge devices | unpair FINGERPRINT      the paired boards
///   loki-bridge serve [--port P] [--voice V] [--echo] [--recordings]
///       run the link: transcribe what the board hears, reply in a Chatter voice (Scar)
let usage = """
    usage: loki-bridge identity | pair SERIAL [--host NAME] [--port N] | devices | unpair FINGERPRINT
           | serve [--port N] [--voice NAME] [--echo] [--recordings]
    """

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("loki-bridge: \(message)\n".utf8))
    exit(1)
}

func option(_ name: String, in arguments: [String]) -> String? {
    guard let index = arguments.firstIndex(of: name), index + 1 < arguments.count else { return nil }
    return arguments[index + 1]
}

func port(in arguments: [String]) -> UInt16 {
    guard let text = option("--port", in: arguments) else { return Loki.defaultPort }
    guard let value = UInt16(text), value > 0 else { fail("--port is 1 to 65535") }
    return value
}

func identity() -> TLSIdentity {
    do { return try TLSIdentity.loadOrCreate(at: BridgeFiles.identity, commonName: "Loki Bridge") } catch {
        fail("\(error)")
    }
}

func store() -> PairingStore {
    do { return try PairingStore(file: BridgeFiles.devices) } catch { fail("\(error)") }
}

let arguments = Array(CommandLine.arguments.dropFirst())
switch arguments.first {
case "identity":
    print(identity().fingerprint)
case "pair":
    guard arguments.count >= 2 else { fail(usage) }
    guard let host = option("--host", in: arguments) ?? BridgeFiles.localHostName else {
        fail("cannot tell this Mac's name; pass --host")
    }
    do {
        let console = try SerialConsole(path: arguments[1])
        let result = try USBPairing.pair(console: console, identity: identity(), store: store(), host: host,
                                         port: port(in: arguments))
        print("Paired \(result.device.name) (\(result.device.fingerprint.prefix(16))…); it reaches this bridge at \(result.host):\(result.port)")
    } catch {
        fail("\(error)")
    }
case "devices":
    for device in store().all { print("\(device.fingerprint)  \(device.name)  paired \(device.paired)") }
case "unpair":
    guard arguments.count == 2 else { fail(usage) }
    do { try store().unpair(fingerprint: arguments[1]) } catch { fail("\(error)") }
case "serve":
    var options = Serve.Options()
    options.port = port(in: arguments)
    options.echo = arguments.contains("--echo")
    options.recordings = arguments.contains("--recordings")
    if let voice = option("--voice", in: arguments) { options.voice = voice }
    try await Serve.run(identity: identity(), store: store(), options: options)
default:
    fail(usage)
}
