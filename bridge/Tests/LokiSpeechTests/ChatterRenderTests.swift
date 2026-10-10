import Foundation
import Testing
@testable import LokiSpeech

// Chatter 3.2's render mode (memo-031): the request, the fallback to save for an older
// Chatter, the held job lookups, and cancelling a render in Chatter when its reply is no
// longer wanted. These extend the serialized ChatterTests suite, so they never race its
// shared fakes.

/// Answers Chatter's REST calls at once, later, or never (until the request is stopped),
/// and records each request and each one URLSession stopped.
final class HoldingChatter: URLProtocol, @unchecked Sendable {
    enum Answer { case now(Int, Data), after(Duration, Int, Data), hold }
    nonisolated(unsafe) static var answer: (@Sendable (URLRequest) -> Answer)?
    nonisolated(unsafe) static var seen: [URLRequest] = []
    nonisolated(unsafe) static var stopped: [URL] = []
    static let lock = NSLock()
    static let token = "hold-token-7f3a9c"

    private let state = NSLock()
    private var finished = false

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
        let answer = Self.lock.withLock {
            Self.seen.append(request)
            return Self.answer?(request) ?? .now(500, Data())
        }
        switch answer {
        case .now(let status, let data): reply(status, data)
        case .hold: break
        case .after(let delay, let status, let data):
            let seconds = Double(delay.components.seconds) + Double(delay.components.attoseconds) / 1e18
            DispatchQueue.global().asyncAfter(deadline: .now() + seconds) { self.reply(status, data) }
        }
    }

    private func reply(_ status: Int, _ data: Data) {
        guard state.withLock({ () -> Bool in defer { finished = true }; return !finished }) else { return }
        let response = HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1",
                                       headerFields: ["Content-Type": "application/json"])!
        client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: data)
        client?.urlProtocolDidFinishLoading(self)
    }

    override func stopLoading() {
        let wasOpen = state.withLock { () -> Bool in defer { finished = true }; return !finished }
        if wasOpen { Self.lock.withLock { Self.stopped.append(request.url!) } }
    }

    static func client(timeout: Duration = .seconds(5), quality: String = ChatterClient.defaultQuality) -> ChatterClient {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [HoldingChatter.self]
        configuration.timeoutIntervalForRequest = 10
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("chatter-token-\(UUID())")
        FileManager.default.createFile(atPath: file.path, contents: Data(token.utf8), attributes: [.posixPermissions: 0o600])
        lock.withLock { seen = []; stopped = [] }
        return ChatterClient(tokenFile: file, session: URLSession(configuration: configuration, delegate: RefuseRedirects(),
                                                                  delegateQueue: nil),
                             timeout: timeout, quality: quality)
    }

    static var requests: [URLRequest] { lock.withLock { seen } }
    static var deletes: [URLRequest] { requests.filter { $0.httpMethod == "DELETE" } }
    static func lookups(_ id: String) -> [URLRequest] {
        requests.filter { ($0.httpMethod ?? "GET") == "GET" && $0.url!.path == "/v1/jobs/\(id)" }
    }
}

private func json(_ object: Any) -> Data { try! JSONSerialization.data(withJSONObject: object) }

private func mode(_ request: URLRequest) -> String? {
    ((try? JSONSerialization.jsonObject(with: request.httpBody ?? Data())) as? [String: Any])?["mode"] as? String
}

/// Polls until `condition` holds, for at most 2 s; false if it never did.
@discardableResult
private func eventually(_ condition: @Sendable () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(2)
    while !condition() {
        guard ContinuousClock.now < deadline else { return false }
        try? await Task.sleep(for: .milliseconds(5))
    }
    return true
}

/// Long enough for a detached DELETE that was going to go out to have gone out.
private func settle() async { try? await Task.sleep(for: .milliseconds(120)) }

/// A Chatter 3.2 that accepts render, answers lookups with `lookup`, and the audio with `audio`.
private func chatter32(id: String, lookup: @escaping @Sendable (URLRequest) -> HoldingChatter.Answer,
                       audio: HoldingChatter.Answer = .now(200, Data("RIFFnew".utf8))) {
    HoldingChatter.answer = { request in
        switch (request.httpMethod ?? "GET", request.url!.path) {
        case ("POST", "/v1/speech"): .now(202, json(["id": id, "state": "queued"]))
        case ("GET", "/v1/jobs/\(id)"): lookup(request)
        case ("GET", "/v1/jobs/\(id)/audio"): audio
        case ("DELETE", "/v1/jobs/\(id)"): .now(200, json(["id": id, "state": "cancelled"]))
        default: .now(404, json(["error": "no route"]))
        }
    }
}

extension ChatterTests {
    // MARK: Cancellation at every point

    /// Cancelled while Chatter has not yet answered the POST: there is no job id, so no
    /// DELETE and no lookup; the held POST is stopped, not waited out.
    @Test func cancellingBeforeThePostAnswersSendsNothingMore() async throws {
        HoldingChatter.answer = { request in request.url!.path == "/v1/speech" ? .hold : .now(200, json(["state": "completed"])) }
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(await eventually { !HoldingChatter.requests.isEmpty })
        let started = ContinuousClock.now
        render.cancel()
        await #expect(throws: (any Error).self) { try await render.value }
        #expect(ContinuousClock.now - started < .seconds(1))
        await settle()
        #expect(HoldingChatter.requests.map { $0.url!.path } == ["/v1/speech"])
        #expect(HoldingChatter.lock.withLock { HoldingChatter.stopped.map(\.path) } == ["/v1/speech"])
    }

    /// A task already cancelled when it asks for a render sends nothing: a POST it could
    /// not wait for would leave Chatter rendering a job no one can cancel (its id never
    /// arrives), and the next reply would wait behind it.
    @Test func anAlreadyCancelledTaskSendsNothing() async throws {
        chatter32(id: "pre-1", lookup: { _ in .now(200, json(["state": "completed"])) })
        let client = HoldingChatter.client()
        let render = Task {
            withUnsafeCurrentTask { $0?.cancel() }
            return try await client.render("Hi.", voice: "Scar")
        }
        await #expect(throws: (any Error).self) { try await render.value }
        await settle()
        #expect(HoldingChatter.requests.map { "\($0.httpMethod ?? "GET") \($0.url!.path)" } == [])
    }

    /// Cancelled as the POST's answer arrives, before the first lookup: whichever side of
    /// the job id the cancel lands on, at most one DELETE goes out, for that id, and no
    /// lookup is answered into a reply.
    @Test(arguments: 0..<4)
    func cancellingAsThePostAnswersSendsAtMostOneDelete(_ attempt: Int) async throws {
        let id = "edge-\(attempt)"
        let box = TaskBox()
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"):
                box.cancel()  // the awaiting task is cancelled while its POST is answered
                return .now(202, json(["id": id]))
            case ("DELETE", _): return .now(200, json(["state": "cancelled"]))
            default: return .now(200, json(["state": "completed"]))
            }
        }
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        box.set(render)
        let outcome = await render.result
        await settle()
        let deletes = HoldingChatter.deletes
        #expect(deletes.count <= 1, "attempt \(attempt)")
        #expect(deletes.allSatisfy { $0.url!.path == "/v1/jobs/\(id)" && $0.url!.query == nil }, "attempt \(attempt)")
        if case .success = outcome {
            Issue.record("a render cancelled before its first lookup returned audio (attempt \(attempt))")
        }
    }

    /// Cancelled while Chatter 3.2 holds a lookup open (`?wait=20`): exactly one DELETE, for
    /// the job, with the token and no body or query; the held lookup is stopped at once,
    /// not waited out for 20 s.
    @Test func cancellingDuringAHeldLookupCancelsTheJobOnce() async throws {
        chatter32(id: "held-1", lookup: { _ in .hold })
        let client = HoldingChatter.client()
        let render = Task { try await client.render("A long answer.", voice: "Scar") }
        #expect(await eventually { !HoldingChatter.lookups("held-1").isEmpty })
        let started = ContinuousClock.now
        render.cancel()
        await #expect(throws: (any Error).self) { try await render.value }
        #expect(ContinuousClock.now - started < .seconds(1))
        #expect(await eventually { !HoldingChatter.deletes.isEmpty })
        await settle()
        let deletes = HoldingChatter.deletes
        #expect(deletes.count == 1)
        #expect(deletes.first?.url?.path == "/v1/jobs/held-1" && deletes.first?.url?.query == nil)
        #expect(deletes.first?.value(forHTTPHeaderField: "Authorization") == "Bearer \(HoldingChatter.token)")
        #expect((deletes.first?.httpBody ?? Data()).isEmpty && deletes.first?.value(forHTTPHeaderField: "Content-Type") == nil)
        #expect(HoldingChatter.lock.withLock { HoldingChatter.stopped.map(\.path) } == ["/v1/jobs/held-1"])
        #expect(HoldingChatter.lookups("held-1").count == 1)
    }

    /// Cancelled between two quick lookups (an older Chatter answering at once, or a
    /// lookup that came back early): the poll's sleep ends, one DELETE goes out.
    @Test func cancellingBetweenLookupsCancelsTheJobOnce() async throws {
        chatter32(id: "poll-1", lookup: { _ in .now(200, json(["state": "generating"])) })
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(await eventually { HoldingChatter.lookups("poll-1").count >= 2 })
        render.cancel()
        await #expect(throws: (any Error).self) { try await render.value }
        #expect(await eventually { !HoldingChatter.deletes.isEmpty })
        await settle()
        #expect(HoldingChatter.deletes.map { $0.url!.path } == ["/v1/jobs/poll-1"])
    }

    /// Cancelled after the job completed, while its audio downloads: the download is
    /// stopped and at most one DELETE goes out, for that job only.
    @Test func cancellingWhileTheAudioDownloadsSendsAtMostOneDelete() async throws {
        chatter32(id: "done-1", lookup: { _ in .now(200, json(["state": "completed"])) }, audio: .hold)
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(await eventually { HoldingChatter.requests.contains { $0.url!.path == "/v1/jobs/done-1/audio" } })
        render.cancel()
        await #expect(throws: (any Error).self) { try await render.value }
        await settle()
        let deletes = HoldingChatter.deletes
        #expect(deletes.count <= 1 && deletes.allSatisfy { $0.url!.path == "/v1/jobs/done-1" })
        #expect(HoldingChatter.lock.withLock { HoldingChatter.stopped.map(\.path) } == ["/v1/jobs/done-1/audio"])
    }

    /// The Speaker cancels its render task in `say()`'s defer, after the render returned:
    /// a finished render is never cancelled in Chatter.
    @Test func cancellingAFinishedRenderSendsNoDelete() async throws {
        chatter32(id: "fin-1", lookup: { _ in .now(200, json(["state": "completed"])) })
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(try await render.value == Data("RIFFnew".utf8))
        render.cancel()
        render.cancel()
        await settle()
        #expect(HoldingChatter.deletes.isEmpty)
        #expect(HoldingChatter.requests.map { "\($0.httpMethod ?? "GET") \($0.url!.path)" }
                == ["POST /v1/speech", "GET /v1/jobs/fin-1", "GET /v1/jobs/fin-1/audio"])
    }

    /// Cancelling over and over (an interrupt, then say()'s defer) sends one DELETE.
    @Test func cancellingRepeatedlySendsOneDelete() async throws {
        chatter32(id: "many-1", lookup: { _ in .hold })
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(await eventually { !HoldingChatter.lookups("many-1").isEmpty })
        for _ in 0..<5 { render.cancel() }
        _ = await render.result
        render.cancel()
        #expect(await eventually { !HoldingChatter.deletes.isEmpty })
        await settle()
        #expect(HoldingChatter.deletes.count == 1)
    }

    /// A save from an older Chatter held in a lookup is never cancelled, wherever the
    /// cancel lands, since that Chatter's cancel also stops what plays on the Mac.
    @Test func aSaveIsNeverCancelledEvenDuringAHeldLookup() async throws {
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"):
                mode(request) == "render" ? .now(400, json(["error": "Mode must be play or save."])) : .now(202, json(["id": "old-3"]))
            case ("GET", "/v1/jobs/old-3"): .hold
            default: .now(404, Data())
            }
        }
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(await eventually { !HoldingChatter.lookups("old-3").isEmpty })
        render.cancel()
        _ = await render.result
        await settle()
        #expect(HoldingChatter.deletes.isEmpty)
    }

    /// Chatter cancelled the job itself (another client, or a restart): the render fails
    /// as cancelled, and nothing is sent back.
    @Test func aJobChatterCancelledFailsWithoutADelete() async throws {
        chatter32(id: "gone-1", lookup: { _ in .now(200, json(["state": "cancelled"])) })
        do {
            _ = try await HoldingChatter.client().render("Hi.", voice: "Scar")
            Issue.record("a cancelled job returned audio")
        } catch {
            #expect("\(error)" == "Chatter's rendering cancelled")
        }
        await settle()
        #expect(HoldingChatter.deletes.isEmpty && HoldingChatter.lookups("gone-1").count == 1)
    }

    /// Load: twenty renders in flight at once, all cancelled: each job is cancelled exactly
    /// once, and only the jobs that were rendering.
    @Test func manyRendersCancelledAtOnceEachCancelTheirOwnJob() async throws {
        let ids = Counter()
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"): .now(202, json(["id": "load-\(ids.next())"]))
            case ("DELETE", _): .now(200, json(["state": "cancelled"]))
            default: .hold
            }
        }
        let client = HoldingChatter.client()
        let renders = (0..<20).map { index in Task { try await client.render("Piece \(index).", voice: "Scar") } }
        #expect(await eventually { HoldingChatter.requests.filter { ($0.httpMethod ?? "GET") == "GET" }.count == 20 })
        for render in renders { render.cancel() }
        for render in renders { _ = await render.result }
        #expect(await eventually { HoldingChatter.deletes.count == 20 })
        await settle()
        let deleted = HoldingChatter.deletes.map { $0.url!.path }
        #expect(deleted.count == 20 && Set(deleted) == Set((0..<20).map { "/v1/jobs/load-\($0)" }))
    }

    // MARK: The fallback to save

    /// Only a 400 whose explanation begins "Mode must be" means an older Chatter; any other
    /// refusal is reported as it is, after one POST.
    @Test func onlyAModeRefusalFallsBackToSave() async throws {
        let refusals: [(Int, Data, String)] = [
            (400, json(["error": "Text must not be empty."]), "Text must not be empty"),
            (400, Data(), "(400)"),
            (400, Data("Mode must be play or save.".utf8), "(400)"),  // not JSON: no explanation
            (400, json(["error": ["Mode must be play or save."]]), "(400)"),  // not a string
            (400, json(["message": "Mode must be play or save."]), "(400)"),
            (400, json(["error": "mode must be play or save."]), "mode must be"),
            (400, json(["error": " Mode must be play or save."]), "Mode must be"),
            (400, json(["error": "The Mode must be set."]), "Mode must be"),
            (404, json(["error": "Mode must be play or save."]), "(404)"),
            (422, json(["error": "Mode must be play or save."]), "(422)"),
            (503, json(["error": "Mode must be play or save."]), "(503)"),
        ]
        for (status, body, words) in refusals {
            HoldingChatter.answer = { _ in .now(status, body) }
            do {
                _ = try await HoldingChatter.client().render("Hi.", voice: "Scar")
                Issue.record("\(status) \(String(decoding: body, as: UTF8.self)) returned audio")
            } catch {
                #expect("\(error)".contains(words) && "\(error)".contains("/v1/speech"), "\(error)")
                #expect(!"\(error)".contains(HoldingChatter.token))
            }
            let posts = HoldingChatter.requests
            #expect(posts.count == 1 && mode(posts[0]) == "render", "\(status) \(String(decoding: body, as: UTF8.self))")
        }
    }

    /// The fallback happens once: a save refused the same way (or any way) is the error,
    /// never a third POST, and nothing is cancelled.
    @Test func theFallbackNeverLoops() async throws {
        let saveAnswers: [(Int, Data, String)] = [
            (400, json(["error": "Mode must be play or save."]), "Mode must be play or save."),
            (503, json(["error": "The speech engine is starting"]), "starting"),
            (400, Data(), "(400)"),
        ]
        for (status, body, words) in saveAnswers {
            HoldingChatter.answer = { request in
                mode(request) == "render" ? .now(400, json(["error": "Mode must be play or save."])) : .now(status, body)
            }
            do {
                _ = try await HoldingChatter.client().render("Hi.", voice: "Scar")
                Issue.record("a refused save returned audio")
            } catch {
                #expect("\(error)".contains(words), "\(error)")
            }
            await settle()
            #expect(HoldingChatter.requests.map(mode) == ["render", "save"], "\(status)")
            #expect(HoldingChatter.deletes.isEmpty)
        }
    }

    /// The fallback's save carries the same voice and text and no quality; a render
    /// carries the client's quality.
    @Test func theRenderCarriesItsQualityAndTheSaveNone() async throws {
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"):
                mode(request) == "render" ? .now(400, json(["error": "Mode must be play or save."])) : .now(202, json(["id": "q-1"]))
            case ("GET", "/v1/jobs/q-1"): .now(200, json(["state": "completed"]))
            default: .now(200, Data("RIFF".utf8))
            }
        }
        _ = try await HoldingChatter.client(quality: "studio").render("Hello there.", voice: "6D86-AB")
        let bodies = HoldingChatter.requests.filter { $0.httpMethod == "POST" }
            .map { (try? JSONSerialization.jsonObject(with: $0.httpBody ?? Data())) as? [String: String] }
        #expect(bodies == [["voice": "6D86-AB", "text": "Hello there.", "mode": "render", "quality": "studio"],
                           ["voice": "6D86-AB", "text": "Hello there.", "mode": "save"]])
        #expect(HoldingChatter.requests.filter { $0.httpMethod == "POST" }
            .allSatisfy { $0.value(forHTTPHeaderField: "Content-Type") == "application/json" && $0.url!.query == nil })
    }

    /// A refusal carries Chatter's status and its explanation, cleaned for a terminal;
    /// a failure of the bridge's own carries neither.
    @Test func refusalsCarryTheirStatusAndCleanedDetail() async throws {
        HoldingChatter.answer = { _ in .now(409, json(["error": "Busy\u{1B}[2J now"])) }
        do {
            _ = try await HoldingChatter.client().render("Hi.", voice: "Scar")
            Issue.record("a refusal returned audio")
        } catch let error as SpeechError {
            #expect(error.status == 409 && error.detail == "Busy?[2J now")
            #expect(error.description == "Chatter refused /v1/speech (409): Busy?[2J now")
        }
        HoldingChatter.answer = { _ in .now(500, Data()) }
        do {
            _ = try await HoldingChatter.client().render("Hi.", voice: "Scar")
        } catch let error as SpeechError {
            #expect(error.status == 500 && error.detail == nil && error.description == "Chatter refused /v1/speech (500)")
        }
        let local = SpeechError("Chatter took too long to render")
        #expect(local.status == nil && local.detail == nil)
    }

    // MARK: Lookups

    /// Property: for any valid job id (1 to 64 letters, digits or dashes), each lookup is
    /// exactly /v1/jobs/{id}?wait=20, unescaped, the audio and the cancel name the same
    /// job without a query, and the token appears in no URL. Seeded, so a failure replays.
    @Test(arguments: 0..<12)
    func lookupsNameTheJobExactlyAndWaitTwentySeconds(_ seed: Int) async throws {
        var state = UInt64(seed) &* 0x9E37_79B9_7F4A_7C15 | 1
        func next(_ bound: Int) -> Int {
            state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
            return Int((state >> 33) % UInt64(bound))
        }
        let alphabet = Array("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")
        let length = seed == 0 ? 64 : seed == 1 ? 1 : 1 + next(64)
        let id = String((0..<length).map { _ in next(4) == 0 ? "-" : alphabet[next(alphabet.count)] })
        let polls = Counter()
        chatter32(id: id, lookup: { _ in .now(200, json(["state": polls.next() < 1 ? "generating" : "completed"])) },
                  audio: .after(.milliseconds(30), 200, Data("RIFF".utf8)))
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        if seed % 2 == 1 {
            // Odd seeds cancel during the audio download, so the DELETE's URL is checked too.
            #expect(await eventually { HoldingChatter.requests.contains { $0.url!.path.hasSuffix("/audio") } }, "seed \(seed)")
            render.cancel()
            _ = await render.result
            await settle()
        } else {
            #expect(try await render.value == Data("RIFF".utf8), "seed \(seed): \(id)")
        }
        let base = "http://127.0.0.1:18423/v1/jobs/\(id)"
        let lookups = HoldingChatter.lookups(id)
        #expect(lookups.count == 2 && lookups.allSatisfy { $0.url!.absoluteString == base + "?wait=20" }, "seed \(seed): \(id)")
        let items = lookups.first.flatMap { URLComponents(url: $0.url!, resolvingAgainstBaseURL: false)?.queryItems }
        #expect(items == [URLQueryItem(name: "wait", value: "\(ChatterClient.waitSeconds)")], "seed \(seed)")
        let others = HoldingChatter.requests.filter { $0.url!.path != "/v1/speech" && !lookups.contains($0) }
        #expect(others.allSatisfy { $0.url!.absoluteString == base + "/audio" || $0.url!.absoluteString == base }, "seed \(seed)")
        for request in HoldingChatter.requests {
            #expect(!request.url!.absoluteString.contains(HoldingChatter.token), "seed \(seed)")
            #expect(!String(decoding: request.httpBody ?? Data(), as: UTF8.self).contains(HoldingChatter.token), "seed \(seed)")
            #expect(request.value(forHTTPHeaderField: "Authorization") == "Bearer \(HoldingChatter.token)", "seed \(seed)")
        }
    }

    /// An older Chatter answers each lookup at once: it is polled every 100 ms, not in a
    /// tight loop, and the render gives up near its timeout.
    @Test func quickLookupsArePolledEveryHundredMillisecondsUntilTheTimeout() async throws {
        chatter32(id: "slow-1", lookup: { _ in .now(200, json(["state": "generating"])) })
        let started = ContinuousClock.now
        do {
            _ = try await HoldingChatter.client(timeout: .milliseconds(600)).render("Hi.", voice: "Scar")
            Issue.record("a render that never finished returned audio")
        } catch {
            #expect("\(error)" == "Chatter took too long to render")
        }
        let elapsed = ContinuousClock.now - started
        let count = HoldingChatter.lookups("slow-1").count
        #expect(elapsed >= .milliseconds(600) && elapsed < .milliseconds(1500), "\(elapsed)")
        #expect((4...8).contains(count), "\(count) lookups in \(elapsed)")
    }

    /// A lookup Chatter holds past the client's timeout is answered before the render gives
    /// up: the deadline is checked between lookups, and no extra lookup follows it.
    @Test func aHeldLookupThatOutlastsTheTimeoutEndsTheRender() async throws {
        chatter32(id: "held-2", lookup: { _ in .after(.milliseconds(400), 200, json(["state": "generating"])) })
        let started = ContinuousClock.now
        await #expect(throws: SpeechError.self) {
            try await HoldingChatter.client(timeout: .milliseconds(150)).render("Hi.", voice: "Scar")
        }
        #expect(ContinuousClock.now - started < .seconds(1))
        #expect(HoldingChatter.lookups("held-2").count == 1)
        // A render given up on is cancelled in Chatter, so the next reply need not wait for it.
        #expect(await eventually { HoldingChatter.deletes.map(\.url!.path) == ["/v1/jobs/held-2"] })
    }

    /// An older Chatter's save that times out is left alone: its cancel would silence the Mac.
    @Test func aSaveThatTimesOutIsNeverCancelled() async throws {
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"):
                let body = (try? JSONSerialization.jsonObject(with: request.httpBody ?? Data()) as? [String: String]) ?? [:]
                return body["mode"] == "render" ? .now(400, json(["error": "Mode must be play or save."])) : .now(202, json(["id": "old-slow"]))
            default: return .now(200, json(["state": "generating"]))
            }
        }
        await #expect(throws: SpeechError.self) {
            try await HoldingChatter.client(timeout: .milliseconds(200)).render("Hi.", voice: "Scar")
        }
        await settle()
        #expect(HoldingChatter.deletes.isEmpty)
    }

    /// A lookup without a state, or with one this client does not know, is not taken for
    /// completion: the render waits on, then times out without fetching audio.
    @Test func anUnknownStateIsNeverTakenForCompletion() async throws {
        for reply in [json([String: String]()), json(["state": "Completed"]), json(["state": 1]), json(["status": "completed"])] {
            chatter32(id: "odd-1", lookup: { _ in .now(200, reply) })
            await #expect(throws: SpeechError.self) {
                try await HoldingChatter.client(timeout: .milliseconds(150)).render("Hi.", voice: "Scar")
            }
            #expect(!HoldingChatter.requests.contains { $0.url!.path.hasSuffix("/audio") })
        }
    }

    /// A lookup Chatter refuses (the job is gone) ends the render with that refusal.
    @Test func aRefusedLookupEndsTheRender() async throws {
        chatter32(id: "lost-1", lookup: { _ in .now(404, json(["error": "No such job."])) })
        do {
            _ = try await HoldingChatter.client().render("Hi.", voice: "Scar")
            Issue.record("a lost job returned audio")
        } catch {
            #expect("\(error)" == "Chatter refused /v1/jobs/lost-1 (404): No such job.")
        }
        #expect(HoldingChatter.lookups("lost-1").count == 1 && HoldingChatter.deletes.isEmpty)
    }
}

/// Holds a task so a fake can cancel it from inside a request.
final class TaskBox: @unchecked Sendable {
    private let lock = NSLock()
    private var task: Task<Data, Error>?
    private var pending = false

    func set(_ task: Task<Data, Error>) {
        let cancelNow = lock.withLock { () -> Bool in self.task = task; return pending }
        if cancelNow { task.cancel() }
    }

    func cancel() {
        let task = lock.withLock { () -> Task<Data, Error>? in pending = true; return self.task }
        task?.cancel()
    }
}

// MARK: Giving up after the timeout, with and without a cancel

/// Runs `body` with standard error sent to a file and returns what was written to it. A file, not
/// a pipe, so nothing else writing to standard error meanwhile can fill a buffer and block.
private func capturingStandardError(until done: @Sendable (String) -> Bool, _ body: () async -> Void) async -> String {
    let file = FileManager.default.temporaryDirectory.appendingPathComponent("stderr-\(UUID())")
    FileManager.default.createFile(atPath: file.path, contents: nil)
    defer { try? FileManager.default.removeItem(at: file) }
    let handle = try! FileHandle(forWritingTo: file)
    let saved = dup(STDERR_FILENO)
    dup2(handle.fileDescriptor, STDERR_FILENO)
    await body()
    let deadline = ContinuousClock.now + .seconds(2)
    var text = ""
    repeat {
        text = (try? String(contentsOf: file, encoding: .utf8)) ?? ""
        if done(text) { break }
        try? await Task.sleep(for: .milliseconds(10))
    } while ContinuousClock.now < deadline
    dup2(saved, STDERR_FILENO)
    close(saved)
    try? handle.close()
    return (try? String(contentsOf: file, encoding: .utf8)) ?? text
}

extension ChatterTests {
    /// A render that gave up after its timeout has already sent its one DELETE: cancelling the task
    /// afterwards (say()'s defer, an interrupt) sends no second one.
    @Test func cancellingAfterTheTimeoutSendsNoSecondDelete() async throws {
        chatter32(id: "late-1", lookup: { _ in .after(.milliseconds(250), 200, json(["state": "generating"])) })
        let client = HoldingChatter.client(timeout: .milliseconds(100))
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        do {
            _ = try await render.value
            Issue.record("a render that never finished returned audio")
        } catch {
            #expect("\(error)" == "Chatter took too long to render")
        }
        for _ in 0..<3 { render.cancel() }
        #expect(await eventually { !HoldingChatter.deletes.isEmpty })
        await settle()
        #expect(HoldingChatter.deletes.map(\.url!.path) == ["/v1/jobs/late-1"])
    }

    /// The deadline has passed and Chatter holds the last lookup when the task is cancelled: the
    /// cancel sends the DELETE and the stopped lookup ends the render, so the timeout sends none.
    @Test func aCancelDuringALookupPastTheDeadlineSendsOneDelete() async throws {
        chatter32(id: "past-1", lookup: { _ in .hold })
        let client = HoldingChatter.client(timeout: .zero)
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(await eventually { !HoldingChatter.lookups("past-1").isEmpty })
        render.cancel()
        await #expect(throws: (any Error).self) { try await render.value }
        #expect(await eventually { !HoldingChatter.deletes.isEmpty })
        await settle()
        #expect(HoldingChatter.deletes.map(\.url!.path) == ["/v1/jobs/past-1"])
        #expect(HoldingChatter.lookups("past-1").count == 1)
    }

    /// The task is cancelled as a lookup past the deadline answers "generating" (just before, or a
    /// few hundred microseconds after, while the reply is read): the cancel and the timeout both give
    /// up on the job, and it is cancelled once. With the two paths not sharing one `RunOnce`, cancels
    /// landing 50 to 150 µs after the answer send two DELETEs nearly every time (10 to 11 of the 24
    /// attempts on an M-series Mac); at 150 µs alone it was 2 to 5, so the delays are spread to keep
    /// that window covered on a slower or busier machine.
    @Test(arguments: 0..<24)
    func aCancelAsTheLastLookupAnswersSendsOneDelete(_ attempt: Int) async throws {
        let id = "race-\(attempt)"
        let box = TaskBox()
        let delay = [0, 50, 100, 150, 200, 300][attempt % 6]  // microseconds after the answer; 0 cancels before it
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"): return .now(202, json(["id": id]))
            case ("GET", "/v1/jobs/\(id)"):
                if delay == 0 { box.cancel() } else {
                    DispatchQueue.global().asyncAfter(deadline: .now() + .microseconds(delay)) { box.cancel() }
                }
                return .now(200, json(["state": "generating"]))
            case ("DELETE", _): return .now(200, json(["state": "cancelled"]))
            default: return .now(404, Data())
            }
        }
        let client = HoldingChatter.client(timeout: .zero)
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        box.set(render)
        _ = await render.result
        #expect(await eventually { !HoldingChatter.deletes.isEmpty }, "attempt \(attempt)")
        await settle()
        #expect(HoldingChatter.deletes.map(\.url!.path) == ["/v1/jobs/\(id)"], "attempt \(attempt), cancel \(delay) µs after")
    }

    /// The deadline passes while Chatter finishes the job: a lookup that reports it completed is
    /// fetched, never cancelled; one that reports it failed or cancelled ends the render, with no DELETE.
    @Test(arguments: ["completed", "failed", "cancelled"])
    func theTimeoutNeverCancelsAJobChatterHasFinished(_ state: String) async throws {
        for (timeout, lookup) in [(Duration.zero, HoldingChatter.Answer.now(200, json(["state": state]))),
                                  (.milliseconds(50), .after(.milliseconds(200), 200, json(["state": state])))] {
            chatter32(id: "fin-\(state)", lookup: { _ in lookup })
            let outcome = await Result { try await HoldingChatter.client(timeout: timeout).render("Hi.", voice: "Scar") }
            switch (state, outcome) {
            case ("completed", .success(let audio)): #expect(audio == Data("RIFFnew".utf8))
            case (_, .failure(let error)) where state != "completed": #expect("\(error)" == "Chatter's rendering \(state)")
            default: Issue.record("\(state) after \(timeout): \(outcome)")
            }
            await settle()
            #expect(HoldingChatter.deletes.isEmpty, "\(state) after \(timeout)")
            #expect(HoldingChatter.lookups("fin-\(state)").count == 1, "\(state) after \(timeout)")
        }
    }

    /// A save that times out and is then cancelled is still never cancelled in Chatter.
    @Test func aSaveThatTimesOutAndIsCancelledIsNeverCancelled() async throws {
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"):
                mode(request) == "render" ? .now(400, json(["error": "Mode must be play or save."])) : .now(202, json(["id": "old-late"]))
            default: .after(.milliseconds(150), 200, json(["state": "generating"]))
            }
        }
        let client = HoldingChatter.client(timeout: .milliseconds(50))
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        await #expect(throws: SpeechError.self) { try await render.value }
        render.cancel()
        await settle()
        #expect(HoldingChatter.deletes.isEmpty)
        #expect(HoldingChatter.requests.compactMap(mode) == ["render", "save"])
    }

    /// Whatever the DELETE meets (a refusal, a reply that is not JSON, no answer at all), the caller
    /// gets its own error at once: the timeout's, or the cancel's. The DELETE's failure only reaches
    /// standard error, as one line naming the job and never the token.
    @Test(arguments: [0, 1, 2])
    func aFailingDeleteNeverReachesTheCaller(_ kind: Int) async throws {
        let id = "nodel-\(kind)"
        let deleteAnswer: HoldingChatter.Answer = [.now(500, json(["error": "Engine \u{1B}[31mbusy"])), .now(403, Data("<html>".utf8)),
                                                   .hold][kind]
        HoldingChatter.answer = { request in
            switch (request.httpMethod ?? "GET", request.url!.path) {
            case ("POST", "/v1/speech"): .now(202, json(["id": id]))
            case ("DELETE", _): deleteAnswer
            default: .after(.milliseconds(120), 200, json(["state": "generating"]))
            }
        }
        // Gives up after its timeout.
        var thrown = ""
        var took = Duration.zero
        let log = await capturingStandardError(until: { kind == 2 || $0.contains("render \(id):") }) {
            let started = ContinuousClock.now
            do {
                _ = try await HoldingChatter.client(timeout: .milliseconds(50)).render("Hi.", voice: "Scar")
            } catch { thrown = "\(error)" }
            took = ContinuousClock.now - started
        }
        #expect(thrown == "Chatter took too long to render", "\(kind)")
        #expect(took < .seconds(1), "\(kind): \(took)")
        #expect(await eventually { !HoldingChatter.deletes.isEmpty }, "\(kind)")
        #expect(HoldingChatter.deletes.map(\.url!.path) == ["/v1/jobs/\(id)"], "\(kind)")
        let lines = log.split(separator: "\n").filter { $0.contains("Chatter did not cancel render \(id)") }
        if kind == 2 {
            #expect(lines.isEmpty, "a DELETE still in flight has not failed")  // it is stopped only by the session's timeout
        } else {
            #expect(lines.count == 1, "\(kind): \(log)")
            let expected = kind == 0 ? "Chatter did not cancel render \(id): Chatter refused /v1/jobs/\(id) (500): Engine ?[31mbusy"
                                     : "Chatter did not cancel render \(id): Chatter refused /v1/jobs/\(id) (403)"
            #expect(lines.first.map(String.init) == expected, "\(kind)")
        }
        #expect(!log.contains(HoldingChatter.token), "\(kind)")

        // Cancelled by its caller: the caller sees the cancel, not the DELETE's failure, at once.
        HoldingChatter.lock.withLock { HoldingChatter.seen = [] }
        let client = HoldingChatter.client()
        let render = Task { try await client.render("Hi.", voice: "Scar") }
        #expect(await eventually { !HoldingChatter.lookups(id).isEmpty })
        let started = ContinuousClock.now
        render.cancel()
        let outcome = await render.result
        #expect(ContinuousClock.now - started < .seconds(1), "\(kind)")
        if case .failure(let error) = outcome {
            #expect(!"\(error)".contains("did not cancel") && !"\(error)".contains("/v1/jobs/\(id) (") , "\(kind): \(error)")
        } else {
            Issue.record("\(kind): a cancelled render returned audio")
        }
        #expect(await eventually { HoldingChatter.deletes.count == 1 }, "\(kind)")
    }
}

private extension Result where Failure == any Error {
    init(catching body: () async throws -> Success) async {
        do { self = .success(try await body()) } catch { self = .failure(error) }
    }
}

extension ChatterTests {
    /// Contention: a thousand callers on many threads at once run a `RunOnce`'s action exactly once.
    @Test func runOnceRunsItsActionOnceUnderContention() async {
        for round in 0..<20 {
            let runs = Counter()
            let once = RunOnce { _ = runs.next() }
            DispatchQueue.concurrentPerform(iterations: 1_000) { _ in once() }
            await withTaskGroup(of: Void.self) { group in for _ in 0..<50 { group.addTask { once() } } }
            #expect(runs.next() == 1, "round \(round)")
        }
    }
}
