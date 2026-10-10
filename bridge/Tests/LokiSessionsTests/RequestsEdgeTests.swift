import Foundation
import Testing
@testable import LokiSessions

// Requests at their edges: a stop names a project only exactly or misspelled, never as
// the start of something longer; status and repeat are whole requests once the project's
// name is out; addressing words never change what a request is; and classifying stays
// fast however long the utterance.

private let edgeRoot = projectRoot(["codergotchi", "chatter", "atrium", "elysium-main", "elysium-lan", "jev"])
private let edgeResolver = ProjectResolver(roots: [edgeRoot])
private func project(_ name: String) -> Project {
    Project(name: name, url: edgeRoot.appendingPathComponent(name).standardizedFileURL)
}
private func kind(_ text: String) -> Request { Requests.classify(text, resolver: edgeResolver) }

// MARK: Stops

/// The fixed cases from the recent fix: exact and misspelled names stop that project.
@Test(arguments: [
    ("stop chater", "chatter"), ("cancel coder gotcha", "codergotchi"), ("stop chatter", "chatter"),
    ("abort codergotchy", "codergotchi"), ("halt atrium", "atrium"), ("stop it in chatter please", "chatter"),
    ("Loki, cancel everything in coder gotchi now", "codergotchi"), ("stop jev", "jev"),
    ("stop the work on chatters", "chatter"), ("never mind chatter", "chatter"),
])
func anExactOrMisspelledNameStopsThatProject(_ text: String, _ name: String) {
    #expect(kind(text) == .stop(project(name)), "\(text)")
}

/// A name that only starts a longer phrase, or a misspelling too far or too short to
/// trust, is work for Claude, never a stop of a project's job.
@Test(arguments: ["stop the chatter server", "cancel chatter's build", "stop chatter server", "stop codergotchi's tests",
                  "stop the codergotchi watcher now", "stop chat", "stop jeb", "halt the atrium migration",
                  "stop chatter and codergotchi"])
func aLongerNameOrADistantMisspellingIsWork(_ text: String) {
    #expect(kind(text) == .work(text), "\(text)")
}

/// A misspelling close to two projects names neither: it is work, never a guess.
@Test func aMisspellingCloseToTwoProjectsIsNotAStopOfEither() {
    let root = projectRoot(["chatter", "chatted"])
    let resolver = ProjectResolver(roots: [root])
    #expect(Requests.classify("stop chatte", resolver: resolver) == .work("stop chatte"))
    // An exact name still wins over a near one.
    #expect(Requests.classify("stop chatted", resolver: resolver)
            == .stop(Project(name: "chatted", url: root.appendingPathComponent("chatted").standardizedFileURL)))
    // "elysium" is a prefix of two names, not a misspelling of one.
    #expect(kind("stop elysium") == .work("stop elysium"))
}

/// Repeated, addressed and softened stops are stops of everything.
@Test(arguments: ["stop stop", "stop, stop, stop!", "cancel cancel cancel", "Loki, stop", "hey loki stop",
                  "um, okay, just stop", "oh well, never mind", "Nevermind.", "STOP NOW", "abort abort please",
                  "stop. Cancel.", "halt all jobs", "cancel the job", "\u{201C}Stop!\u{201D}"])
func repeatedOrAddressedStopsStopEverything(_ text: String) {
    #expect(kind(text) == .stop(nil), "\(text)")
}

/// Addressing words alone, or nothing at all, are not a stop: they go to Claude as said.
@Test(arguments: ["Loki", "hey loki", "okay", "um uh", "", "   ", "...", "please"])
func addressingAloneIsWorkWordForWord(_ text: String) {
    #expect(kind(text) == .work(text), "\(text.debugDescription)")
}

/// Property: a stop of a project only ever names one of the resolver's projects, and
/// only when the words after the stop are that name exactly or one small edit away.
@Test(arguments: 0..<120)
func aStopNamesOnlyARealProjectAndOnlyWhenTheWordsAreItsName(_ seed: Int) {
    var random = Seeded(seed)
    let letters = Array("abcdefghijklmnopqrstuvwxyz")
    let names = ["codergotchi", "chatter", "atrium", "jev"]
    var spoken = random.pick(names)
    // Edit the name: drop, swap or insert a letter, zero to three times.
    for _ in 0..<random.below(4) {
        var chars = Array(spoken)
        let index = random.below(chars.count)
        switch random.below(3) {
        case 0 where chars.count > 1: chars.remove(at: index)
        case 1: chars[index] = random.pick(letters)
        default: chars.insert(random.pick(letters), at: index)
        }
        spoken = String(chars)
    }
    let text = random.pick(["stop", "cancel", "halt", "abort"]) + " " + spoken
    switch kind(text) {
    case .stop(let found?):
        let key = ProjectResolver.key(found.name)
        #expect(edgeResolver.projects().contains(found), "seed \(seed): \(text)")
        #expect(key == spoken || ProjectResolver.misspelled(key, spoken), "seed \(seed): \(text)")
    case .stop(nil):
        Issue.record("seed \(seed): \(text) stopped everything although it names something")
    case .work(let words):
        #expect(words == text, "seed \(seed)")
        // Work only when no single project is the name or a misspelling of it.
        let matches = names.filter { $0 == spoken || ProjectResolver.misspelled($0, spoken) }
        #expect(matches.count != 1, "seed \(seed): \(text) should have stopped \(matches)")
    case .status, .repeatLast:
        break  // A misspelling can land on a status word; that is not a stop.
    }
}

// MARK: Status and repeat are whole requests

/// Each phrase alone, and with a project's name where it reads naturally, and nothing
/// that merely contains one.
@Test func statusAndRepeatMatchOnlyWholeRequests() {
    #expect(kind("any news") == .status(nil, "any news"))
    #expect(kind("Loki, are you done?") == .status(nil, "Loki, are you done?"))
    #expect(kind("is chatter done") == .work("is chatter done"))  // "is done" is not a phrase
    #expect(kind("whats up with chater") == .status(project("chatter"), "whats up with chater"))
    #expect(kind("repeat") == .repeatLast(nil))
    #expect(kind("come again?") == .repeatLast(nil))
    #expect(kind("what did codergotchy say") == .repeatLast(project("codergotchi")))
    for work in ["status report for the board", "what's running on port 8080", "repeat after me",
                 "say that again but slower", "any news about Swift 7", "how is it going to scale"] {
        #expect(kind(work) == .work(work), "\(work)")
    }
}

/// A status word must not be taken for a project merely because a project's name starts
/// with it: with projects named "statusline" and "updater", "status" and "any updates"
/// are still requests for status, and "repeat" with a project "repeater" still repeats.
@Test func aStatusPhraseIsNotEatenByAProjectWhoseNameStartsWithItsWord() {
    let root = projectRoot(["chatter", "statusline", "updater", "repeater"])
    let resolver = ProjectResolver(roots: [root])
    func kind(_ text: String) -> Request { Requests.classify(text, resolver: resolver) }
    #expect(kind("status") == .status(nil, "status"))
    #expect(kind("any updates") == .status(nil, "any updates"))
    #expect(kind("repeat") == .repeatLast(nil))
    // Named exactly, the project still wins and the rest is the phrase.
    let statusline = Project(name: "statusline", url: root.appendingPathComponent("statusline").standardizedFileURL)
    #expect(kind("status of statusline") == .status(statusline, "status of statusline"))
}

// MARK: Metamorphic relations

/// The shape of a request, without the words it carries.
private func shape(_ request: Request) -> String {
    switch request {
    case .stop(let project): "stop \(project?.name ?? "-")"
    case .status(let project, _): "status \(project?.name ?? "-")"
    case .repeatLast(let project): "repeat \(project?.name ?? "-")"
    case .work: "work"
    }
}

/// Metamorphic: addressing and softening words, anywhere, never change what a request
/// is, since they are dropped before matching.
@Test(arguments: 0..<120)
func addressingWordsAnywhereNeverChangeWhatARequestIs(_ seed: Int) {
    var random = Seeded(seed)
    let phrases = ["stop", "stop chater", "cancel coder gotcha", "stop the chatter server", "never mind", "what's running",
                   "how's codergotchi going", "status of atrium", "say that again", "what did chatter say",
                   "in codergotchi run the tests", "commit it", "cancel chatter's build", "stop elysium"]
    let plain = random.pick(phrases)
    var words = plain.split(separator: " ").map(String.init)
    for _ in 0..<(1 + random.below(3)) {
        words.insert(random.pick(Array(Requests.addressing).sorted()), at: random.below(words.count + 1))
    }
    let dressed = words.joined(separator: random.pick([" ", ", ", "  "]))
    #expect(shape(kind(plain)) == shape(kind(dressed)), "seed \(seed): \(dressed)")
}

/// Metamorphic: a stop followed by more stop words or fillers is the same stop.
@Test(arguments: 0..<60)
func moreStopWordsAndFillersNeverChangeAStop(_ seed: Int) {
    var random = Seeded(seed)
    let base = random.pick(["stop", "stop chatter", "cancel coder gotchi", "halt atrium", "never mind"])
    let extra = (0..<(1 + random.below(4))).map { _ in
        random.pick(Array(Requests.stopWords.union(Requests.stopFillers)).sorted())
    }
    let text = base + " " + extra.joined(separator: " ")
    #expect(shape(kind(text)) == shape(kind(base)), "seed \(seed): \(text)")
}

// MARK: Non-functional

/// This thread's CPU time in seconds: unlike the wall clock, other tests running in
/// parallel do not inflate it.
private func threadSeconds() -> Double {
    var time = timespec()
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)
    return Double(time.tv_sec) + Double(time.tv_nsec) / 1e9
}

/// Classifying stays linear: a long utterance, one made of nothing but pointer and filler
/// words, and one huge word are sorted as work, word for word, within a generous budget,
/// and sixteen times the words takes far less than the 256 times it would if quadratic
/// (median of three, in this thread's CPU time; linear is about 16).
@Test func classifyingAVeryLongUtteranceStaysLinear() {
    var random = Seeded(7)
    let pool = ["stop", "in", "the", "chatter", "codergotchi", "status", "run", "tests", "my", "project", "again"]
    func utterance(_ count: Int) -> String { (0..<count).map { _ in random.pick(pool) }.joined(separator: " ") }
    func median(_ text: String) -> Double {
        let runs = (0..<3).map { _ in
            let start = threadSeconds()
            _ = kind(text)
            _ = edgeResolver.mentioned(in: text)
            return threadSeconds() - start
        }
        return runs.sorted()[1]
    }
    let (small, large) = (utterance(500), utterance(8_000))
    let (smallTime, largeTime) = (median(small), median(large))
    #expect(largeTime / smallTime < 64, "8,000 words took \(largeTime) s, 500 took \(smallTime) s")
    let pointers = String(repeating: "in the my project ", count: 2_000)
    let word = "in " + String(repeating: "x", count: 20_000)
    let start = threadSeconds()
    #expect(kind(large) == .work(large))
    #expect(kind(pointers) == .work(pointers))
    #expect(edgeResolver.mentioned(in: word) == .none)
    #expect(threadSeconds() - start < 3, "\(threadSeconds() - start) s")
}
