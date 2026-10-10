import Foundation
import Testing
@testable import LokiSpeech

/// Answers Chatter's REST calls from a script, without a network or Chatter.
final class FakeChatter: URLProtocol, @unchecked Sendable {
    nonisolated(unsafe) static var handler: (@Sendable (URLRequest) -> (Int, Data))?
    nonisolated(unsafe) static var seen: [URLRequest] = []
    static let lock = NSLock()

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }

    override func startLoading() {
        var request = request
        if request.httpBody == nil, let stream = request.httpBodyStream {
            stream.open()
            var body = Data()
            var buffer = [UInt8](repeating: 0, count: 4096)
            while stream.hasBytesAvailable {
                let count = stream.read(&buffer, maxLength: buffer.count)
                if count <= 0 { break }
                body.append(buffer, count: count)
            }
            stream.close()
            request.httpBody = body
        }
        let (status, data) = Self.lock.withLock {
            Self.seen.append(request)
            return Self.handler?(request) ?? (500, Data())
        }
        let response = HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1",
                                       headerFields: ["Content-Type": "application/json"])!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: data)
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {}

    static func client(token: String? = "secret-token-for-tests", timeout: Duration = .seconds(5)) -> ChatterClient {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [FakeChatter.self]
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("chatter-token-\(UUID())")
        if let token {
            FileManager.default.createFile(atPath: file.path, contents: Data(token.utf8), attributes: [.posixPermissions: 0o600])
        }
        lock.withLock { seen = [] }
        return ChatterClient(tokenFile: file, session: URLSession(configuration: configuration), timeout: timeout)
    }
}

private func json(_ object: Any) -> Data { try! JSONSerialization.data(withJSONObject: object) }

@Suite(.serialized) struct ChatterTests {
    @Test func rendersInTheChosenVoiceAndReturnsTheAudio() async throws {
        let polls = Counter()
        FakeChatter.handler = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"): return (200, json(["id": "job-1", "state": "queued"]))
            case ("GET", "/v1/jobs/job-1"): return (200, json(["state": polls.next() < 2 ? "running" : "completed"]))
            case ("GET", "/v1/jobs/job-1/audio"): return (200, Data("RIFFwav".utf8))
            default: return (404, json(["error": "no route"]))
            }
        }
        let audio = try await FakeChatter.client().render("All tests pass.", voice: "Scar")
        #expect(audio == Data("RIFFwav".utf8))
        let requests = FakeChatter.lock.withLock { FakeChatter.seen }
        #expect(requests.allSatisfy { $0.value(forHTTPHeaderField: "Authorization") == "Bearer secret-token-for-tests" })
        let body = try JSONSerialization.jsonObject(with: requests[0].httpBody ?? Data()) as? [String: String]
        #expect(body == ["voice": "Scar", "text": "All tests pass.", "mode": "render", "quality": "responsive"])
        // Each lookup asks Chatter to hold the reply until the job finishes, inside the 30 s request timeout.
        let lookups = requests.filter { $0.url!.path == "/v1/jobs/job-1" }
        #expect(lookups.count == 3 && lookups.allSatisfy { $0.url!.query == "wait=20" })
        #expect(!requests.contains { $0.httpMethod == "DELETE" })
    }

    /// Chatter before 3.2 refuses render: the reply still comes, from `save` (Studio), and is
    /// never cancelled there, since that Chatter's cancel also stops what plays on the Mac.
    @Test func anOlderChatterWithoutRenderModeIsAskedToSave() async throws {
        FakeChatter.handler = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"):
                let body = (try? JSONSerialization.jsonObject(with: request.httpBody ?? Data()) as? [String: String]) ?? [:]
                return body["mode"] == "render" ? (400, json(["error": "Mode must be play or save."])) : (202, json(["id": "old-1"]))
            case ("GET", "/v1/jobs/old-1"): return (200, json(["state": "completed"]))
            case ("GET", "/v1/jobs/old-1/audio"): return (200, Data("RIFFold".utf8))
            default: return (404, json(["error": "no route"]))
            }
        }
        #expect(try await FakeChatter.client().render("Hi.", voice: "Scar") == Data("RIFFold".utf8))
        let bodies = FakeChatter.lock.withLock { FakeChatter.seen }.filter { $0.httpMethod == "POST" }
            .map { (try? JSONSerialization.jsonObject(with: $0.httpBody ?? Data()) as? [String: String]) ?? [:] }
        #expect(bodies.map { $0["mode"] } == ["render", "save"])
        #expect(bodies[1]["quality"] == nil)
    }

    @Test func otherRefusalsAreNotMistakenForAnOlderChatter() async {
        FakeChatter.handler = { _ in (400, json(["error": "Choose a saved voice by its unique ID or unambiguous name."])) }
        do {
            _ = try await FakeChatter.client().render("Hi.", voice: "Nobody")
            Issue.record("rendering did not fail")
        } catch {
            #expect("\(error)".contains("unique ID"))
        }
        #expect(FakeChatter.lock.withLock { FakeChatter.seen }.count == 1)
    }

    /// Talking over a reply cancels the piece being rendered, in Chatter too, so the next
    /// reply does not wait behind it.
    @Test func cancellingARenderCancelsItInChatter() async throws {
        FakeChatter.handler = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"): return (202, json(["id": "abandoned-1", "state": "queued"]))
            case ("DELETE", "/v1/jobs/abandoned-1"): return (200, json(["id": "abandoned-1", "state": "cancelled"]))
            default: return (200, json(["state": "generating"]))
            }
        }
        let client = FakeChatter.client()
        let render = Task { try await client.render("A long answer.", voice: "Scar") }
        try await waitFor { FakeChatter.lock.withLock { FakeChatter.seen }.contains { $0.url!.path == "/v1/jobs/abandoned-1" } }
        render.cancel()
        await #expect(throws: (any Error).self) { try await render.value }
        try await waitFor { FakeChatter.lock.withLock { FakeChatter.seen }.contains { $0.httpMethod == "DELETE" } }
        let cancel = FakeChatter.lock.withLock { FakeChatter.seen }.first { $0.httpMethod == "DELETE" }
        #expect(cancel?.url!.path == "/v1/jobs/abandoned-1")
        #expect(cancel?.value(forHTTPHeaderField: "Authorization") == "Bearer secret-token-for-tests")
    }

    @Test func aSaveFromAnOlderChatterIsNeverCancelled() async throws {
        FakeChatter.handler = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"):
                let body = (try? JSONSerialization.jsonObject(with: request.httpBody ?? Data()) as? [String: String]) ?? [:]
                return body["mode"] == "render" ? (400, json(["error": "Mode must be play or save."])) : (202, json(["id": "old-2"]))
            default: return (200, json(["state": "running"]))
            }
        }
        let client = FakeChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        try await waitFor { FakeChatter.lock.withLock { FakeChatter.seen }.contains { $0.url!.path == "/v1/jobs/old-2" } }
        render.cancel()
        _ = try? await render.value
        try await Task.sleep(for: .milliseconds(300))
        #expect(!FakeChatter.lock.withLock { FakeChatter.seen }.contains { $0.httpMethod == "DELETE" })
    }

    @Test func failuresBecomeReadableErrors() async throws {
        let cases: [(@Sendable (URLRequest) -> (Int, Data), String)] = [
            ({ _ in (503, json(["error": "The speech engine is starting"])) }, "starting"),
            ({ request in request.url!.path == "/v1/speech" ? (200, json(["state": "queued"])) : (404, Data()) }, "job id"),
            ({ request in request.url!.path == "/v1/speech" ? (200, json(["id": "../etc"])) : (404, Data()) }, "job id"),
            ({ request in request.url!.path == "/v1/speech" ? (200, json(["id": "j"])) : (200, json(["state": "failed"])) },
             "failed"),
            ({ _ in (200, Data("not json".utf8)) }, ""),
        ]
        for (handler, words) in cases {
            FakeChatter.handler = handler
            do {
                _ = try await FakeChatter.client().render("hi", voice: "Scar")
                Issue.record("rendering did not fail")
            } catch {
                #expect(words.isEmpty || "\(error)".contains(words), "\(error)")
                #expect(!"\(error)".contains("secret-token-for-tests"))
            }
        }
    }

    @Test func aMissingTokenStopsBeforeAnyRequest() async {
        FakeChatter.handler = { _ in (200, json(["id": "j"])) }
        await #expect(throws: SpeechError.self) { try await FakeChatter.client(token: nil).render("hi", voice: "Scar") }
        await #expect(throws: SpeechError.self) { try await FakeChatter.client(token: "  \n").render("hi", voice: "Scar") }
        #expect(FakeChatter.lock.withLock { FakeChatter.seen.isEmpty })
    }

    @Test func aRenderingThatNeverFinishesTimesOut() async {
        FakeChatter.handler = { request in
            request.url!.path == "/v1/speech" ? (200, json(["id": "slow"])) : (200, json(["state": "running"]))
        }
        await #expect(throws: SpeechError.self) {
            try await FakeChatter.client(timeout: .milliseconds(300)).render("hi", voice: "Scar")
        }
    }

    @Test func anEmptyJobIDAndOversizedRepliesAreRefused() async {
        let cases: [(@Sendable (URLRequest) -> (Int, Data), String)] = [
            ({ request in request.url!.path == "/v1/speech" ? (200, json(["id": ""])) : (404, Data()) }, "job id"),
            ({ request in
                request.url!.path == "/v1/speech" ? (200, json(["id": "j", "pad": String(repeating: "x", count: 70_000)]))
                    : (404, Data())
            }, "too large"),
            ({ request in
                switch request.url!.path {
                case "/v1/speech": (200, json(["id": "j"]))
                case "/v1/jobs/j": (200, json(["state": "completed"]))
                default: (200, Data(count: ChatterClient.maxAudio + 1))
                }
            }, "too large"),
        ]
        for (handler, words) in cases {
            FakeChatter.handler = handler
            do {
                _ = try await FakeChatter.client().render("hi", voice: "Scar")
                Issue.record("rendering did not fail")
            } catch {
                #expect("\(error)".contains(words), "\(error)")
            }
        }
    }

    @Test func errorTextCannotReachTheTerminalAsControlCharacters() async {
        FakeChatter.handler = { _ in (503, json(["error": "\u{1B}]0;owned\u{07}\u{1B}[2J" + String(repeating: "y", count: 400)])) }
        do {
            _ = try await FakeChatter.client().render("hi", voice: "Scar")
            Issue.record("rendering did not fail")
        } catch {
            let text = "\(error)"
            #expect(!text.unicodeScalars.contains { $0.value < 0x20 || (0x7F...0x9F).contains($0.value) })
            #expect(text.utf8.count < 300)
        }
    }

    @Test func aTokenFileOthersCanReadIsRefused() async throws {
        FakeChatter.handler = { _ in (200, json(["id": "j"])) }
        let client = FakeChatter.client()
        try FileManager.default.setAttributes([.posixPermissions: 0o644], ofItemAtPath: client.tokenFile.path)
        await #expect(throws: SpeechError.self) { try await client.render("hi", voice: "Scar") }
        let link = client.tokenFile.deletingLastPathComponent().appendingPathComponent("chatter-link-\(UUID())")
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: client.tokenFile.path)
        try FileManager.default.createSymbolicLink(at: link, withDestinationURL: client.tokenFile)
        let linked = ChatterClient(tokenFile: link, session: client.session, timeout: .seconds(1))
        await #expect(throws: SpeechError.self) { try await linked.render("hi", voice: "Scar") }
        #expect(FakeChatter.lock.withLock { FakeChatter.seen.isEmpty })
    }

    @Test func voicesAreListed() async throws {
        FakeChatter.handler = { _ in
            (200, json(["voices": [["id": "6D86", "name": "Scar", "ready": true], ["id": "R", "name": "Ryan"],
                                   ["name": "no id"]]]))
        }
        let voices = try await FakeChatter.client().voices()
        #expect(voices == [ChatterClient.Voice(id: "6D86", name: "Scar", ready: true),
                           ChatterClient.Voice(id: "R", name: "Ryan", ready: false)])
    }
}

/// Polls until `condition` holds, for at most 2 s.
private func waitFor(_ condition: @Sendable () -> Bool) async throws {
    let deadline = ContinuousClock.now + .seconds(2)
    while !condition() {
        guard ContinuousClock.now < deadline else { Issue.record("timed out waiting"); return }
        try await Task.sleep(for: .milliseconds(10))
    }
}

final class Counter: @unchecked Sendable {
    private var value = 0
    private let lock = NSLock()
    func next() -> Int { lock.withLock { defer { value += 1 }; return value } }
}
