import Foundation

public struct SpeechError: Error, CustomStringConvertible, Sendable {
    public let description: String
    /// Chatter's HTTP status and explanation, when Chatter refused the request.
    let status: Int?
    let detail: String?
    public init(_ description: String) { self.description = description; status = nil; detail = nil }
    init(_ description: String, status: Int, detail: String?) {
        self.description = description; self.status = status; self.detail = detail
    }
}

/// Chatter's local REST API (127.0.0.1:18423), which renders Loki's spoken replies
/// in the user's chosen voice, Scar by default (RFC-003). The bearer token is read from
/// Chatter's token file for each request and never logged or sent anywhere else. The
/// session follows no redirects and uses no proxy, cache or cookies, and replies are
/// size-capped, so whatever answers on the port can only refuse or send audio.
///
/// Known limit (memo-027): like Chatter's own clients, this trusts whatever listens on
/// the port; a process that took it while Chatter was not running would receive the
/// token. Closing that belongs to Chatter (a socket in a private folder, or a
/// challenge instead of a bearer token).
public struct ChatterClient: Sendable {
    public struct Voice: Equatable, Sendable {
        public let id: String
        public let name: String
        public let ready: Bool
    }

    public static let defaultURL = URL(string: "http://127.0.0.1:18423")!
    public static var defaultTokenFile: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/Chatter/api-token")
    }

    let base: URL
    let tokenFile: URL
    let session: URLSession
    /// Longest wait for one rendering.
    let timeout: Duration
    /// The quality a render asks for. Responsive renders Scar's 4 s reply in about 1 s, where
    /// Studio (what `save` forces) takes about real time (memo-031).
    let quality: String
    public static let defaultQuality = "responsive"
    /// How long Chatter may hold a job lookup open: inside the session's 30 s request timeout.
    static let waitSeconds = 20

    /// The default session: private, and never redirected.
    public static let privateSession: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.connectionProxyDictionary = [:]
        configuration.urlCache = nil
        configuration.httpCookieStorage = nil
        configuration.timeoutIntervalForRequest = 30
        configuration.timeoutIntervalForResource = 60
        return URLSession(configuration: configuration, delegate: RefuseRedirects(), delegateQueue: nil)
    }()

    /// Largest reply accepted: JSON, and a rendered WAV (about 5 minutes of 24 kHz).
    static let maxJSON = 64 * 1024, maxAudio = 16 * 1024 * 1024

    public init(base: URL = defaultURL, tokenFile: URL = defaultTokenFile, session: URLSession = privateSession,
                timeout: Duration = .seconds(120), quality: String = defaultQuality) {
        self.base = base
        self.tokenFile = tokenFile
        self.session = session
        self.timeout = timeout
        self.quality = quality
    }

    /// The token, from a regular file of this user's that no one else can read.
    private func token() throws -> String {
        var info = stat()
        guard lstat(tokenFile.path, &info) == 0 else {
            throw SpeechError("Chatter's API token is not readable; is Chatter installed and set up?")
        }
        guard info.st_mode & S_IFMT == S_IFREG, info.st_uid == getuid(), info.st_mode & 0o077 == 0, info.st_size <= 4096,
              let text = try? String(contentsOf: tokenFile, encoding: .utf8)
        else {
            throw SpeechError("Chatter's API token file must be a small file of yours that only you can read")
        }
        let token = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !token.isEmpty, token.utf8.allSatisfy({ (0x21...0x7E).contains($0) }) else {
            throw SpeechError("Chatter's API token file is empty or damaged")
        }
        return token
    }

    private func call(_ path: String, method: String? = nil, query: [URLQueryItem] = [], body: [String: Any]? = nil,
                      limit: Int = maxJSON) async throws -> Data {
        var url = base.appendingPathComponent(path)
        if !query.isEmpty, var components = URLComponents(url: url, resolvingAgainstBaseURL: false) {
            components.queryItems = query
            url = components.url ?? url
        }
        var request = URLRequest(url: url)
        request.timeoutInterval = 30
        request.setValue("Bearer " + (try token()), forHTTPHeaderField: "Authorization")
        if let method { request.httpMethod = method }
        if let body {
            request.httpMethod = "POST"
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        }
        let (data, response) = try await fetch(request, limit: limit)
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            let detail = (try? JSONSerialization.jsonObject(with: data) as? [String: Any])?["error"] as? String
            throw SpeechError("Chatter refused \(path) (\(status))\(detail.map { ": \(Self.printable($0))" } ?? "")",
                              status: status, detail: detail.map(Self.printable))
        }
        return data
    }

    /// The response, refused once it grows past limit.
    private func fetch(_ request: URLRequest, limit: Int) async throws -> (Data, URLResponse) {
        do {
            let (bytes, response) = try await session.bytes(for: request)
            guard response.expectedContentLength <= Int64(limit) else { throw SpeechError("Chatter's reply is too large") }
            var data = Data()
            for try await byte in bytes {
                guard data.count < limit else { throw SpeechError("Chatter's reply is too large") }
                data.append(byte)
            }
            return (data, response)
        } catch let error as SpeechError {
            throw error
        } catch {
            throw SpeechError("Chatter is not answering; is it running? (\(Self.printable(error.localizedDescription)))")
        }
    }

    /// Text from Chatter fit for a terminal: no control characters, and none of the
    /// invisible ones that reorder or hide text (bidi controls, zero-width characters,
    /// line and paragraph separators); at most 200 bytes.
    static func printable(_ text: String) -> String {
        let scalars = text.unicodeScalars.map { scalar -> Character in
            let hidden = [.format, .lineSeparator, .paragraphSeparator].contains(scalar.properties.generalCategory)
            return scalar.value < 0x20 || (0x7F...0x9F).contains(scalar.value) || hidden ? "?" : Character(scalar)
        }
        var out = ""
        for character in scalars {
            guard out.utf8.count + character.utf8.count <= 200 else { break }
            out.append(character)
        }
        return out
    }

    private func object(_ data: Data) throws -> [String: Any] {
        guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw SpeechError("Chatter sent a reply that is not a JSON object")
        }
        return object
    }

    public func voices() async throws -> [Voice] {
        guard let list = try object(try await call("/v1/voices"))["voices"] as? [[String: Any]] else {
            throw SpeechError("Chatter's voice list is missing")
        }
        return list.compactMap { entry in
            guard let id = entry["id"] as? String, let name = entry["name"] as? String else { return nil }
            return Voice(id: id, name: name, ready: entry["ready"] as? Bool ?? false)
        }
    }

    /// Renders text in a voice (its name or id) and returns Chatter's WAV, without
    /// playing it on the Mac: Chatter 3.2's silent `render` mode at `quality`, or, from an
    /// older Chatter that has no render mode, `save` (always Studio). Cancelling the task
    /// that awaits a render also cancels it in Chatter, so the next render need not wait
    /// for it; a `save` is never cancelled, since an older Chatter's cancel also stops
    /// whatever it is playing on the Mac.
    public func render(_ text: String, voice: String) async throws -> Data {
        // A task already cancelled asks for nothing: URLSession would still send the POST,
        // and the job it started could not be cancelled, its id never read. A cancel that
        // lands while the POST is in flight stops it at once; Chatter may then finish that
        // job unasked, which on the local socket is a window of milliseconds (memo-031).
        try Task.checkCancellation()
        let (id, cancellable) = try await submit(text, voice: voice)
        // The caller's cancel and the timeout can both give up on the job, even at once; it is
        // cancelled in Chatter once.
        let giveUp = RunOnce { if cancellable { cancel(id) } }
        return try await withTaskCancellationHandler {
            try await finish(id, giveUp: giveUp)
        } onCancel: {
            giveUp()
        }
    }

    /// Queues the speech: Chatter 3.2's silent render, or `save` on an older Chatter that
    /// refuses render ("Mode must be play or save."). The flag says whether the job may be
    /// cancelled.
    private func submit(_ text: String, voice: String) async throws -> (id: String, cancellable: Bool) {
        do {
            let job = try object(try await call("/v1/speech", body: ["voice": voice, "text": text, "mode": "render", "quality": quality]))
            return (try Self.jobID(job), true)
        } catch let error as SpeechError where error.status == 400 && error.detail?.hasPrefix("Mode must be") == true {
            let job = try object(try await call("/v1/speech", body: ["voice": voice, "text": text, "mode": "save"]))
            return (try Self.jobID(job), false)
        }
    }

    /// Waits for the job and fetches its audio. Chatter 3.2 holds each lookup until the
    /// job finishes (`?wait=`); an older Chatter answers at once and is polled. A render
    /// given up on for taking too long is cancelled (`giveUp`), so the next one need not wait for it.
    private func finish(_ id: String, giveUp: RunOnce) async throws -> Data {
        let deadline = ContinuousClock.now + timeout
        while true {
            let state = try object(try await call("/v1/jobs/\(id)", query: [URLQueryItem(name: "wait", value: "\(Self.waitSeconds)")]))["state"]
                as? String
            if state == "completed" { break }
            if state == "failed" || state == "cancelled" { throw SpeechError("Chatter's rendering \(state!)") }
            guard ContinuousClock.now < deadline else {
                giveUp()
                throw SpeechError("Chatter took too long to render")
            }
            try await Task.sleep(for: .milliseconds(100))
        }
        return try await call("/v1/jobs/\(id)/audio", limit: Self.maxAudio)
    }

    /// Asks Chatter to stop a render that is no longer wanted. It runs outside the
    /// cancelled task, which would otherwise cancel this request too. A failure means only
    /// that Chatter finishes the render, as before; it is reported on standard error.
    func cancel(_ id: String) {
        let client = self
        Task.detached {
            do { _ = try await client.call("/v1/jobs/\(id)", method: "DELETE") } catch {
                FileHandle.standardError.write(Data("Chatter did not cancel render \(id): \(error)\n".utf8))
            }
        }
    }

    /// A job id from Chatter: 1 to 64 letters, digits or dashes, so it can only name a job.
    static func jobID(_ job: [String: Any]) throws -> String {
        guard let id = (job["id"] ?? job["jobID"]) as? String, (1...64).contains(id.utf8.count),
              id.utf8.allSatisfy({ (0x30...0x39).contains($0) || (0x41...0x5A).contains($0) || (0x61...0x7A).contains($0) || $0 == 0x2D })
        else { throw SpeechError("Chatter did not return a job id") }
        return id
    }
}

/// Runs its action the first time it is called, from whichever task, and never again.
final class RunOnce: @unchecked Sendable {
    private let lock = NSLock()
    private var done = false
    private let action: @Sendable () -> Void
    init(_ action: @escaping @Sendable () -> Void) { self.action = action }
    func callAsFunction() {
        guard lock.withLock({ () -> Bool in defer { done = true }; return !done }) else { return }
        action()
    }
}

/// Chatter never redirects; a redirect is refused rather than followed.
final class RefuseRedirects: NSObject, URLSessionTaskDelegate, Sendable {
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest) async -> URLRequest? { nil }
}
