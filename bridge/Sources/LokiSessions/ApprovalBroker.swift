import Foundation
import LokiProtocol

/// What a question on the board settled.
public enum ApprovalOutcome: Equatable, Sendable {
    case chosen(Int)
    /// No answer in time, the board went away, or the question was withdrawn: deny.
    case unanswered
}

/// The board's approval cards (RFC-003, ADR-014): the only path by which a tap on the
/// board allows something. A question is answered at most once, and only by an answer
/// that carries its id, its nonce, and the digest of the card as sent, with a choice
/// the card offered. Anything else changes nothing. The board holds one card, so cards
/// are shown one at a time, in the order asked: none replaces another under a finger.
public actor ApprovalBroker {
    public typealias Send = @Sendable (BridgeMessage) -> Bool

    private struct Open {
        let ask: Ask
        let digest: String
        let resume: CheckedContinuation<ApprovalOutcome, Never>
    }

    private let send: Send
    private var open: [String: Open] = [:]
    private var counter: UInt64 = 0
    private var showing = false
    private var waiting: [CheckedContinuation<Void, Never>] = []
    /// Counts the board's departures: a card asked before one is never shown after it.
    private var departures = 0

    public init(send: @escaping Send) { self.send = send }

    /// Shows a card, once the cards before it are settled, and waits for its answer.
    /// Unanswered when it cannot be shown. The timeout runs from when it is shown.
    public func ask(kind: AskKind, title: String, detail: String = "", options: [String],
                    timeout: Duration = .seconds(60)) async -> ApprovalOutcome {
        // The turn passes straight to the next in line, so a later card cannot cut in.
        let asked = departures
        if showing { await withCheckedContinuation { waiting.append($0) } } else { showing = true }
        let outcome = asked == departures
            ? await show(kind: kind, title: title, detail: detail, options: options, timeout: timeout) : .unanswered
        if waiting.isEmpty { showing = false } else { waiting.removeFirst().resume() }
        return outcome
    }

    private func show(kind: AskKind, title: String, detail: String, options: [String],
                      timeout: Duration) async -> ApprovalOutcome {
        counter += 1
        let milliseconds = max(1000, min(600_000, Int(timeout.components.seconds * 1000)))
        let card = Ask(id: "a\(counter)", nonce: Self.nonce(), kind: kind, title: title, detail: detail,
                       options: options, timeoutMs: UInt32(milliseconds))
        // A card the protocol refuses is never shown, so it can never be approved.
        guard (try? LokiCodec.encode(.ask(card), seq: 1)) != nil else { return .unanswered }
        return await withCheckedContinuation { resume in
            open[card.id] = Open(ask: card, digest: card.digest, resume: resume)
            guard send(.ask(card)) else {
                settle(card.id, .unanswered)
                return
            }
            Task { [weak self] in
                try? await Task.sleep(for: .milliseconds(milliseconds))
                await self?.expire(card.id)
            }
        }
    }

    /// An answer from the board. True when it settled an open question.
    @discardableResult
    public func answer(id: String, nonce: String, digest: String, choice: UInt32) -> Bool {
        guard let question = open[id], question.ask.nonce == nonce, question.digest == digest,
              Int(choice) < question.ask.options.count
        else { return false }
        settle(id, .chosen(Int(choice)))
        return true
    }

    /// The board went away: every question shown or waiting its turn is unanswered.
    public func boardGone() {
        departures += 1
        for id in Array(open.keys) { settle(id, .unanswered) }
    }

    public var openCount: Int { open.count }
    /// Cards waiting for the one shown to settle.
    public var waitingCount: Int { waiting.count }

    private func expire(_ id: String) {
        guard open[id] != nil else { return }
        _ = send(.askCancel(id: id))
        settle(id, .unanswered)
    }

    private func settle(_ id: String, _ outcome: ApprovalOutcome) {
        open.removeValue(forKey: id)?.resume.resume(returning: outcome)
    }

    static func nonce() -> String {
        var generator = SystemRandomNumberGenerator()
        return (0..<16).map { _ in String(format: "%02x", UInt8.random(in: 0...255, using: &generator)) }.joined()
    }
}

/// What an approval card shows: the action whole, or nothing. A command cut to fit
/// could hide its dangerous end, so one that does not fit, or holds characters the
/// card cannot show, is refused instead of approved by voice.
public enum ApprovalCard {
    public struct Content: Equatable, Sendable {
        public let title: String
        public let detail: String
    }

    /// Lines an action may take on the round screen and still be seen whole.
    static let maxLines = 8

    public static func content(title: String, action: String, place: String) -> Content? {
        let detail = place.isEmpty ? action : "\(action)\nin \(place)"
        // Nothing that hides or reorders text: controls (but line feeds), bidi and
        // zero-width characters, any space or separator but the plain space, and runs
        // of three or more spaces, any of which could push part of a command out of sight.
        let scalarsOK = { (text: String) in
            text.unicodeScalars.allSatisfy { scalar in
                if scalar == "\n" || scalar == " " { return true }
                if CharacterSet.controlCharacters.contains(scalar) { return false }
                switch scalar.properties.generalCategory {
                case .format, .spaceSeparator, .lineSeparator, .paragraphSeparator: return false
                default: return true
                }
            }
        }
        guard !title.isEmpty, title.utf8.count <= 120, !title.contains("\n"), scalarsOK(title),
              detail.utf8.count <= 600, scalarsOK(detail), !detail.contains("   "),
              detail.split(separator: "\n", omittingEmptySubsequences: false).count <= maxLines
        else { return nil }
        return Content(title: title, detail: detail)
    }
}
