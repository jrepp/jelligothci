import Foundation
import LokiProtocol
import Network
import Security

/// Network.framework parameters for loki/1: TLS 1.2 or later, both sides presenting a
/// certificate and accepting only the peer they pinned at pairing, TCP keepalive, and
/// WebSocket with the `loki.1` subprotocol (docs/loki-protocol.md).
enum LinkTLS {
    static let subprotocol = "loki.1"
    /// A control message is at most 2,048 bytes and an audio frame 4,812.
    static let maximumMessageSize = 8192

    /// `accepts` decides on the peer certificate's fingerprint; nothing else is checked.
    static func parameters(identity: TLSIdentity, server: Bool, queue: DispatchQueue,
                           accepts: @escaping @Sendable (String) -> Bool) -> NWParameters {
        let tls = NWProtocolTLS.Options()
        let options = tls.securityProtocolOptions
        sec_protocol_options_set_local_identity(options, sec_identity_create(identity.secIdentity)!)
        sec_protocol_options_set_min_tls_protocol_version(options, .TLSv12)
        if server { sec_protocol_options_set_peer_authentication_required(options, true) }
        sec_protocol_options_set_verify_block(options, { _, trust, complete in
            complete(leafFingerprint(trust).map(accepts) ?? false)
        }, queue)

        let tcp = NWProtocolTCP.Options()
        tcp.enableKeepalive = true
        tcp.keepaliveIdle = 10
        tcp.keepaliveInterval = 5
        tcp.keepaliveCount = 3
        tcp.connectionTimeout = 5

        let websocket = NWProtocolWebSocket.Options()
        websocket.autoReplyPing = true
        websocket.maximumMessageSize = maximumMessageSize
        if server {
            websocket.setClientRequestHandler(queue) { subprotocols, _ in
                subprotocols.contains(subprotocol) ? .init(status: .accept, subprotocol: subprotocol)
                    : .init(status: .reject, subprotocol: nil)
            }
        } else {
            websocket.setSubprotocols([subprotocol])
        }
        let parameters = NWParameters(tls: tls, tcp: tcp)
        parameters.defaultProtocolStack.applicationProtocols.insert(websocket, at: 0)
        return parameters
    }

    static func leafFingerprint(_ trust: sec_trust_t) -> String? {
        let reference = sec_trust_copy_ref(trust).takeRetainedValue()
        guard let chain = SecTrustCopyCertificateChain(reference) as? [SecCertificate], let leaf = chain.first else {
            return nil
        }
        return Fingerprint.of(SecCertificateCopyData(leaf) as Data)
    }

    /// The fingerprint of the certificate the peer presented on a ready connection.
    static func peerFingerprint(_ connection: NWConnection) -> String? {
        guard let metadata = connection.metadata(definition: NWProtocolTLS.definition) as? NWProtocolTLS.Metadata
        else { return nil }
        var leaf: String?
        _ = sec_protocol_metadata_access_peer_certificate_chain(metadata.securityProtocolMetadata) { certificate in
            if leaf == nil {
                leaf = Fingerprint.of(SecCertificateCopyData(sec_certificate_copy_ref(certificate).takeRetainedValue()) as Data)
            }
        }
        return leaf
    }

    static func context(_ opcode: NWProtocolWebSocket.Opcode, closeCode: UInt16? = nil) -> NWConnection.ContentContext {
        let metadata = NWProtocolWebSocket.Metadata(opcode: opcode)
        if let closeCode {
            let defined: NWProtocolWebSocket.CloseCode.Defined = switch closeCode {
            case 1001: .goingAway
            case 1003: .unsupportedData
            case 1008: .policyViolation
            case 1009: .messageTooBig
            case 1011: .internalServerError
            default: .protocolError
            }
            metadata.closeCode = .protocolCode(defined)
        }
        return NWConnection.ContentContext(identifier: "loki", metadata: [metadata])
    }

    static func opcode(_ context: NWConnection.ContentContext?) -> NWProtocolWebSocket.Opcode? {
        (context?.protocolMetadata(definition: NWProtocolWebSocket.definition) as? NWProtocolWebSocket.Metadata)?.opcode
    }
}

/// Runs a body once, for callbacks that may report the same outcome twice. Used only
/// on one serial queue.
final class Once: @unchecked Sendable {
    private var done = false
    func run(_ body: () -> Void) {
        guard !done else { return }
        done = true
        body()
    }
}

/// Seconds on a monotonic clock, for rate limits.
func monotonicSeconds() -> Double {
    Double(DispatchTime.now().uptimeNanoseconds) / 1e9
}
