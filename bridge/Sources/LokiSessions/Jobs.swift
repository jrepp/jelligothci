import Foundation

/// Where a piece of work runs: one project's Claude Code session, or the general
/// session rooted at the user's projects folder for everything else (RFC-003).
public enum Place: Equatable, Hashable, Sendable {
    case general
    case project(Project)

    /// How Loki says it.
    public var name: String {
        switch self {
        case .general: "General"
        case .project(let project): project.name
        }
    }
}

extension Project: Hashable {
    public func hash(into hasher: inout Hasher) { hasher.combine(url.path) }
}

/// One request the user gave Claude, from the words to the spoken summary.
public struct Job: Equatable, Sendable {
    public enum Status: Equatable, Sendable {
        case running, asking
        case done(ok: Bool)
        case stopped
    }

    public let id: Int
    public let place: Place
    public let session: String
    public let request: String
    public let started: Double
    public var status: Status = .running
    /// The latest progress line or tool caption.
    public var progress = ""
    /// What Claude said for the user (the speak tool), else a sentence of its result.
    public var summary = ""
    public var finished: Double?

    public var isActive: Bool { status == .running || status == .asking }
}

/// The jobs Loki knows about and the Claude Code session of each place. Sessions
/// stay open between requests, so a follow-up keeps its context; a session's jobs run
/// in the order given. Pure: the conductor feeds it events and the time (seconds on
/// any monotonic clock), so it is tested without Claude.
public struct JobBook: Sendable {
    public private(set) var jobs: [Job] = []
    private var sessions: [Place: String] = [:]
    private var counter = 0
    private var sessionCounter = 0
    /// Jobs remembered after they end, for status and "say that again".
    static let remembered = 20
    /// A request naming no project follows the last one this soon after it.
    public static let followUpSeconds = 120.0

    public init() {}

    /// Opens a job in place: the session to start when the place has none yet, and
    /// the session the request goes to.
    public mutating func start(_ request: String, in place: Place, at now: Double) -> (job: Job, newSession: Bool) {
        var newSession = false
        if sessions[place] == nil {
            sessionCounter += 1
            sessions[place] = "s\(sessionCounter)"
            newSession = true
        }
        counter += 1
        let job = Job(id: counter, place: place, session: sessions[place]!, request: request, started: now)
        jobs.append(job)
        forgetOld()
        return (job, newSession)
    }

    public func session(of place: Place) -> String? { sessions[place] }

    public func place(of session: String) -> Place? { sessions.first { $0.value == session }?.key }

    /// The job a session is working on: its oldest still active.
    public func current(in session: String) -> Job? { jobs.first { $0.session == session && $0.isActive } }

    public var active: [Job] { jobs.filter(\.isActive) }

    /// Where a request naming no project goes: the place of the job the user last
    /// started or heard about, while it runs or for followUpSeconds after it ended;
    /// else nil (the general session).
    public func followUp(at now: Double) -> Place? {
        guard let last = jobs.filter({ $0.status != .stopped })
            .max(by: { ($0.finished ?? $0.started) < ($1.finished ?? $1.started) })
        else { return nil }
        if last.isActive { return last.place }
        guard let finished = last.finished, now - finished <= Self.followUpSeconds else { return nil }
        return last.place
    }

    // MARK: Events from Claude Code

    public mutating func progress(_ line: String, in session: String) {
        update(session) { $0.progress = line }
    }

    public mutating func asking(_ asking: Bool, in session: String) {
        update(session) { $0.status = asking ? .asking : .running }
    }

    public mutating func spoke(_ text: String, in session: String) {
        update(session) { $0.summary = text }
    }

    /// Ends the session's current job; returns it as ended.
    public mutating func finished(ok: Bool, result: String, in session: String, at now: Double) -> Job? {
        guard let index = jobs.firstIndex(where: { $0.session == session && $0.isActive }) else { return nil }
        jobs[index].status = .done(ok: ok)
        jobs[index].finished = now
        if !ok {
            // An error's text is for the Mac's log, not for speaking: its first plain words.
            jobs[index].summary = Self.failure(result)
        } else if jobs[index].summary.isEmpty {
            jobs[index].summary = Self.sentences(result) ?? "Done."
        }
        return jobs[index]
    }

    /// The session is gone: its jobs stop and the place will get a new session.
    public mutating func closed(_ session: String, at now: Double) -> [Job] {
        var stopped: [Job] = []
        for index in jobs.indices where jobs[index].session == session && jobs[index].isActive {
            jobs[index].status = .stopped
            jobs[index].finished = now
            stopped.append(jobs[index])
        }
        sessions = sessions.filter { $0.value != session }
        return stopped
    }

    /// Every session, for when Claude Code itself stopped.
    public mutating func closedAll(at now: Double) -> [Job] {
        var stopped: [Job] = []
        for session in Array(sessions.values) { stopped += closed(session, at: now) }
        return stopped
    }

    private mutating func update(_ session: String, _ change: (inout Job) -> Void) {
        guard let index = jobs.firstIndex(where: { $0.session == session && $0.isActive }) else { return }
        change(&jobs[index])
    }

    private mutating func forgetOld() {
        while jobs.count > Self.remembered, let index = jobs.firstIndex(where: { !$0.isActive }) {
            jobs.remove(at: index)
        }
    }

    // MARK: Spoken status

    /// What is running and what finished lately, in a few short sentences.
    public func status(at now: Double) -> String {
        let running = active
        let recent = jobs.filter { !$0.isActive && ($0.finished.map { now - $0 <= 600 } ?? false) }.suffix(2)
        var lines: [String] = []
        if running.isEmpty {
            lines.append("Nothing is running.")
        } else if running.count == 1 {
            lines.append(Self.describe(running[0], at: now))
        } else {
            lines.append("\(running.count) jobs are running.")
            lines += running.prefix(3).map { Self.describe($0, at: now) }
        }
        lines += recent.reversed().map {
            $0.status == .stopped ? "\($0.place.name) was stopped." : "\($0.place.name) finished: \($0.summary)"
        }
        return Self.fitted(lines.joined(separator: " "))
    }

    /// The status of one place's latest job; nil when it has none.
    public func status(of place: Place, at now: Double) -> String? {
        guard let job = jobs.last(where: { $0.place == place }) else { return nil }
        if job.isActive { return Self.fitted(Self.describe(job, at: now)) }
        let outcome = job.status == .stopped ? "was stopped." : "finished: \(job.summary)"
        return Self.fitted("\(place.name) \(outcome)")
    }

    /// The last summary, of a place or of any job.
    public func lastSummary(of place: Place?) -> String? {
        jobs.last { !$0.isActive && $0.status != .stopped && (place == nil || $0.place == place) }
            .map { "\($0.place.name): \($0.summary)" }
    }

    static func describe(_ job: Job, at now: Double) -> String {
        let doing = job.status == .asking ? "is waiting for you on the device"
            : job.progress.isEmpty ? "is working" : "is on: \(job.progress)"
        return "\(job.place.name) \(doing), for \(Self.elapsed(now - job.started))."
    }

    static func elapsed(_ seconds: Double) -> String {
        let s = max(0, Int(seconds))
        if s < 60 { return s == 1 ? "1 second" : "\(s) seconds" }
        let m = s / 60
        return m == 1 ? "1 minute" : "\(m) minutes"
    }

    /// A failed run in a few spoken words: "it hit an error" and the error's first plain
    /// words, never its JSON.
    static func failure(_ text: String) -> String {
        let line = text.split(whereSeparator: \.isNewline).first.map(String.init) ?? ""
        let plain = String(line.prefix { $0 != "{" && $0 != "[" && $0 != "·" })
            .trimmingCharacters(in: CharacterSet(charactersIn: " :.-").union(.whitespaces))
        guard !plain.isEmpty else { return "That didn't work." }
        return fitted("That didn't work: \(plain).", maxBytes: 160)
    }

    /// Up to two sentences of plain text from Claude's result, for speaking.
    static func sentences(_ text: String) -> String? {
        let plain = text.replacingOccurrences(of: "`", with: "").replacingOccurrences(of: "*", with: "")
            .replacingOccurrences(of: "#", with: "")
            .split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }
            .first { !$0.isEmpty } ?? ""
        guard !plain.isEmpty else { return nil }
        var end = plain.endIndex, found = 0
        for index in plain.indices where ".!?".contains(plain[index]) {
            found += 1
            end = plain.index(after: index)
            if found == 2 { break }
        }
        return fitted(String(plain[..<end]), maxBytes: 240)
    }

    /// At most maxBytes of UTF-8, cut at a word.
    static func fitted(_ text: String, maxBytes: Int = 400) -> String {
        guard text.utf8.count > maxBytes else { return text }
        var out = ""
        for word in text.split(separator: " ") {
            let next = out.isEmpty ? String(word) : out + " " + word
            guard next.utf8.count <= maxBytes - 3 else { break }
            out = next
        }
        return out + "..."
    }
}
