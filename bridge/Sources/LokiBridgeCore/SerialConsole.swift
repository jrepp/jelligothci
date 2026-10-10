import Darwin
import Foundation

public struct ConsoleError: Error, CustomStringConvertible, Sendable {
    public let description: String
    init(_ description: String) { self.description = description }
}

/// The board's @A1 USB console (RFC-003): requests "@A1 <id> words\n", replies
/// "\n@A1 <id> {json}\n" among log lines. The port is the board's own USB Serial/JTAG.
public final class SerialConsole {
    private let descriptor: Int32
    private var sequence = UInt32.random(in: 1..<UInt32.max)
    private var pending: [UInt8] = []

    public init(path: String) throws {
        descriptor = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard descriptor >= 0 else { throw ConsoleError("cannot open \(path): \(String(cString: strerror(errno)))") }
        var options = termios()
        guard tcgetattr(descriptor, &options) == 0 else {
            close(descriptor)
            throw ConsoleError("\(path) is not a serial port")
        }
        cfmakeraw(&options)
        options.c_cflag |= tcflag_t(CLOCAL | CREAD)
        cfsetspeed(&options, speed_t(B115200))
        _ = tcsetattr(descriptor, TCSANOW, &options)
        // Keep DTR and RTS asserted, as the Python tools do: on macOS the native USB
        // Serial/JTAG resets the board when they drop. TIOCMBIS is _IOW('t', 108, int).
        var lines: Int32 = TIOCM_DTR | TIOCM_RTS
        _ = ioctl(descriptor, 0x8004_746C, &lines)
    }

    deinit { close(descriptor) }

    /// Sends one request and returns its JSON reply; throws when the board says no.
    public func request(_ words: String, timeout: Double = 3) throws -> [String: Any] {
        guard words.utf8.count <= 220, words.utf8.allSatisfy({ (0x20...0x7E).contains($0) }) else {
            throw ConsoleError("a request is at most 220 printable ASCII characters")
        }
        sequence = sequence % 0xFFFF_FFFF + 1
        let prefix = "@A1 \(sequence) "
        try write(Array((prefix + words + "\n").utf8))
        let deadline = Date().addingTimeInterval(timeout)
        while Date() < deadline {
            while let newline = pending.firstIndex(of: 0x0A) {
                let line = Array(pending[..<newline])
                pending.removeSubrange(...newline)
                if line.starts(with: Array(prefix.utf8)) { return try parse(Array(line.dropFirst(prefix.utf8.count))) }
            }
            try readAvailable(until: deadline)
        }
        throw ConsoleError("no reply to \"\(words.split(separator: " ").prefix(2).joined(separator: " "))\"; is Loki Agent running?")
    }

    private func write(_ bytes: [UInt8]) throws {
        var offset = 0
        while offset < bytes.count {
            let written = bytes[offset...].withUnsafeBytes { Darwin.write(descriptor, $0.baseAddress, $0.count) }
            if written < 0 && errno != EAGAIN { throw ConsoleError("write failed: \(String(cString: strerror(errno)))") }
            if written > 0 { offset += written } else { usleep(1000) }
        }
    }

    private func readAvailable(until deadline: Date) throws {
        var poller = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
        let wait = Int32(max(1, min(100, deadline.timeIntervalSinceNow * 1000)))
        guard poll(&poller, 1, wait) > 0 else { return }
        var buffer = [UInt8](repeating: 0, count: 4096)
        let count = read(descriptor, &buffer, buffer.count)
        if count < 0 && errno != EAGAIN { throw ConsoleError("read failed: \(String(cString: strerror(errno)))") }
        if count > 0 { pending += buffer[..<count] }
        if pending.count > 65536 { pending.removeFirst(pending.count - 4096) }  // Endless logs without a reply.
    }

    private func parse(_ body: [UInt8]) throws -> [String: Any] {
        guard let reply = try? JSONSerialization.jsonObject(with: Data(body)) as? [String: Any],
              let ok = reply["ok"] as? Bool
        else { throw ConsoleError("damaged reply") }
        guard ok else { throw ConsoleError("the board said: \(reply["error"] as? String ?? "error")") }
        return reply
    }
}
