import Foundation
import Testing
@testable import LokiBridgeCore

// The pairing file's lock and change stamp (memo-027: M4). `loki-bridge pair` and
// `unpair` run in other processes than the bridge, so each change rereads and writes the
// file under an flock on `.devices.json.lock`, and the change stamp includes the ctime.

private let fingerprints = ["c1", "d2", "e3"].map { String(repeating: $0, count: 32) }

private func fingerprintsIn(_ file: URL) throws -> Set<String> {
    Set(try PairingStore(file: file).all.map(\.fingerprint))
}

/// Runs blocking work (locks, threads, child processes) on a GCD thread, so it never
/// holds one of the cooperative threads that the link tests running alongside rely on.
func offThePool<T: Sendable>(_ work: @escaping @Sendable () throws -> T) async throws -> T {
    try await withCheckedThrowingContinuation { done in
        DispatchQueue.global().async { done.resume(with: Result { try work() }) }
    }
}

/// Runs both closures at once on two threads, released together.
private func race(_ first: @escaping @Sendable () -> Void, _ second: @escaping @Sendable () -> Void) {
    let start = DispatchSemaphore(value: 0)
    let group = DispatchGroup()
    for work in [first, second] {
        DispatchQueue.global().async(group: group) {
            start.wait()
            work()
        }
    }
    start.signal()
    start.signal()
    group.wait()
}

/// Regression for the lost update: two stores that both read X, one unpairing X while the
/// other pairs Y, must never leave X paired; two concurrent pairs must both survive.
@Test func concurrentChangesFromTwoStoresAreNeverLost() async throws {
    try await offThePool(concurrentChangesAreNeverLost)
}

private func concurrentChangesAreNeverLost() throws {
    let (x, y, z) = (fingerprints[0], fingerprints[1], fingerprints[2])
    var comebacks = 0, lost = 0
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    for trial in 0..<300 {
        let file = directory.appendingPathComponent("devices-\(trial).json")
        try PairingStore(file: file).pair(PairedDevice(name: "X", fingerprint: x))
        let (a, b) = (try PairingStore(file: file), try PairingStore(file: file))
        #expect(a.device(fingerprint: x) != nil && b.device(fingerprint: x) != nil)
        if trial % 2 == 0 {
            race({ try? a.unpair(fingerprint: x) }, { try? b.pair(PairedDevice(name: "Y", fingerprint: y)) })
            let now = try fingerprintsIn(file)
            if now.contains(x) { comebacks += 1 }
            if !now.contains(y) { lost += 1 }
        } else {
            race({ try? a.pair(PairedDevice(name: "Y", fingerprint: y)) },
                 { try? b.pair(PairedDevice(name: "Z", fingerprint: z)) })
            if try fingerprintsIn(file) != [x, y, z] { lost += 1 }
        }
    }
    #expect(comebacks == 0, "an unpaired board came back in \(comebacks) of 150 trials")
    #expect(lost == 0, "a pairing was lost in \(lost) of 300 trials")
}

/// The lock is honoured across processes: while another process holds it through a
/// slow reread-edit-write that unpairs X, this store's pair waits, then starts from
/// that process's file, so neither change is lost.
@Test func anotherProcessHoldingTheLockIsWaitedFor() async throws {
    try await offThePool(anotherProcessIsWaitedFor)
}

private func anotherProcessIsWaitedFor() throws {
    guard let python = ["/usr/bin/python3", "/opt/homebrew/bin/python3"].first(where: {
        FileManager.default.isExecutableFile(atPath: $0)
    }) else { return }  // no interpreter to hold the lock from another process
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let file = directory.appendingPathComponent("devices.json")
    let (x, y) = (fingerprints[0], fingerprints[1])
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "X", fingerprint: x))
    // Another process: lock, read, say so, take 0.4 s, write the list without X, unlock.
    let script = """
        import fcntl, json, os, sys, time
        lock = os.open(sys.argv[1], os.O_RDWR | os.O_CREAT, 0o600)
        fcntl.flock(lock, fcntl.LOCK_EX)
        devices = [d for d in json.load(open(sys.argv[2])) if d["fingerprint"] != sys.argv[3]]
        print("locked", flush=True)
        time.sleep(0.4)
        open(sys.argv[2] + ".tmp", "w").write(json.dumps(devices))
        os.rename(sys.argv[2] + ".tmp", sys.argv[2])
        fcntl.flock(lock, fcntl.LOCK_UN)
        """
    let process = Process()
    process.executableURL = URL(fileURLWithPath: python)
    process.arguments = ["-I", "-c", script, directory.appendingPathComponent(".devices.json.lock").path, file.path, x]
    let output = Pipe()
    process.standardOutput = output
    try process.run()
    let line = output.fileHandleForReading.availableData
    #expect(String(decoding: line, as: UTF8.self).hasPrefix("locked"))
    let started = ContinuousClock.now
    try store.pair(PairedDevice(name: "Y", fingerprint: y))
    let waited = ContinuousClock.now - started
    process.waitUntilExit()
    #expect(process.terminationStatus == 0)
    #expect(waited >= .milliseconds(250), "the pair did not wait for the other process's lock (\(waited))")
    #expect(try fingerprintsIn(file) == [y])
    #expect(store.device(fingerprint: x) == nil)
}

@Test func theLockFileIsPrivateAndNeverFollowedThroughALink() throws {
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let file = directory.appendingPathComponent("devices.json")
    let lock = directory.appendingPathComponent(".devices.json.lock")
    try PairingStore(file: file).pair(PairedDevice(name: "X", fingerprint: fingerprints[0]))
    let mode = (try FileManager.default.attributesOfItem(atPath: lock.path)[.posixPermissions] as? NSNumber)?.intValue
    #expect(mode == 0o600)
    // A lock planted as a symlink (here to a file that does not exist yet) is refused,
    // creates nothing at its target, and changes nothing.
    try FileManager.default.removeItem(at: lock)
    let target = directory.appendingPathComponent("elsewhere")
    try FileManager.default.createSymbolicLink(at: lock, withDestinationURL: target)
    let store = try PairingStore(file: file)
    #expect(throws: IdentityError.self) { try store.pair(PairedDevice(name: "Y", fingerprint: fingerprints[1])) }
    #expect(throws: IdentityError.self) { try store.unpair(fingerprint: fingerprints[0]) }
    #expect(!FileManager.default.fileExists(atPath: target.path))
    #expect(try fingerprintsIn(file) == [fingerprints[0]])
}

/// Sets a file's modification time to exactly `stamp`'s, nanoseconds included.
private func restoreModified(_ file: URL, to stamp: PairingStore.Stamp) {
    var times = [timespec(tv_sec: 0, tv_nsec: Int(UTIME_OMIT)), stamp.modified]
    #expect(utimensat(AT_FDCWD, file.path, &times, 0) == 0)
}

/// A rewrite in place, of the same size, with the modification time put back, keeps the
/// device, inode, size and mtime: only the ctime, which no program can set back, shows it.
@Test func anInPlaceSameSizeRewriteWithTheTimePutBackIsSeenByItsCtime() throws {
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let file = directory.appendingPathComponent("devices.json")
    let paired = Date(timeIntervalSince1970: 1_790_000_000)
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "X", fingerprint: fingerprints[0], paired: paired))
    let before = try #require(PairingStore.stamp(of: file))
    let other = directory.appendingPathComponent("other.json")
    try PairingStore(file: other).pair(PairedDevice(name: "Y", fingerprint: fingerprints[1], paired: paired))
    let replacement = try Data(contentsOf: other)
    #expect(replacement.count == Int(before.size))
    usleep(20_000)  // so the ctime moves on even on a coarse clock
    let handle = try FileHandle(forWritingTo: file)
    try handle.write(contentsOf: replacement)
    try handle.close()
    restoreModified(file, to: before)
    let after = try #require(PairingStore.stamp(of: file))
    #expect((after.device, after.inode, after.size) == (before.device, before.inode, before.size))
    #expect((after.modified.tv_sec, after.modified.tv_nsec) == (before.modified.tv_sec, before.modified.tv_nsec))
    #expect(store.device(fingerprint: fingerprints[0]) == nil)
    #expect(store.device(fingerprint: fingerprints[1])?.name == "Y")
}

/// A file that cannot be read trusts no one, and is read again once it can be.
@Test func anUnreadableFileTrustsNoOneUntilItIsReadableAgain() throws {
    guard getuid() != 0 else { return }  // root reads any file
    let directory = scratchDirectory()
    defer { try? FileManager.default.removeItem(at: directory) }
    let file = directory.appendingPathComponent("devices.json")
    let store = try PairingStore(file: file)
    try store.pair(PairedDevice(name: "X", fingerprint: fingerprints[0]))
    #expect(chmod(file.path, 0) == 0)
    #expect(store.device(fingerprint: fingerprints[0]) == nil && store.all.isEmpty)
    #expect(store.device(fingerprint: fingerprints[0]) == nil)  // still closed on a second look
    #expect(chmod(file.path, 0o600) == 0)
    #expect(store.device(fingerprint: fingerprints[0])?.name == "X")
}
