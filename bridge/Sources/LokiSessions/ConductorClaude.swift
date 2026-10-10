import Foundation
import LokiProtocol

/// The conductor's side facing Claude Code: what each session reports, the cards
/// Claude needs answered, and the spoken summaries, queued.
extension Conductor {
    public func handle(_ event: SidecarEvent) async {
        switch event {
        case .tool(let id, _, let summary) where !summary.isEmpty:
            report(summary, in: id)
        case .progress(let id, let message, let fraction):
            // Clamped first: the board takes 0 to 1000, and Int32 traps out of range.
            report(message, in: id, progress: fraction.map { Int32((min(max($0, 0), 1) * 1000).rounded()) } ?? -1)
        case .permission(let id, let request, let tool, let input, _, let blockedPath):
            // A prompt from a session already stopped is refused without a card.
            guard let place = book.place(of: id) else {
                return sidecar.send(.decision(session: id, request: request, allow: false,
                                              message: "That job was stopped.", answers: nil))
            }
            // Off the sidecar's loop, so other jobs' events go on while the card waits.
            detach {
                await self.decide(permission: request, in: id, tool: tool, input: input, blockedPath: blockedPath,
                                  place: place)
            }
        case .question(let id, let request, let questions):
            guard let place = book.place(of: id) else {
                return sidecar.send(.decision(session: id, request: request, allow: false,
                                              message: "That job was stopped.", answers: nil))
            }
            detach { await self.decide(question: request, in: id, questions: questions, place: place) }
        case .speak(let id, let text, _):
            // Spoken as soon as Claude says it, not when its run ends: the run's last turn
            // and the render then overlap, which saves the user a second or more.
            let said = text.trimmingCharacters(in: .whitespacesAndNewlines)
            if !said.isEmpty, let job = book.spoke(said, in: id) { announce(Self.spoken(said, in: job.place), of: job.id) }
        case .result(let id, let ok, let text, _, _):
            let early = book.current(in: id)?.spokeAloud == true
            guard let job = book.finished(ok: ok, result: text, in: id, at: clock()) else { return }
            if job.place == .general { generalActive = clock() }
            state(ok ? .success : .failure, caption: job.place.name)
            // What Claude already said is not said twice; a failure is always said.
            if !ok || !early { announce(Self.spoken(job.summary, in: job.place)) }
            // A run that errored may leave its session unusable (a failed first turn, an
            // expired login): the next request to that place starts a fresh one, unless
            // requests are already waiting in it, which then run as they are.
            if !ok, book.current(in: id) == nil { stop(sessions: [id]) }
        case .closed(let id), .error(let id?, _):
            let stopped = book.closed(id, at: clock())
            silence(stopped)
            if let job = stopped.first {
                state(.failure, caption: job.place.name)
                announce("\(job.place.name) stopped before it finished.")
            }
        case .exited:
            let stopped = book.closedAll(at: clock())
            silence(stopped)
            await approvals.boardGone()
            state(.failure, caption: "Claude Code stopped")
            if !stopped.isEmpty { announce("Claude Code stopped, and with it \(Self.list(stopped.map(\.place.name))).") }
        default:
            break
        }
    }

    /// A line as spoken: a project's name first, so the user knows which job it was.
    static func spoken(_ line: String, in place: Place) -> String {
        place == .general ? line : "\(place.name): \(line)"
    }

    private func decide(permission request: String, in id: String, tool: String, input: JSONValue,
                        blockedPath: String?, place: Place) async {
        book.asking(true, in: id)
        let answer = await permit(tool: tool, input: input, blockedPath: blockedPath, place: place)
        book.asking(false, in: id)
        let message: String? = switch answer {
        case true?: nil
        case false?: "The user did not allow this on the Loki device."
        case nil: "Loki's voice policy refuses this without asking the user. Do not try another way to do it."
        }
        sidecar.send(.decision(session: id, request: request, allow: answer == true, message: message, answers: nil))
    }

    private func decide(question request: String, in id: String, questions: [SidecarQuestion], place: Place) async {
        book.asking(true, in: id)
        let answers = await answer(questions, place: place)
        book.asking(false, in: id)
        sidecar.send(.decision(session: id, request: request, allow: answers != nil,
                               message: answers == nil ? "The user did not answer on the Loki device." : nil,
                               answers: answers))
    }

    private func report(_ line: String, in session: String, progress: Int32 = -1) {
        book.progress(line, in: session)
        let place = book.place(of: session)?.name ?? ""
        state(.working, caption: place.isEmpty ? line : "\(place): \(line)", progress: progress)
    }

    /// The user's answer on a card; nil when the policy refuses without one.
    private func permit(tool: String, input: JSONValue, blockedPath: String?, place: Place?) async -> Bool? {
        let project: Project? = switch place {
        case .project(let project): project
        case .general: Project(name: "General", url: resolver.roots.first ?? URL(fileURLWithPath: "/"))
        case nil: nil
        }
        guard let card = PermissionCards.card(tool: tool, input: input, project: project, blockedPath: blockedPath)
        else { return nil }
        state(.asking, caption: place?.name ?? "")
        return await approvals.ask(kind: .approve, title: card.title, detail: card.detail,
                                   options: ["Allow", "Deny"]) == .chosen(0)
    }

    /// Each question as a card, in order; nil when one goes unanswered.
    private func answer(_ questions: [SidecarQuestion], place: Place?) async -> [String: String]? {
        var answers: [String: String] = [:]
        state(.asking, caption: place?.name ?? "")
        for question in questions.prefix(4) {
            let options = Array(question.options.prefix(4))
            guard options.count >= 2,
                  case .chosen(let index) = await approvals.ask(
                    kind: .choose, title: LokiText.fitted(question.question, maxBytes: 120),
                    options: options.map { LokiText.fitted($0, maxBytes: 40) }),
                  index < options.count
            else { return nil }
            answers[question.question] = options[index]
        }
        return answers.isEmpty ? nil : answers
    }

    // MARK: Speaking, one line at a time

    /// A line waiting to be said. Claude's own words carry their job, so they are
    /// dropped if that job is stopped before their turn comes.
    struct Line: Equatable, Sendable {
        let text: String
        var job: Int?
    }

    /// An answer to what the user just asked: ahead of announcements.
    func reply(_ text: String) {
        queue.insert(Line(text: text), at: 0)
    }

    /// A finished job's summary, or Claude's words as it says them (`job`), after
    /// whatever is already waiting. A job's newer words replace its words still
    /// waiting, as before speaking moved to the speak event only its last were said.
    func announce(_ text: String, of job: Int? = nil) {
        if let job { queue.removeAll { $0.job == job } }
        queue.append(Line(text: text, job: job))
        // While the user talks the queue waits for the release, which speaks it after
        // routing what they said: a "stop" then silences the stopped job's words first.
        if !listening, !routing { Task { await self.speakQueued() } }
    }

    /// Drops the words of jobs that were stopped and have not been said yet.
    func silence(_ stopped: [Job]) {
        let ids = Set(stopped.map(\.id))
        if !ids.isEmpty { queue.removeAll { $0.job.map(ids.contains) ?? false } }
    }

    /// Speaks the queue in order, never while the user holds to talk; it resumes when
    /// they let go.
    func speakQueued() async {
        guard !speaking else { return }
        speaking = true
        defer { speaking = false }
        while !listening, !routing, !queue.isEmpty {
            let line = queue.removeFirst()
            state(.speaking)
            await speaker.say(line.text)
        }
        state(book.active.isEmpty ? .idle : .working)
    }
}
