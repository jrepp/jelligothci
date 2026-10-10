import CryptoKit
import Darwin
import Foundation
import Testing
@testable import LokiBridgeCore

/// A board on a pseudo-terminal that answers the @A1 pairing commands, with log lines
/// mixed in like the real console.
final class FakeBoard: @unchecked Sendable {
    let path: String
    private let master: Int32
    /// Held open: with no open slave the master reads fail at once.
    private let slave: Int32
    private let lock = NSLock()
    private var certificate: [UInt8] = []
    private(set) var commit: [String] = []
    let app: String
    let refuseCommit: Bool
    static let fingerprint = String(repeating: "5a", count: 32)

    init(app: String = "agent", refuseCommit: Bool = false) {
        var master: Int32 = -1, slave: Int32 = -1
        precondition(openpty(&master, &slave, nil, nil, nil) == 0)
        self.master = master
        self.slave = slave
        path = String(cString: ttyname(slave))
        self.app = app
        self.refuseCommit = refuseCommit
        Thread { self.serve() }.start()
    }

    var received: [UInt8] { lock.withLock { certificate } }
    var committed: [String] { lock.withLock { commit } }

    private func reply(_ id: Substring, _ body: String) {
        let text = "I (1234) agent_wifi: noise\n\n@A1 \(id) \(body)\nI (1235) more noise\n"
        _ = text.withCString { write(master, $0, strlen($0)) }
    }

    private func serve() {
        var line: [UInt8] = []
        var byte: UInt8 = 0
        while read(master, &byte, 1) == 1 {
            guard byte == 0x0A else { line.append(byte); continue }
            let words = String(decoding: line, as: UTF8.self).split(separator: " ")
            line = []
            guard words.count >= 3, words[0] == "@A1" else { continue }
            answer(words[1], Array(words.dropFirst(2)))
        }
    }

    private func answer(_ id: Substring, _ words: [Substring]) {
        switch words.joined(separator: " ") {
        case "hello": reply(id, #"{"ok":true,"app":"\#(app)","protocol":1}"#)
        case "pair show": reply(id, #"{"ok":true,"name":"Loki Agent TEST","fingerprint":"\#(Self.fingerprint)"}"#)
        case "pair begin":
            lock.withLock { certificate = [] }
            reply(id, #"{"ok":true,"received":0}"#)
        default:
            if words.first == "pair", words.count == 4, words[1] == "cert", Int(words[2]) == received.count {
                let hex = Array(words[3].utf8)
                let bytes = stride(from: 0, to: hex.count, by: 2).map {
                    UInt8(String(decoding: hex[$0..<$0 + 2], as: UTF8.self), radix: 16)!
                }
                lock.withLock { certificate += bytes }
                reply(id, #"{"ok":true,"received":\#(received.count)}"#)
            } else if words.first == "pair", words.count >= 5, words[1] == "commit" {
                let digest = SHA256.hash(data: received).map { String(format: "%02x", $0) }.joined()
                guard !refuseCommit, words[4] == digest else {
                    reply(id, #"{"ok":false,"error":"certificate"}"#)
                    return
                }
                lock.withLock { commit = words.map(String.init) }
                reply(id, #"{"ok":true,"received":0}"#)
            } else {
                reply(id, #"{"ok":false,"error":"syntax"}"#)
            }
        }
    }
}

@Test func pairingHandsOverTheCertificateAndPinsTheBoard() throws {
    let board = FakeBoard()
    let store = try PairingStore(file: scratchDirectory().appendingPathComponent("devices.json"))
    let result = try USBPairing.pair(console: try SerialConsole(path: board.path), identity: Fixture.bridge,
                                     store: store, host: "studio.local", port: 8766)
    #expect(board.received == [UInt8](Fixture.bridge.certificateDER))
    #expect(Array(board.committed.prefix(5)) == ["pair", "commit", "studio.local", "8766", Fixture.bridge.fingerprint])
    #expect(result.device.fingerprint == FakeBoard.fingerprint && result.device.name == "Loki Agent TEST")
    #expect(store.device(fingerprint: FakeBoard.fingerprint) != nil)
}

private func pairingError(_ board: FakeBoard, _ store: PairingStore) -> String {
    do {
        _ = try USBPairing.pair(console: try SerialConsole(path: board.path), identity: Fixture.bridge, store: store,
                                host: "studio.local", port: 8766)
        return "paired"
    } catch {
        return "\(error)"
    }
}

@Test func onlyLokiAgentIsPaired() throws {
    let store = try PairingStore(file: scratchDirectory().appendingPathComponent("devices.json"))
    #expect(pairingError(FakeBoard(app: "pet"), store).contains("not Loki Agent"))
    #expect(store.all.isEmpty)
}

@Test func aRefusedCommitPairsNothing() throws {
    let store = try PairingStore(file: scratchDirectory().appendingPathComponent("devices.json"))
    #expect(pairingError(FakeBoard(refuseCommit: true), store).contains("certificate"))
    #expect(store.all.isEmpty)
}

@Test func consoleRequestsStayPrintableAndShort() throws {
    let console = try SerialConsole(path: FakeBoard().path)
    #expect(throws: ConsoleError.self) { try console.request("pair\ncommit") }
    #expect(throws: ConsoleError.self) { try console.request(String(repeating: "x", count: 221)) }
    #expect(try console.request("hello")["app"] as? String == "agent")
}
