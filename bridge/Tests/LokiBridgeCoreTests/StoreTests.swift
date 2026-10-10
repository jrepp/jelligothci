import Foundation
import Testing
@testable import LokiBridgeCore

/// A fresh directory under the system temp folder for one test.
func scratchDirectory() -> URL {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("loki-tests-\(UUID().uuidString)")
    try! FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    return url
}

private func mode(_ url: URL) -> Int {
    (try! FileManager.default.attributesOfItem(atPath: url.path)[.posixPermissions] as! NSNumber).intValue
}

@Test func identitiesAreCreatedPrivateAndReloadTheSame() throws {
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let file = directory.appendingPathComponent("support/identity.p12")
    let first = try TLSIdentity.loadOrCreate(at: file, commonName: "Loki Bridge")
    #expect(Fingerprint.isValid(first.fingerprint))
    #expect(mode(file) == 0o600)
    #expect(mode(file.deletingLastPathComponent()) == 0o700)
    let again = try TLSIdentity.loadOrCreate(at: file, commonName: "Loki Bridge")
    #expect(again.fingerprint == first.fingerprint)
    #expect(throws: IdentityError.self) { try TLSIdentity.create(at: file, commonName: "Loki Bridge") }
    let other = try TLSIdentity.loadOrCreate(at: directory.appendingPathComponent("other.p12"), commonName: "Loki Agent 1")
    #expect(other.fingerprint != first.fingerprint)
    // A loosened file is made private again when loaded.
    try FileManager.default.setAttributes([.posixPermissions: 0o644], ofItemAtPath: file.path)
    _ = try TLSIdentity.load(file)
    #expect(mode(file) == 0o600)
}

@Test func commonNamesCannotInjectSubjectFields() {
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    for name in ["", "a/CN=evil", "x=y", String(repeating: "n", count: 41), "line\nbreak"] {
        #expect(throws: IdentityError.self) {
            try TLSIdentity.create(at: directory.appendingPathComponent("\(UUID()).p12"), commonName: name)
        }
    }
}

@Test func pairingPersistsPrivately() throws {
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let file = directory.appendingPathComponent("devices.json")
    let store = try PairingStore(file: file)
    let fingerprint = String(repeating: "ab", count: 32)
    try store.pair(PairedDevice(name: "Loki Agent", fingerprint: fingerprint))
    try store.pair(PairedDevice(name: "Renamed", fingerprint: fingerprint))
    #expect(mode(file) == 0o600)
    let reloaded = try PairingStore(file: file)
    #expect(reloaded.all.count == 1 && reloaded.device(fingerprint: fingerprint)?.name == "Renamed")
    try reloaded.unpair(fingerprint: fingerprint)
    #expect(try PairingStore(file: file).all.isEmpty)
    #expect(throws: IdentityError.self) { try store.pair(PairedDevice(name: "x", fingerprint: "ABCD")) }
}

@Test func aDamagedPairingFileIsRefused() throws {
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let file = directory.appendingPathComponent("devices.json")
    try Data("not json".utf8).write(to: file)
    #expect(throws: (any Error).self) { try PairingStore(file: file) }
    try Data(#"[{"name":"x","fingerprint":"zz","paired":"2026-10-09T00:00:00Z"}]"#.utf8).write(to: file)
    #expect(throws: IdentityError.self) { try PairingStore(file: file) }
}
