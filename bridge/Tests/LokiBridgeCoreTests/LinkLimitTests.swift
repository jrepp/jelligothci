import Foundation
import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

// The listener's handshake limits by address, and the send paths' pairing check
// (memo-027: M4 and M5). Strangers are few and paced, so parallel tests keep their ports.

private func isConnected(_ event: LinkEvent) -> Bool { if case .connected = event { true } else { false } }
private func isText(_ event: RawBoard.Received) -> Bool { if case .text = event { true } else { false } }

/// A TCP connection that never starts TLS, from 127.0.0.1 or (ipv6) ::1.
private final class Idle: @unchecked Sendable {
    let fd: Int32

    init(port: UInt16, ipv6: Bool) {
        fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0)
        if ipv6 {
            var address = sockaddr_in6()
            address.sin6_len = UInt8(MemoryLayout<sockaddr_in6>.size)
            address.sin6_family = sa_family_t(AF_INET6)
            address.sin6_port = port.bigEndian
            address.sin6_addr = in6addr_loopback
            _ = withUnsafePointer(to: &address) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in6>.size)) }
            }
        } else {
            var address = sockaddr_in()
            address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
            address.sin_family = sa_family_t(AF_INET)
            address.sin_port = port.bigEndian
            address.sin_addr.s_addr = inet_addr("127.0.0.1")
            _ = withUnsafePointer(to: &address) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) }
            }
        }
    }

    deinit { close(fd) }

    var ended: Bool {
        var byte: UInt8 = 0
        let count = recv(fd, &byte, 1, MSG_DONTWAIT | MSG_PEEK)
        return count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    }
}

/// Opens strangers one at a time, far enough apart that their ages are distinct and the
/// listener has accepted each before the next.
private func open(_ port: UInt16, ipv6: [Bool]) async throws -> [Idle] {
    var opened: [Idle] = []
    for v6 in ipv6 {
        opened.append(Idle(port: port, ipv6: v6))
        try await Task.sleep(for: .milliseconds(40))
    }
    return opened
}

@Test func aThirdHandshakeFromOneAddressEndsOnlyThatAddresssOldest() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let notes = Notes()
    server.diagnostics = { notes.add($0) }
    // Two from each address are at the per-address limit and under the overall one.
    let held = try await open(port, ipv6: [false, true, false, true])
    try await Task.sleep(for: .milliseconds(100))
    #expect(held.allSatisfy { !$0.ended })
    let third = try await open(port, ipv6: [false])
    #expect(await waitUntil { held[0].ended })
    try await Task.sleep(for: .milliseconds(150))
    #expect(!held[1].ended && !held[2].ended && !held[3].ended && !third[0].ended)
    let ended = notes.all.filter { $0.contains("too many handshakes") }
    #expect(ended.count == 1 && ended.allSatisfy { $0.contains("127.0.0.1") }, "\(ended)")
}

@Test func atTheOverallLimitTheBusiestAddressLosesItsOldest() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.maxPending = 4
    server.maxPendingPerHost = 3
    let notes = Notes()
    server.diagnostics = { notes.add($0) }
    // ::1 holds the oldest handshake, 127.0.0.1 the next three: four, the overall limit.
    var held = try await open(port, ipv6: [true, false, false, false])
    try await Task.sleep(for: .milliseconds(100))
    #expect(held.allSatisfy { !$0.ended })
    // A second from ::1 is under its own limit; the busiest address, 127.0.0.1, gives
    // way, though ::1 holds the oldest handshake of all.
    held += try await open(port, ipv6: [true])
    #expect(await waitUntil { held[1].ended })
    #expect(!held[0].ended)
    // Now two each: a tie, so the oldest of all ends, the first from ::1.
    held += try await open(port, ipv6: [false])
    #expect(await waitUntil { held[0].ended })
    try await Task.sleep(for: .milliseconds(150))
    #expect(held.enumerated().allSatisfy { $0.offset < 2 || !$0.element.ended }, "\(held.map(\.ended))")
    let ended = notes.all.filter { $0.contains("too many handshakes") }
    #expect(ended.count == 2 && ended[0].contains("127.0.0.1") && ended[1].contains("::1"), "\(ended)")
}

/// A server whose pairing file a second store can change underneath it.
private func sharedServer() async throws -> (LinkServer, UInt16, PairingStore) {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    try PairingStore(file: file).pair(PairedDevice(name: "Loki Agent", fingerprint: Fixture.board.fingerprint))
    let server = LinkServer(identity: Fixture.bridge, pairing: try PairingStore(file: file), port: 0) {
        .welcome(bridge: "Loki Bridge test", voice: "Scar")
    }
    return (server, try await server.start(), try PairingStore(file: file))
}

@Test(arguments: ["send", "sendAudio"])
func sendingToABoardUnpairedSinceItConnectedDropsIt(_ path: String) async throws {
    let (server, port, other) = try await sharedServer()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    board.send(Fixture.hello, seq: 1)
    #expect(await board.log.wait(isText) != nil)
    #expect(await events.wait(isConnected) != nil)
    try other.unpair(fingerprint: Fixture.board.fingerprint)
    // Nothing reaches the board after its unpairing; the send path itself closes it.
    let sent = path == "send" ? server.send(.stopAudio)
        : server.sendAudio(AudioFrame(kind: .reply, final: true, stream: 1, seq: 0, payload: [0, 0]))
    #expect(sent == false)
    #expect(await board.closeCode() == .some(1008))
    guard case .disconnected(let reason)? = await events.wait({ if case .disconnected = $0 { true } else { false } }) else {
        Issue.record("still connected")
        return
    }
    #expect(reason.contains("unpaired"))
    let texts = await board.log.events.filter(isText).count
    #expect(texts == 1)  // the welcome only
    #expect(await board.log.events.contains { if case .binary = $0 { true } else { false } } == false)
    #expect(server.send(.stopAudio) == false)
}
