import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

/// Records what the broker sends to the board.
final class Board: @unchecked Sendable {
    private let lock = NSLock()
    private var sent: [BridgeMessage] = []
    var refuse = false

    func send(_ message: BridgeMessage) -> Bool {
        lock.withLock {
            guard !refuse else { return false }
            sent.append(message)
            return true
        }
    }

    var messages: [BridgeMessage] { lock.withLock { sent } }

    func lastAsk() async -> Ask? {
        for _ in 0..<200 {
            if let ask = messages.reversed().lazy.compactMap({ if case .ask(let a) = $0 { a } else { nil } }).first { return ask }
            try? await Task.sleep(for: .milliseconds(5))
        }
        return nil
    }
}

private func broker(_ board: Board) -> ApprovalBroker { ApprovalBroker { board.send($0) } }

@Test func theRightAnswerSettlesTheQuestionOnce() async throws {
    let board = Board(), approvals = broker(board)
    let outcome = Task { await approvals.ask(kind: .approve, title: "Run outside the sandbox?", detail: "npm run dev",
                                             options: ["Allow", "Deny"]) }
    let card = try #require(await board.lastAsk())
    #expect(await approvals.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 0))
    #expect(await outcome.value == .chosen(0))
    #expect(await !approvals.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 1))  // only once
    #expect(await approvals.openCount == 0)
}

@Test func answersThatDoNotMatchChangeNothing() async throws {
    let board = Board(), approvals = broker(board)
    let outcome = Task { await approvals.ask(kind: .approve, title: "Push to GitHub?", options: ["Allow", "Deny"]) }
    let card = try #require(await board.lastAsk())
    let other = Ask(id: card.id, nonce: card.nonce, kind: .approve, title: "Something else?", options: ["Allow", "Deny"],
                    timeoutMs: card.timeoutMs)
    #expect(await !approvals.answer(id: "a999", nonce: card.nonce, digest: card.digest, choice: 0))
    #expect(await !approvals.answer(id: card.id, nonce: String(repeating: "0", count: 32), digest: card.digest, choice: 0))
    #expect(await !approvals.answer(id: card.id, nonce: card.nonce, digest: other.digest, choice: 0))
    #expect(await !approvals.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 2))
    #expect(await approvals.openCount == 1)
    #expect(await approvals.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 1))
    #expect(await outcome.value == .chosen(1))
}

@Test func silenceAndDepartureMeanNo() async throws {
    let board = Board(), approvals = broker(board)
    let started = ContinuousClock.now
    #expect(await approvals.ask(kind: .approve, title: "Delete build?", options: ["Allow", "Deny"],
                                timeout: .milliseconds(1)) == .unanswered)  // at least one second
    #expect(ContinuousClock.now - started >= .seconds(1))
    let card = try #require(await board.lastAsk())
    #expect(board.messages.contains(.askCancel(id: card.id)))

    let waiting = Task { await approvals.ask(kind: .choose, title: "Which project?", options: ["a", "b", "c"]) }
    _ = try #require(await board.lastAsk())
    try await Task.sleep(for: .milliseconds(20))
    await approvals.boardGone()
    #expect(await waiting.value == .unanswered)
    board.refuse = true
    #expect(await approvals.ask(kind: .approve, title: "Offline?", options: ["Allow", "Deny"]) == .unanswered)
    // A card the protocol refuses is never shown, so it is never approved.
    board.refuse = false
    #expect(await approvals.ask(kind: .approve, title: "Three?", options: ["A", "B", "C"]) == .unanswered)
}

/// Cards asked at once are shown one at a time, each with a fresh id and nonce.
@Test func nonceAndIdentityAreFreshAndCardsAreShownInTurn() async throws {
    let board = Board(), approvals = broker(board)
    for _ in 0..<3 { Task { _ = await approvals.ask(kind: .approve, title: "t", options: ["Allow", "Deny"]) } }
    let shown = { board.messages.compactMap { if case .ask(let a) = $0 { a } else { nil } } }
    for count in 1...3 {
        try #require(await eventually(5) { shown().count == count })
        let (open, waiting) = (await approvals.openCount, await approvals.waitingCount)
        #expect(open == 1 && waiting == 3 - count)
        let card = shown()[count - 1]
        #expect(await approvals.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: 1))
    }
    let asks = shown()
    #expect(Set(asks.map(\.id)).count == 3 && Set(asks.map(\.nonce)).count == 3)
    #expect(asks.allSatisfy { $0.nonce.count == 32 })
    #expect(await eventually { await approvals.openCount == 0 })
}

@Test func cardsShowTheWholeActionOrNothing() {
    #expect(ApprovalCard.content(title: "Run outside the sandbox?", action: "npm run dev", place: "codergotchi")
        == ApprovalCard.Content(title: "Run outside the sandbox?", detail: "npm run dev\nin codergotchi"))
    let long = String(repeating: "x", count: 600)
    #expect(ApprovalCard.content(title: "Run?", action: long, place: "") != nil)
    #expect(ApprovalCard.content(title: "Run?", action: long + "y", place: "") == nil)  // never shortened
    #expect(ApprovalCard.content(title: "Run?", action: "echo hi\trm -rf ~", place: "") == nil)
    #expect(ApprovalCard.content(title: "Run?", action: "safe\u{202E}fr- mr", place: "") == nil)  // bidi override
    #expect(ApprovalCard.content(title: "Run?", action: "a\u{200B}b", place: "") == nil)          // zero width
    #expect(ApprovalCard.content(title: "two\nlines", action: "ls", place: "") == nil)
    #expect(ApprovalCard.content(title: "", action: "ls", place: "") == nil)
}
