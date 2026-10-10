import Foundation
import Testing
@testable import LokiBridgeCore
@testable import LokiProtocol

// Unpairing reaches a running bridge, and strangers cannot hold its door open
// (memo-027: M4 and M5 of the link's security review).

private func isConnected(_ event: LinkEvent) -> Bool { if case .connected = event { true } else { false } }
private func isDisconnected(_ event: LinkEvent) -> Bool { if case .disconnected = event { true } else { false } }
private func isText(_ event: RawBoard.Received) -> Bool { if case .text = event { true } else { false } }

/// A listener whose pairing file another store (as `loki-bridge unpair` would, from
/// its own process) can change underneath it.
private func serverWithSharedFile() async throws -> (LinkServer, UInt16, other: PairingStore) {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "Loki Agent", fingerprint: Fixture.board.fingerprint))
    let server = LinkServer(identity: Fixture.bridge, pairing: store, port: 0) {
        .welcome(bridge: "Loki Bridge test", voice: "Scar")
    }
    return (server, try await server.start(), try PairingStore(file: file))
}

private func greet(_ board: RawBoard) async -> Bool {
    board.send(Fixture.hello, seq: 1)
    return await board.log.wait(isText) != nil
}

// MARK: The store follows its file

@Test func anotherStoresUnpairIsSeenAtOnceAndNeverUndone() throws {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    let (x, y) = (String(repeating: "aa", count: 32), String(repeating: "bb", count: 32))
    let bridge = try PairingStore(file: file)
    try bridge.pair(PairedDevice(name: "X", fingerprint: x))
    let command = try PairingStore(file: file)
    #expect(command.device(fingerprint: x) != nil)
    try command.unpair(fingerprint: x)
    #expect(bridge.device(fingerprint: x) == nil)
    // A change made by the first store starts from the file, so X stays unpaired.
    try bridge.pair(PairedDevice(name: "Y", fingerprint: y))
    #expect(try PairingStore(file: file).all.map(\.fingerprint) == [y])
    #expect(command.all.map(\.fingerprint) == [y])
}

@Test func aPairingFileThatBreaksOrGoesTrustsNoOne() throws {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    let x = String(repeating: "cd", count: 32)
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "X", fingerprint: x))
    let good = try Data(contentsOf: file)
    try Data("not json".utf8).write(to: file)
    #expect(store.device(fingerprint: x) == nil && store.all.isEmpty)
    try good.write(to: file)
    #expect(store.device(fingerprint: x) != nil)
    try Data(#"[{"name":"x","fingerprint":"zz","paired":"2026-10-09T00:00:00Z"}]"#.utf8).write(to: file)
    #expect(store.all.isEmpty)
    try FileManager.default.removeItem(at: file)
    #expect(store.device(fingerprint: x) == nil)
    // A store whose file broke still pairs: it starts again from what is there.
    #expect(throws: (any Error).self) {
        try Data("broken".utf8).write(to: file)
        try store.pair(PairedDevice(name: "X", fingerprint: x))
    }
    try FileManager.default.removeItem(at: file)
    try store.pair(PairedDevice(name: "X", fingerprint: x))
    #expect(store.all.count == 1)
}

// MARK: A running bridge lets go of an unpaired board

@Test func anUnpairedBoardIsDroppedAtItsNextMessage() async throws {
    let (server, port, other) = try await serverWithSharedFile()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    try other.unpair(fingerprint: Fixture.board.fingerprint)
    board.send(.pttStart(stream: 1), seq: 2)
    #expect(await board.closeCode() == .some(1008))
    guard case .disconnected(let reason)? = await events.wait(isDisconnected) else {
        Issue.record("still connected")
        return
    }
    #expect(reason.contains("unpaired"))
    #expect(await events.events.contains { if case .message = $0 { true } else { false } } == false)
}

@Test func anUnpairedIdleBoardIsDroppedByTheTimer() async throws {
    let (server, port, other) = try await serverWithSharedFile()
    defer { server.stop() }
    server.pingInterval = 0.15
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    try other.unpair(fingerprint: Fixture.board.fingerprint)
    #expect(await board.closeCode() == .some(1008))
    #expect(await events.wait(isDisconnected) != nil)
}

@Test func anUnpairedBoardCannotComeBack() async throws {
    let (server, port, other) = try await serverWithSharedFile()
    defer { server.stop() }
    try other.unpair(fingerprint: Fixture.board.fingerprint)
    let board = RawBoard(port: port)
    await #expect(throws: (any Error).self) { try await board.connect() }
}

// MARK: Handshakes are few and short

/// A plain TCP connection that never starts TLS, as a stranger would hold one.
private final class Stranger {
    let fd: Int32

    /// From 127.0.0.1, or from ::1 when `ipv6`, so a test can have two addresses.
    init(port: UInt16, ipv6: Bool = false) {
        fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0)
        if ipv6 {
            var address = sockaddr_in6()
            address.sin6_len = UInt8(MemoryLayout<sockaddr_in6>.size)
            address.sin6_family = sa_family_t(AF_INET6)
            address.sin6_port = port.bigEndian
            address.sin6_addr = in6addr_loopback
            _ = withUnsafePointer(to: &address) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                    connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in6>.size))
                }
            }
        } else {
            var address = sockaddr_in()
            address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
            address.sin_family = sa_family_t(AF_INET)
            address.sin_port = port.bigEndian
            address.sin_addr.s_addr = inet_addr("127.0.0.1")
            _ = withUnsafePointer(to: &address) {
                $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                    connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
                }
            }
        }
    }

    deinit { close(fd) }

    /// Whether the bridge has ended this connection.
    var ended: Bool {
        var byte: UInt8 = 0
        let count = recv(fd, &byte, 1, MSG_DONTWAIT | MSG_PEEK)
        return count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
    }
}

@Test func aHandshakeThatNeverStartsIsEndedAtItsDeadline() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.handshakeTimeout = 0.5
    server.maxPendingPerHost = 8  // three from one address, all under their deadline
    let strangers = (0..<3).map { _ in Stranger(port: port) }
    try await Task.sleep(for: .milliseconds(150))
    #expect(strangers.allSatisfy { !$0.ended })
    #expect(await waitUntil(timeout: .seconds(5)) { strangers.allSatisfy(\.ended) })
}

@Test func manyStrangersCannotKeepThePairedBoardOut() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let events = collect(server.events)
    let strangers = (0..<20).map { _ in Stranger(port: port) }
    // Beyond eight handshakes in progress, the oldest are ended at once.
    #expect(await waitUntil { strangers.filter(\.ended).count >= 12 })
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    #expect(strangers.filter { !$0.ended }.count <= 8)
}

// MARK: The store's change detection, file kinds, and many stores

/// Sets a file's modification time to exactly `stamp`'s, nanoseconds included.
private func setModified(_ file: URL, like stamp: PairingStore.Stamp) {
    var times = [timespec(tv_sec: 0, tv_nsec: Int(UTIME_OMIT)),
                 stamp.modified]
    #expect(utimensat(AT_FDCWD, file.path, &times, 0) == 0)
}

@Test func aSameSizeSameTimeRewriteIsSeenByItsNewInode() throws {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    let (x, y) = (String(repeating: "1a", count: 32), String(repeating: "2b", count: 32))
    let paired = Date(timeIntervalSince1970: 1_790_000_000)
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "X", fingerprint: x, paired: paired))
    let before = try #require(PairingStore.stamp(of: file))
    // Another process writes a file of the same size by rename, then restores the time.
    let other = scratchDirectory().appendingPathComponent("devices.json")
    try PairingStore(file: other).pair(PairedDevice(name: "Y", fingerprint: y, paired: paired))
    _ = try FileManager.default.replaceItemAt(file, withItemAt: other)
    setModified(file, like: before)
    let after = try #require(PairingStore.stamp(of: file))
    #expect(after.size == before.size && after.modified.tv_sec == before.modified.tv_sec
        && after.modified.tv_nsec == before.modified.tv_nsec)
    #expect(after.inode != before.inode)
    #expect(store.device(fingerprint: x) == nil)
    #expect(store.device(fingerprint: y)?.name == "Y")
}

@Test func aPairingFileThatBecomesADirectoryTrustsNoOneAndCannotBeWritten() throws {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    let x = String(repeating: "3c", count: 32)
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "X", fingerprint: x))
    try FileManager.default.removeItem(at: file)
    try FileManager.default.createDirectory(at: file, withIntermediateDirectories: false)
    #expect(store.device(fingerprint: x) == nil && store.all.isEmpty)
    #expect(throws: (any Error).self) { try store.pair(PairedDevice(name: "X", fingerprint: x)) }
    #expect(throws: (any Error).self) { try store.unpair(fingerprint: x) }
    #expect(throws: (any Error).self) { try PairingStore(file: file) }
    var isDirectory: ObjCBool = false
    #expect(FileManager.default.fileExists(atPath: file.path, isDirectory: &isDirectory) && isDirectory.boolValue)
    #expect(store.device(fingerprint: x) == nil)
}

@Test func aPairingFileReplacedBySymlinksFollowsTheTargetAndFailsClosedWhenDangling() throws {
    let directory = scratchDirectory()
    let file = directory.appendingPathComponent("devices.json")
    let (x, y) = (String(repeating: "4d", count: 32), String(repeating: "5e", count: 32))
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "X", fingerprint: x))
    let elsewhere = directory.appendingPathComponent("elsewhere.json")
    try PairingStore(file: elsewhere).pair(PairedDevice(name: "Y", fingerprint: y))
    try FileManager.default.removeItem(at: file)
    try FileManager.default.createSymbolicLink(at: file, withDestinationURL: elsewhere)
    #expect(store.device(fingerprint: x) == nil)
    #expect(store.device(fingerprint: y)?.name == "Y")
    try FileManager.default.removeItem(at: elsewhere)  // now dangling
    #expect(store.device(fingerprint: y) == nil && store.all.isEmpty)
    let link = directory.appendingPathComponent("loop.json")
    try FileManager.default.createSymbolicLink(at: link, withDestinationURL: link)  // a loop
    #expect(try PairingStore(file: link).all.isEmpty)
}

/// Model check: three stores on one file take seeded turns pairing and unpairing four
/// boards; each sees every earlier change, and none is lost or undone.
@Test(arguments: 0..<6)
func storesTakingTurnsAgreeWithAModel(_ seed: Int) throws {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    let boards = ["6f", "70", "81", "92"].map { String(repeating: $0, count: 32) }
    let stores = try (0..<3).map { _ in try PairingStore(file: file) }
    var model = Set<String>()
    var state = UInt64(seed + 1) &* 0x2545_F491_4F6C_DD1D
    func below(_ bound: Int) -> Int {
        state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
        return Int((state >> 33) % UInt64(bound))
    }
    for step in 0..<60 {
        let (store, board) = (stores[below(3)], boards[below(4)])
        if below(2) == 0 {
            try store.pair(PairedDevice(name: "b\(step)", fingerprint: board))
            model.insert(board)
        } else {
            try store.unpair(fingerprint: board)
            model.remove(board)
        }
        // Reads are occasional, so a store often writes without having looked first.
        guard below(4) == 0 || step == 59 else { continue }
        for reader in stores {
            #expect(Set(reader.all.map(\.fingerprint)) == model, "seed \(seed) step \(step)")
            #expect(reader.all.count == model.count, "seed \(seed) step \(step): duplicates")
        }
    }
}

@Test func manyReadersAndOneWriterOnOneStoreStayConsistent() throws {
    let file = scratchDirectory().appendingPathComponent("devices.json")
    let x = String(repeating: "a7", count: 32)
    let store = try PairingStore(file: file)
    let writer = try PairingStore(file: file)
    let torn = Flag()
    DispatchQueue.concurrentPerform(iterations: 9) { worker in
        for round in 0..<40 {
            if worker == 0 {
                if round % 2 == 0 { try? writer.pair(PairedDevice(name: "X", fingerprint: x)) }
                else { try? writer.unpair(fingerprint: x) }
            } else {
                // A reader sees X paired or not, never a broken or doubled list.
                let all = store.all
                if all.count > 1 || all.contains(where: { $0.fingerprint != x }) { torn.set() }
            }
        }
    }
    #expect(!torn.isSet)
    #expect(store.all.isEmpty)  // the writer's last turn unpaired
}

// MARK: The link after these changes

@Test func aBoardUnpairedBeforeHelloIsNeverConnected() async throws {
    let (server, port, other) = try await serverWithSharedFile()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    try other.unpair(fingerprint: Fixture.board.fingerprint)
    board.send(Fixture.hello, seq: 1)
    #expect(await board.closeCode() == .some(1008))
    #expect(await events.wait(timeout: .milliseconds(300), isConnected) == nil)
    #expect(await board.log.events.contains(where: isText) == false)  // no welcome
    #expect(server.send(.stopAudio) == false)
}

@Test func anUnpairedBoardCannotFinishTheUtteranceItStarted() async throws {
    let (server, port, other) = try await serverWithSharedFile()
    defer { server.stop() }
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    board.send(.pttStart(stream: 1), seq: 2)
    let frame = { (seq: UInt32) in AudioFrame(kind: .mic, final: false, stream: 1, seq: seq, payload: [1, 0]).bytes }
    board.send(binary: frame(0))
    #expect(await events.wait { if case .audio = $0 { true } else { false } } != nil)
    try other.unpair(fingerprint: Fixture.board.fingerprint)
    // Frames of the open utterance are not checked one by one (the stream was opened
    // while paired, and is rate limited); its end, a control message, is.
    board.send(binary: frame(1))
    board.send(.pttEnd(stream: 1, frames: 2, dropped: 0), seq: 3)
    #expect(await board.closeCode() == .some(1008))
    #expect(await events.wait(isDisconnected) != nil)
    let all = await events.events
    #expect(!all.contains { if case .message(.pttEnd) = $0 { true } else { false } })
    #expect(all.filter { if case .audio = $0 { true } else { false } }.count <= 2)
}

@Test func pairingAgainLetsTheSameBoardBackIn() async throws {
    let (server, port, other) = try await serverWithSharedFile()
    defer { server.stop() }
    try other.unpair(fingerprint: Fixture.board.fingerprint)
    await #expect(throws: (any Error).self) { try await RawBoard(port: port).connect() }
    try other.pair(PairedDevice(name: "Loki Agent again", fingerprint: Fixture.board.fingerprint))
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    guard case .connected(let device)? = await events.wait(isConnected) else {
        Issue.record("not connected")
        return
    }
    #expect(device.paired.name == "Loki Agent again")
}

@Test func theHandshakeDeadlineSparesAConnectionThatBecameReady() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    server.handshakeTimeout = 0.3
    let events = collect(server.events)
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    try await Task.sleep(for: .milliseconds(600))  // twice the deadline
    #expect(server.send(.stopAudio))
    let stop = await board.log.wait { if case .text(let bytes) = $0 { String(decoding: bytes, as: UTF8.self).contains("stop_audio") } else { false } }
    #expect(stop != nil)
    #expect(await events.events.contains(where: isDisconnected) == false)
    #expect(await board.log.events.contains { if case .close = $0 { true } else { false } } == false)
}

@Test func strangersBelowTheCapAreLeftAloneAndTheNextEndsTheOldest() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let notes = Notes()
    server.diagnostics = { notes.add($0) }
    server.maxPendingPerHost = 16  // the overall limit alone: every stranger shares the board's address
    let events = collect(server.events)
    // Seven strangers and a board make eight handshakes at once: none is ended.
    var strangers: [Stranger] = []
    for _ in 0..<7 {
        strangers.append(Stranger(port: port))
        try await Task.sleep(for: .milliseconds(20))  // so their ages differ
    }
    let board = RawBoard(port: port)
    try await board.connect()
    #expect(await greet(board))
    #expect(await events.wait(isConnected) != nil)
    #expect(strangers.allSatisfy { !$0.ended })
    // The board is past its handshake, so seven are pending: two more end one, the oldest.
    strangers.append(Stranger(port: port))
    strangers.append(Stranger(port: port))
    #expect(await waitUntil { strangers[0].ended })
    try await Task.sleep(for: .milliseconds(100))
    #expect(strangers.dropFirst().allSatisfy { !$0.ended })
    #expect(notes.all.filter { $0.contains("too many handshakes") }.count == 1)
    #expect(server.send(.stopAudio))  // the board is untouched
}

@Test func aFloodFromOneAddressEndsOnlyItsOwnHandshakes() async throws {
    let (server, port) = try await Fixture.server()
    defer { server.stop() }
    let notes = Notes()
    server.diagnostics = { notes.add($0) }
    let events = collect(server.events)
    // A stranger on ::1 keeps opening handshakes while the board, on 127.0.0.1, connects.
    let flooding = Flag()
    let flood = Task.detached {
        // Paced and bounded, so tests running alongside keep their sockets and ports.
        var held: [Stranger] = []
        for _ in 0..<400 where !flooding.isSet {
            held.append(Stranger(port: port, ipv6: true))
            if held.count > 32 { held.removeFirst(16) }
            usleep(1_000)
        }
    }
    for _ in 0..<5 {
        let board = RawBoard(port: port)
        try await board.connect()
        #expect(await greet(board))
        board.close()
    }
    flooding.set()
    await flood.value
    #expect(await events.events.filter(isConnected).count == 5)
    let ended = notes.all.filter { $0.contains("too many handshakes") }
    #expect(!ended.isEmpty)
    #expect(ended.allSatisfy { $0.contains("::1") })
}
