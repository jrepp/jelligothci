import Foundation
import SystemConfiguration

/// Where the bridge keeps its identity and paired boards: readable only by the user,
/// and outside what voice sessions may read (ADR-014).
public enum BridgeFiles {
    public static var directory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Loki Bridge", isDirectory: true)
    }
    public static var identity: URL { directory.appendingPathComponent("identity.p12") }
    public static var devices: URL { directory.appendingPathComponent("devices.json") }

    /// This Mac's Bonjour name, which the board resolves over mDNS, so a new DHCP
    /// address does not break the link.
    public static var localHostName: String? {
        (SCDynamicStoreCopyLocalHostName(nil) as String?).map { "\($0).local" }
    }

    /// This Mac's IPv4 address on its first active Ethernet or Wi-Fi interface: the
    /// board's fallback when the name does not resolve.
    public static var primaryIPv4: String? {
        var list: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&list) == 0, let first = list else { return nil }
        defer { freeifaddrs(list) }
        for entry in sequence(first: first, next: { $0.pointee.ifa_next }) {
            let flags = Int32(entry.pointee.ifa_flags)
            guard let address = entry.pointee.ifa_addr, address.pointee.sa_family == UInt8(AF_INET),
                  flags & IFF_UP != 0, flags & IFF_LOOPBACK == 0,
                  String(cString: entry.pointee.ifa_name).hasPrefix("en")
            else { continue }
            var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
            if getnameinfo(address, socklen_t(address.pointee.sa_len), &host, socklen_t(host.count), nil, 0,
                           NI_NUMERICHOST) == 0 {
                return String(decoding: host.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
            }
        }
        return nil
    }
}

/// Pairing over USB (RFC-003): the board hands over its certificate's fingerprint
/// and receives the bridge's certificate and address, all over its own console.
/// Physical access already means full access to the board's flash, so the cable is
/// the trust.
public enum USBPairing {
    public struct Result: Sendable {
        public let device: PairedDevice
        public let host: String
        public let port: UInt16
    }

    public static func pair(console: SerialConsole, identity: TLSIdentity, store: PairingStore,
                            host: String, port: UInt16) throws -> Result {
        let hello = try console.request("hello")
        guard hello["app"] as? String == "agent" else { throw ConsoleError("that is not Loki Agent; switch the board to it first") }
        let shown = try console.request("pair show")
        guard let name = shown["name"] as? String, let fingerprint = shown["fingerprint"] as? String,
              Fingerprint.isValid(fingerprint)
        else { throw ConsoleError("the board did not give a certificate fingerprint") }
        _ = try console.request("pair begin")
        let der = [UInt8](identity.certificateDER)
        var offset = 0
        while offset < der.count {
            let part = der[offset..<min(der.count, offset + 96)]
            _ = try console.request("pair cert \(offset) " + part.map { String(format: "%02x", $0) }.joined())
            offset += part.count
        }
        let fallback = BridgeFiles.primaryIPv4.map { " \($0)" } ?? ""
        _ = try console.request("pair commit \(host) \(port) \(identity.fingerprint)\(fallback)")
        let device = PairedDevice(name: LokiDeviceName.clean(name), fingerprint: fingerprint)
        try store.pair(device)
        return Result(device: device, host: host, port: port)
    }
}

enum LokiDeviceName {
    /// The board's own name, kept printable and short for the bridge's lists.
    static func clean(_ name: String) -> String {
        let printable = name.unicodeScalars.filter { (0x20...0x7E).contains($0.value) }
        return String(String.UnicodeScalarView(printable)).prefix(40).description
    }
}
