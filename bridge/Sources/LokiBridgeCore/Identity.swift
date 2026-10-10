import CryptoKit
import Foundation
import Security

/// SHA-256 of a certificate's DER encoding, in lowercase hex: how loki/1 pins peers.
public enum Fingerprint {
    public static func of(_ der: Data) -> String {
        SHA256.hash(data: der).map { String(format: "%02x", $0) }.joined()
    }

    public static func isValid(_ text: String) -> Bool {
        text.utf8.count == 64 && text.utf8.allSatisfy { (0x30...0x39).contains($0) || (0x61...0x66).contains($0) }
    }
}

public struct IdentityError: Error, CustomStringConvertible, Sendable {
    public let description: String
    init(_ description: String) { self.description = description }
}

/// A self-signed ECDSA P-256 TLS identity: the bridge's own, or a simulated board's
/// (RFC-003, docs/loki-protocol.md). It is made once with the system's
/// /usr/bin/openssl and kept as a PKCS#12 file only the user can read, then imported
/// into memory only, never into a keychain. The file's permissions protect the key;
/// the PKCS#12 passphrase is a fixed formality, and voice sessions cannot read the
/// file (ADR-014).
public struct TLSIdentity: @unchecked Sendable {
    public let secIdentity: SecIdentity
    public let certificateDER: Data
    public var fingerprint: String { Fingerprint.of(certificateDER) }

    static let passphrase = "loki-bridge"
    static let openssl = URL(fileURLWithPath: "/usr/bin/openssl")

    public static func loadOrCreate(at file: URL, commonName: String) throws -> TLSIdentity {
        if !FileManager.default.fileExists(atPath: file.path) {
            try create(at: file, commonName: commonName)
        }
        return try load(file)
    }

    public static func load(_ file: URL) throws -> TLSIdentity {
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
        let data = try Data(contentsOf: file)
        var items: CFArray?
        let options = [kSecImportExportPassphrase: passphrase, kSecImportToMemoryOnly: true] as CFDictionary
        let status = SecPKCS12Import(data as CFData, options, &items)
        guard status == errSecSuccess, let entries = items as? [[String: Any]], let entry = entries.first,
              let value = entry[kSecImportItemIdentity as String]
        else { throw IdentityError("cannot import \(file.lastPathComponent) (\(status))") }
        let identity = value as! SecIdentity  // The import result guarantees the type.
        var certificate: SecCertificate?
        guard SecIdentityCopyCertificate(identity, &certificate) == errSecSuccess, let certificate else {
            throw IdentityError("no certificate in \(file.lastPathComponent)")
        }
        return TLSIdentity(secIdentity: identity, certificateDER: SecCertificateCopyData(certificate) as Data)
    }

    /// Writes a new identity to `file` (mode 0600), replacing none: the caller decides.
    public static func create(at file: URL, commonName: String) throws {
        guard !commonName.isEmpty, commonName.utf8.count <= 40,
              commonName.unicodeScalars.allSatisfy({ CharacterSet.alphanumerics.contains($0) || " .-".unicodeScalars.contains($0) })
        else { throw IdentityError("a common name is letters, digits, spaces, dots and dashes") }
        let manager = FileManager.default
        let directory = file.deletingLastPathComponent()
        try manager.createDirectory(at: directory, withIntermediateDirectories: true,
                                    attributes: [.posixPermissions: 0o700])
        let work = manager.temporaryDirectory.appendingPathComponent("loki-identity-\(UUID().uuidString)")
        try manager.createDirectory(at: work, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        defer { try? manager.removeItem(at: work) }
        let key = work.appendingPathComponent("key.pem").path
        let certificate = work.appendingPathComponent("cert.pem").path
        let bundle = work.appendingPathComponent("identity.p12").path
        // The system's LibreSSL writes a P-256 key whose scalar has a leading zero byte
        // (1 in 256) as 31 bytes, which SecPKCS12Import accepts without an identity; such a
        // file could never be loaded. Make another key until the bundle imports.
        for attempt in 1...8 {
            try run(["ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key])
            try run(["req", "-new", "-x509", "-key", key, "-out", certificate, "-days", "36500", "-sha256",
                     "-subj", "/CN=\(commonName)"])
            try run(["pkcs12", "-export", "-inkey", key, "-in", certificate, "-out", bundle, "-name", commonName,
                     "-passout", "pass:\(passphrase)", "-certpbe", "PBE-SHA1-3DES", "-keypbe", "PBE-SHA1-3DES",
                     "-macalg", "sha1"])
            try manager.setAttributes([.posixPermissions: 0o600], ofItemAtPath: bundle)
            if (try? load(URL(fileURLWithPath: bundle))) != nil { break }
            guard attempt < 8 else { throw IdentityError("openssl made no identity that imports") }
        }
        guard !manager.fileExists(atPath: file.path) else { throw IdentityError("\(file.path) already exists") }
        try manager.moveItem(atPath: bundle, toPath: file.path)
    }

    private static func run(_ arguments: [String]) throws {
        let process = Process()
        process.executableURL = openssl
        process.arguments = arguments
        let errors = Pipe()
        process.standardError = errors
        process.standardOutput = FileHandle.nullDevice
        try process.run()
        process.waitUntilExit()
        let message = String(decoding: errors.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        guard process.terminationStatus == 0 else {
            throw IdentityError("openssl \(arguments[0]) failed: \(message.prefix(200))")
        }
    }
}
