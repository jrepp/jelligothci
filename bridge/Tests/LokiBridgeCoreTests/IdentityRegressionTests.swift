import Foundation
import Testing
@testable import LokiBridgeCore

/// Regression: LibreSSL writes about one P-256 key in 256 with a 31-byte scalar, which
/// SecPKCS12Import accepts without an identity, so a bridge that made one could never
/// load it. Every identity made must load; 384 of them would hit a short key with
/// probability 1 - (255/256)^384, about 78%, if create stopped checking.
@Test func everyIdentityMadeCanBeLoaded() async throws {
    try await offThePool(everyIdentityLoads)
}

private func everyIdentityLoads() throws {
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let failures = Notes()
    DispatchQueue.concurrentPerform(iterations: 2) { worker in
        for index in 0..<192 {
            let file = directory.appendingPathComponent("\(worker)-\(index).p12")
            do {
                try TLSIdentity.create(at: file, commonName: "Loki Agent")
                _ = try TLSIdentity.load(file)
            } catch {
                failures.add("\(file.lastPathComponent): \(error)")
            }
        }
    }
    #expect(failures.all.isEmpty, "\(failures.all)")
}
