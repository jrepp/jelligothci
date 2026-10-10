import Foundation

/// What an utterance asks of Loki, decided before anything goes to Claude: a few short
/// requests the bridge answers itself from what it knows about the running jobs, and
/// work for Claude Code (RFC-003, background jobs).
public enum Request: Equatable, Sendable {
    /// "What's running?", "how's codergotchi going?", with the words as said, which
    /// go to Claude as work when there is no job to report on.
    case status(Project?, String)
    /// "Stop", "stop chatter", "cancel everything".
    case stop(Project?)
    /// "Say that again", "what did codergotchi say?"
    case repeatLast(Project?)
    /// Anything else, word for word.
    case work(String)
}

public enum Requests {
    static let stopWords: Set<String> = ["stop", "cancel", "halt", "abort"]
    /// Words a short stop may carry besides a project's name.
    static let stopFillers: Set<String> = ["it", "that", "this", "everything", "all", "the", "now", "please",
                                           "jobs", "job", "work", "working", "on", "in"]
    /// Words that only address or soften ("Loki, please stop"), dropped before matching.
    static let addressing: Set<String> = ["loki", "hey", "ok", "okay", "please", "so", "um", "uh", "oh", "well",
                                          "now", "just"]
    /// Whole requests, after addressing words and the project's name are taken out.
    static let statusPhrases: Set<String> = [
        "whats running", "what is running", "what are you doing", "what are you working on", "whats happening",
        "hows it going", "how is it going", "hows that going", "how is that going", "any updates", "any news",
        "are you done", "is it done", "status", "whats the status", "what is the status", "hows the work going",
        "status of", "whats the status of", "what is the status of", "give me the status", "tell me the status",
        "hows going", "how is going", "hows doing", "how is doing", "whats doing", "what is doing",
        "how are things", "whats up with",
    ]
    static let repeatPhrases: Set<String> = ["say that again", "repeat that", "what did you say", "say it again",
                                             "repeat", "come again", "what did say"]

    /// Lowercase words without punctuation; apostrophes vanish ("what's" is "whats").
    static func words(_ text: String) -> [String] {
        text.lowercased().replacingOccurrences(of: "'", with: "").replacingOccurrences(of: "\u{2019}", with: "")
            .split(whereSeparator: { !$0.isLetter && !$0.isNumber }).map(String.init)
    }

    public static func classify(_ text: String, resolver: ProjectResolver) -> Request {
        let said = words(text).filter { !addressing.contains($0) }
        guard !said.isEmpty else { return .work(text) }
        if let stop = stop(said, resolver) { return stop }
        if said.count <= 8, let request = asked(said, text, resolver) { return request }
        return .work(text)
    }

    /// A short stop: "stop", "never mind", "stop chatter", "cancel everything", "stop
    /// stop". A project counts when named exactly or misspelled ("stop chater"), not
    /// when the name starts something longer: "stop the chatter server" is work for
    /// Claude, not a stop of chatter's job.
    static func stop(_ said: [String], _ resolver: ProjectResolver) -> Request? {
        var rest = said
        if rest.starts(with: ["never", "mind"]) {
            rest.removeFirst(2)
        } else if rest.starts(with: ["nevermind"]) || stopWords.contains(rest[0]) {
            rest.removeFirst()
        } else {
            return nil
        }
        let other = rest.filter { !stopFillers.contains($0) && !stopWords.contains($0) }
        guard other.count <= 3 else { return nil }
        if other.isEmpty { return .stop(nil) }
        let key = ProjectResolver.key(other.joined()), projects = resolver.projects()
        if let project = projects.first(where: { ProjectResolver.key($0.name) == key }) { return .stop(project) }
        let near = projects.filter { ProjectResolver.misspelled(ProjectResolver.key($0.name), key) }
        return near.count == 1 ? .stop(near[0]) : nil  // "stop the server": work for Claude.
    }

    /// Status or repeat, of everything or of the project named in it: the whole
    /// request must be one of the phrases once the project's name is taken out, so
    /// "show me the git status" and "repeat the migration" go to Claude.
    static func asked(_ said: [String], _ text: String, _ resolver: ProjectResolver) -> Request? {
        // The whole request first, so a project named like a word of a phrase
        // ("updater", "statusline") never eats it.
        let whole = said.joined(separator: " ")
        if repeatPhrases.contains(whole) { return .repeatLast(nil) }
        if statusPhrases.contains(whole) { return .status(nil, text) }
        guard let found = named(in: said, resolver) else { return nil }
        let phrase = (said[..<found.words.lowerBound] + said[found.words.upperBound...]).joined(separator: " ")
        if repeatPhrases.contains(phrase) { return .repeatLast(found.project) }
        if statusPhrases.contains(phrase) { return .status(found.project, text) }
        return nil
    }

    /// A project named anywhere in a short request, in one to three words, and where:
    /// an exact name anywhere first ("coder gotchi" is codergotchi), then a near miss.
    static func named(in said: [String], _ resolver: ProjectResolver) -> (project: Project, words: Range<Int>)? {
        let projects = resolver.projects()
        let windows = [1, 2, 3].flatMap { length in
            length <= said.count ? (0...(said.count - length)).map { $0..<($0 + length) } : []
        }
        for range in windows {
            let key = ProjectResolver.key(said[range].joined())
            if let project = projects.first(where: { ProjectResolver.key($0.name) == key }) { return (project, range) }
        }
        for range in windows where said[range].joined().count >= 4 {
            if case .found(let project) = resolver.resolve(said[range].joined()) { return (project, range) }
        }
        return nil
    }
}
