import Foundation

/// A board the bridge accepts: its certificate's fingerprint, pinned at pairing.
public struct PairedDevice: Codable, Equatable, Sendable {
    public var name: String
    public var fingerprint: String
    public var paired: Date

    public init(name: String, fingerprint: String, paired: Date = Date()) {
        self.name = name
        self.fingerprint = fingerprint
        self.paired = paired
    }
}

/// The paired boards, kept as JSON (mode 0600) in the bridge's support folder. A
/// connection whose certificate is not here fails its TLS handshake.
///
/// The file is the truth: `loki-bridge unpair` runs in another process, so every
/// lookup first rereads the file if it has changed (its inode, size or modification
/// time), and a running bridge stops trusting a board as soon as it is unpaired. A
/// file that can no longer be read trusts no one until it can (fail closed). Changes
/// hold an exclusive lock on a sibling `.lock` file from reread to write, so two
/// processes pairing and unpairing at once cannot undo each other.
public final class PairingStore: @unchecked Sendable {
    private let file: URL
    private let lock = NSLock()
    private var devices: [PairedDevice]
    /// What the file looked like when `devices` was read; nil when it did not exist.
    private var stamp: Stamp?

    /// The change time is part of it because no program can set it back.
    struct Stamp: Equatable {
        let device: dev_t, inode: ino_t, size: off_t, modified: timespec, changed: timespec

        static func == (a: Stamp, b: Stamp) -> Bool {
            (a.device, a.inode, a.size) == (b.device, b.inode, b.size)
                && (a.modified.tv_sec, a.modified.tv_nsec, a.changed.tv_sec, a.changed.tv_nsec)
                == (b.modified.tv_sec, b.modified.tv_nsec, b.changed.tv_sec, b.changed.tv_nsec)
        }
    }

    public init(file: URL) throws {
        self.file = file
        (devices, stamp) = ([], nil)
        if let stamp = Self.stamp(of: file) {
            devices = try Self.read(file)
            self.stamp = stamp
        }
    }

    public var all: [PairedDevice] { lock.withLock { refresh(); return devices } }

    public func device(fingerprint: String) -> PairedDevice? {
        lock.withLock {
            refresh()
            return devices.first { $0.fingerprint == fingerprint }
        }
    }

    /// Adds a board, replacing any entry with the same fingerprint.
    public func pair(_ device: PairedDevice) throws {
        guard Fingerprint.isValid(device.fingerprint) else { throw IdentityError("invalid fingerprint") }
        try change {
            devices.removeAll { $0.fingerprint == device.fingerprint }
            devices.append(device)
        }
    }

    public func unpair(fingerprint: String) throws {
        try change { devices.removeAll { $0.fingerprint == fingerprint } }
    }

    /// Rereads, edits and writes the file under both this store's lock and the file lock.
    private func change(_ edit: () -> Void) throws {
        try lock.withLock {
            let directory = file.deletingLastPathComponent()
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true,
                                                    attributes: [.posixPermissions: 0o700])
            let path = directory.appendingPathComponent(".\(file.lastPathComponent).lock").path
            let descriptor = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0o600)
            guard descriptor >= 0 else { throw IdentityError("cannot lock \(path)") }
            defer { close(descriptor) }
            guard flock(descriptor, LOCK_EX) == 0 else { throw IdentityError("cannot lock \(path)") }
            defer { flock(descriptor, LOCK_UN) }
            try reload()
            edit()
            do {
                try save()
            } catch {
                try? reload()  // What is on disk, not an edit that never got there.
                throw error
            }
        }
    }

    // MARK: The file (called with the lock held)

    /// Rereads the file when it has changed since it was read; on failure, trusts no one.
    private func refresh() {
        let now = Self.stamp(of: file)
        guard now != stamp else { return }
        do {
            devices = now == nil ? [] : try Self.read(file)
            stamp = now
        } catch {
            devices = []
            stamp = nil  // retried at the next lookup
        }
    }

    /// The file as it is now, before a change is written over it.
    private func reload() throws {
        let now = Self.stamp(of: file)
        devices = now == nil ? [] : try Self.read(file)
        stamp = now
    }

    private static func read(_ file: URL) throws -> [PairedDevice] {
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        let devices = try decoder.decode([PairedDevice].self, from: Data(contentsOf: file))
        guard devices.allSatisfy({ Fingerprint.isValid($0.fingerprint) }) else {
            throw IdentityError("\(file.lastPathComponent) holds an invalid fingerprint")
        }
        return devices
    }

    static func stamp(of file: URL) -> Stamp? {
        var info = stat()
        guard stat(file.path, &info) == 0 else { return nil }
        return Stamp(device: info.st_dev, inode: info.st_ino, size: info.st_size, modified: info.st_mtimespec,
                     changed: info.st_ctimespec)
    }

    /// Called with the lock held.
    private func save() throws {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .iso8601
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let directory = file.deletingLastPathComponent()
        let temporary = directory.appendingPathComponent(".\(file.lastPathComponent).\(UUID().uuidString)")
        guard FileManager.default.createFile(atPath: temporary.path, contents: try encoder.encode(devices),
                                             attributes: [.posixPermissions: 0o600])
        else { throw IdentityError("cannot write \(temporary.path)") }
        if FileManager.default.fileExists(atPath: file.path) {
            _ = try FileManager.default.replaceItemAt(file, withItemAt: temporary)
        } else {
            try FileManager.default.moveItem(at: temporary, to: file)
        }
        stamp = Self.stamp(of: file)
    }
}
