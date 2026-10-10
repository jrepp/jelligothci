import Foundation
import Testing
@testable import LokiSessions

// The job book under seeded random event sequences: every invariant the conductor
// relies on is checked after every event, so a failure names its seed and step.

private let places: [Place] = [.general] + ["chatter", "codergotchi", "atrium"].map {
    .project(Project(name: $0, url: URL(fileURLWithPath: "/dev/\($0)")))
}

/// Text Claude might send: plain, markdown, long, multi-byte, or empty.
private func claudeText(_ random: inout Seeded) -> String {
    switch random.below(7) {
    case 0: ""
    case 1: "# Report\n\n**Done.** I ran `make test`. All passed. Then more."
    case 2: String(repeating: "word ", count: 50 + random.below(200))
    case 3: String(repeating: "\u{1F984}\u{00E9}", count: 10 + random.below(300))
    case 4: "Version 1.2.3 is out! Upgrade now? Yes."
    case 5: String(repeating: "x", count: 300 + random.below(500))
    default: "The tests pass."
    }
}

/// Progress and speak text arrive as one line (tool captions are collapsed); see the
/// report for multi-line progress from the progress tool.
private func oneLine(_ text: String) -> String { text.replacingOccurrences(of: "\n", with: " ") }

private func hasControl(_ text: String) -> Bool {
    text.unicodeScalars.contains { $0.value < 0x20 || $0.value == 0x7F }
}

/// The documented follow-up rule, written out independently of the book: the job
/// last started or heard about, leaving out stopped ones.
private func expectedFollowUp(_ jobs: [Job], at now: Double) -> Place? {
    var last: Job?
    for job in jobs where job.status != .stopped
        && (last == nil || (job.finished ?? job.started) > (last!.finished ?? last!.started)) { last = job }
    guard let last else { return nil }
    if last.isActive { return last.place }
    guard let finished = last.finished, now - finished <= JobBook.followUpSeconds else { return nil }
    return last.place
}

@Test(arguments: 0..<64)
func theJobBookKeepsItsInvariantsUnderAnyEvents(_ seed: Int) {
    var random = Seeded(seed)
    var book = JobBook()
    var now = 0.0
    var sessionsSeen: [String] = []
    var closedSessions = Set<String>()
    var terminal: [Int: Job.Status] = [:]
    var lastId = 0
    for step in 0..<(40 + random.below(160)) {
        let context = "seed \(seed) step \(step)"
        now += Double(random.below(4) == 0 ? random.below(400) : random.below(10))
        let session = sessionsSeen.isEmpty || random.chance(5) ? "s999" : random.pick(sessionsSeen)
        switch random.below(10) {
        case 0, 1:
            let place = random.pick(places)
            let wasOpen = book.session(of: place)
            let (job, newSession) = book.start("request \(step)", in: place, at: now)
            #expect(newSession == (wasOpen == nil), "\(context)")
            #expect(!closedSessions.contains(job.session), "\(context): a closed session was reused")
            #expect(job.id > lastId && job.status == .running && job.session == book.session(of: place), "\(context)")
            lastId = job.id
            if newSession {
                #expect(!sessionsSeen.contains(job.session), "\(context)")
                sessionsSeen.append(job.session)
            }
        case 2: book.progress(random.chance(50) ? "Running the tests" : oneLine(claudeText(&random)), in: session)
        case 3: book.asking(random.chance(50), in: session)
        case 4: book.spoke(random.chance(80) ? oneLine(claudeText(&random)) : "Said it.", in: session)
        case 5, 6:
            let oldest = book.current(in: session)
            let ended = book.finished(ok: random.chance(70), result: claudeText(&random), in: session, at: now)
            #expect(ended?.id == oldest?.id, "\(context): jobs end in the order given")
            if let ended { #expect(!ended.summary.isEmpty && ended.finished == now, "\(context)") }
        case 7:
            let active = book.active.filter { $0.session == session }.map(\.id)
            #expect(book.closed(session, at: now).map(\.id) == active, "\(context)")
            closedSessions.insert(session)
        case 8 where random.chance(15):
            let active = book.active.map(\.id).sorted()
            #expect(book.closedAll(at: now).map(\.id).sorted() == active, "\(context)")
            closedSessions.formUnion(sessionsSeen)
        default:
            break
        }
        checkInvariants(book, now: now, closed: closedSessions, terminal: &terminal, context)
    }
}

private func checkInvariants(_ book: JobBook, now: Double, closed: Set<String>, terminal: inout [Int: Job.Status],
                             _ context: String) {
    // At most one session per place, never a closed one, and each maps back to its place.
    let open = places.compactMap { place in book.session(of: place).map { (place, $0) } }
    #expect(Set(open.map(\.1)).count == open.count, "\(context): two places share a session")
    for (place, session) in open {
        #expect(book.place(of: session) == place && !closed.contains(session), "\(context)")
    }
    // Active jobs only in open sessions of their own place; each session's current is its oldest.
    for job in book.active {
        #expect(book.session(of: job.place) == job.session, "\(context): job \(job.id) active outside its session")
        #expect(book.current(in: job.session)?.id == book.active.filter { $0.session == job.session }.map(\.id).min(),
                "\(context)")
    }
    #expect(zip(book.jobs, book.jobs.dropFirst()).allSatisfy { $0.id < $1.id }, "\(context): jobs out of order")
    #expect(book.jobs.count <= max(JobBook.remembered, book.active.count), "\(context): forgotten jobs kept")
    for job in book.jobs where !job.isActive {
        #expect(job.finished != nil, "\(context)")
        if case .done = job.status { #expect(!job.summary.isEmpty, "\(context): job \(job.id) has no summary") }
        // Ended is final.
        if let before = terminal[job.id] { #expect(before == job.status, "\(context): job \(job.id) changed after ending") }
        terminal[job.id] = job.status
    }
    // What is spoken stays short and fit for the board's text.
    let status = book.status(at: now)
    #expect(status.utf8.count <= 400 && !status.isEmpty && !hasControl(status), "\(context): \(status.utf8.count) bytes")
    for place in places {
        if let line = book.status(of: place, at: now) {
            #expect(line.utf8.count <= 400 && !hasControl(line), "\(context)")
        }
    }
    #expect(book.followUp(at: now) == expectedFollowUp(book.jobs, at: now), "\(context)")
}

// MARK: The spoken text helpers

/// Property: fitted text is at most the limit, unchanged when it fits, and otherwise
/// whole words of the original followed by "...".
@Test(arguments: 0..<200)
func fittedTextIsShortAndMadeOfWholeWords(_ seed: Int) {
    var random = Seeded(seed)
    let words = (0..<random.below(150)).map { _ in
        String(repeating: random.pick(["a", "é", "\u{1F984}", "xyz", "longerword"]), count: 1 + random.below(random.chance(5) ? 200 : 6))
    }
    let text = words.joined(separator: " ")
    let limit = random.pick([3, 10, 60, 240, 400])
    let fitted = JobBook.fitted(text, maxBytes: limit)
    #expect(fitted.utf8.count <= limit, "seed \(seed)")
    if text.utf8.count <= limit {
        #expect(fitted == text, "seed \(seed)")
    } else {
        #expect(fitted.hasSuffix("..."), "seed \(seed)")
        let kept = fitted.dropLast(3)
        #expect(kept.isEmpty || text.hasPrefix(kept + " "), "seed \(seed): cut inside a word")
    }
}

/// Property: a summary from Claude's result is one line of at most 240 bytes, with no
/// markdown marks, and nil only when the result has no text.
@Test(arguments: 0..<200)
func summariesOfResultsAreOneShortPlainLine(_ seed: Int) {
    var random = Seeded(seed)
    let pieces = ["# ", "**", "`", "\n", "\n\n", " ", "Done.", "It works!", "Why?", "e.g. this", "\u{1F984}",
                  "make test", "1.2.3", "word", "\t", "\r\n"]
    let text = (0..<random.below(60)).map { _ in random.pick(pieces) }.joined()
    let summary = JobBook.sentences(text)
    let plain = text.replacingOccurrences(of: "`", with: "").replacingOccurrences(of: "*", with: "")
        .replacingOccurrences(of: "#", with: "")
    if plain.split(whereSeparator: \.isNewline).allSatisfy({ $0.trimmingCharacters(in: .whitespaces).isEmpty }) {
        #expect(summary == nil, "seed \(seed)")
    } else {
        let line = try! #require(summary, "seed \(seed)")
        #expect(!line.isEmpty && line.utf8.count <= 240, "seed \(seed)")
        #expect(!line.contains { $0.isNewline } && !line.contains("`") && !line.contains("*") && !line.contains("#"),
                "seed \(seed)")
        #expect(line.filter { ".!?".contains($0) }.count <= 2 || line.hasSuffix("..."), "seed \(seed)")
    }
}

@Test func elapsedTimeIsSaidInWholeSecondsThenMinutes() {
    #expect(JobBook.elapsed(-5) == "0 seconds")
    #expect(JobBook.elapsed(0.9) == "0 seconds")
    #expect(JobBook.elapsed(1) == "1 second")
    #expect(JobBook.elapsed(59.99) == "59 seconds")
    #expect(JobBook.elapsed(60) == "1 minute")
    #expect(JobBook.elapsed(119) == "1 minute")
    #expect(JobBook.elapsed(120) == "2 minutes")
    #expect(JobBook.elapsed(3600) == "60 minutes")
}

@Test func statusListsAtMostThreeRunningAndTwoRecentAndForgetsOldOnes() {
    var book = JobBook()
    for (index, place) in places.enumerated() { _ = book.start("job \(index)", in: place, at: Double(index)) }
    let status = book.status(at: 10)
    #expect(status.hasPrefix("4 jobs are running."))
    #expect(status.contains("General is working") && status.contains("codergotchi is working"))
    #expect(!status.contains("atrium"))  // the fourth is counted, not described
    _ = book.closedAll(at: 11)
    // The two latest ended, newest first; a stopped job was stopped, it did not finish.
    #expect(book.status(at: 611) == "Nothing is running. atrium was stopped. codergotchi was stopped.")
    // Ended more than ten minutes ago: no longer mentioned.
    #expect(book.status(at: 612) == "Nothing is running.")
    // A stopped job is "was stopped", never "finished".
    #expect(book.status(of: places[1], at: 12) == "chatter was stopped.")
    #expect(book.lastSummary(of: nil) == nil)  // stopped jobs have nothing to repeat
}

@Test func theBookRemembersTwentyEndedJobsButNeverForgetsARunningOne() {
    var book = JobBook()
    let running = book.start("long one", in: places[2], at: 0).job
    for index in 0..<50 {
        let job = book.start("quick \(index)", in: .general, at: Double(index + 1)).job
        _ = book.finished(ok: true, result: "Done \(index).", in: job.session, at: Double(index + 1))
    }
    #expect(book.jobs.count == JobBook.remembered)
    #expect(book.jobs.contains { $0.id == running.id && $0.isActive })
    #expect(book.lastSummary(of: .general) == "General: Done 49.")
    #expect(book.jobs.filter { !$0.isActive }.map(\.request).first == "quick 31")
}

@Test func eventsForUnknownOrEndedSessionsChangeNothing() {
    var book = JobBook()
    let job = book.start("a", in: .general, at: 0).job
    book.progress("x", in: "s404")
    book.asking(true, in: "s404")
    book.spoke("y", in: "s404")
    #expect(book.finished(ok: true, result: "z", in: "s404", at: 1) == nil)
    #expect(book.closed("s404", at: 1).isEmpty)
    #expect(book.jobs == [job])
    _ = book.finished(ok: true, result: "Done.", in: job.session, at: 2)
    book.spoke("late", in: job.session)
    book.asking(true, in: job.session)
    #expect(book.jobs.first?.summary == "Done." && book.jobs.first?.status == .done(ok: true))
    #expect(book.finished(ok: false, result: "", in: job.session, at: 3) == nil)
}
