import Foundation
import LokiProtocol

/// The conductor's side facing the user's choice of model (ADR-018): the list the
/// board picks the default from, the choice kept between runs, and requests a model
/// Ollama runs on the Mac answers. An Ollama request is a job like Claude's, so
/// status, stop and "say that again" work on it; its answer reaches the user through
/// the same speak and result events a Claude session sends.
extension Conductor {
    /// An Ollama conversation idle this long starts afresh.
    static let ollamaIdleSeconds = 600.0

    /// Sends the board the models it may pick from: Claude and those Ollama runs here
    /// that can use tools. A default Ollama does not list (it is not running, or the
    /// model is gone) stays on the list, so the board shows what requests go to.
    func offerModels() async {
        guard let localModels else { return }
        var local = await localModels.models()
        if choice.defaultAssistant == .ollama, !local.contains(choice.defaultModel) { local.append(choice.defaultModel) }
        let (names, current) = choice.offer(local)
        offered = names
        board.send(.models(names: names, current: current))
    }

    /// The user picked a model on the board: only one the last list offered. Every pick
    /// is answered with the list, so a refused one does not stay shown as picked.
    func pick(_ model: String) {
        guard let index = offered.firstIndex(of: model) else {
            if !offered.isEmpty {
                board.send(.models(names: offered, current: UInt32(offered.firstIndex(of: choice.defaultModel) ?? 0)))
            }
            return
        }
        choice.pick(model)
        do {
            try choices.save(choice)
        } catch {
            state(.failure, caption: "Model choice not saved")
        }
        board.send(.models(names: offered, current: UInt32(index)))
        warmDefault()
    }

    /// Loads the default Ollama model while the user talks, so their request does not
    /// wait for it.
    func warmDefault() {
        guard let localModels, choice.defaultAssistant == .ollama else { return }
        let model = choice.defaultModel, now = clock()
        // A model is warmed at most once a minute.
        if let last = warmed, last.model == model, now - last.at < 60 { return }
        warmed = (model, now)
        Task { await localModels.warm(model) }
    }

    /// A request for Ollama: a job in the model's conversation, answered in the order
    /// asked.
    func askOllama(_ words: String, model: String) {
        guard let localModels else { return reply("Ollama is not set up on this bridge.") }
        let place = Place.ollama(model)
        if let session = book.session(of: place), book.current(in: session) == nil,
           let last = ollamaActive[session], clock() - last > Self.ollamaIdleSeconds {
            stop(sessions: [session])
        }
        let (job, _) = book.start(words, in: place, at: clock())
        let session = job.session
        ollamaActive[session] = clock()
        state(.thinking, caption: place.name)
        let previous = ollamaRuns[session]?.last
        let run = Task {
            await previous?.value
            await self.answer(words, model: model, in: session, with: localModels)
        }
        ollamaRuns[session, default: []].append(run)
    }

    private func answer(_ words: String, model: String, in session: String, with models: LocalModels) async {
        defer {
            if ollamaRuns[session]?.isEmpty == false { ollamaRuns[session]?.removeFirst() }
            if ollamaRuns[session]?.isEmpty == true { ollamaRuns[session] = nil }
            if book.session(of: .ollama(model)) == session { ollamaActive[session] = clock() }
        }
        guard !Task.isCancelled, book.current(in: session) != nil else { return }
        do {
            let text = try await models.answer(words, model: model, conversation: session) { line in
                await self.handle(.tool(session: session, tool: "search", summary: line))
            }
            try Task.checkCancellation()
            await handle(.speak(session: session, text: text, mood: "neutral"))
            await handle(.result(session: session, ok: true, text: text, turns: 1, cost: 0))
        } catch is CancellationError {
            return
        } catch {
            guard !Task.isCancelled else { return }
            await handle(.result(session: session, ok: false, text: "\(error)", turns: 0, cost: 0))
        }
    }

    /// Ends an Ollama conversation: its runs are cancelled and the model forgets it.
    func endOllama(_ session: String) {
        ollamaRuns.removeValue(forKey: session)?.forEach { $0.cancel() }
        ollamaActive[session] = nil
        if let localModels { Task { await localModels.forget(session) } }
    }
}
