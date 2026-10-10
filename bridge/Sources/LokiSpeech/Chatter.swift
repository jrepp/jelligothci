import Foundation

public struct SpeechError: Error, CustomStringConvertible, Sendable {
    public let description: String
    public init(_ description: String) { self.description = description }
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
                timeout: Duration = .seconds(120)) {
        self.base = base
        self.tokenFile = tokenFile
        self.session = session
        self.timeout = timeout
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

    private func call(_ path: String, body: [String: Any]? = nil, limit: Int = maxJSON) async throws -> Data {
        var request = URLRequest(url: base.appendingPathComponent(path))
        request.timeoutInterval = 30
        request.setValue("Bearer " + (try token()), forHTTPHeaderField: "Authorization")
        if let body {
            request.httpMethod = "POST"
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
            request.httpBody = try JSONSerialization.data(withJSONObject: body)
        }
        let (data, response) = try await fetch(request, limit: limit)
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            let status = (response as? HTTPURLResponse)?.statusCode ?? 0
            let detail = (try? JSONSerialization.jsonObject(with: data) as? [String: Any])?["error"] as? String
            throw SpeechError("Chatter refused \(path) (\(status))\(detail.map { ": \(Self.printable($0))" } ?? "")")
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
    /// playing it on the Mac.
    public func render(_ text: String, voice: String) async throws -> Data {
        let job = try object(try await call("/v1/speech", body: ["voice": voice, "text": text, "mode": "save"]))
        guard let id = (job["id"] ?? job["jobID"]) as? String, (1...64).contains(id.utf8.count),
              id.utf8.allSatisfy({ (0x30...0x39).contains($0) || (0x41...0x5A).contains($0) || (0x61...0x7A).contains($0) || $0 == 0x2D })
        else { throw SpeechError("Chatter did not return a job id") }
        let deadline = ContinuousClock.now + timeout
        while true {
            let state = try object(try await call("/v1/jobs/\(id)"))["state"] as? String
            if state == "completed" { break }
            if state == "failed" || state == "cancelled" { throw SpeechError("Chatter's rendering \(state!)") }
            guard ContinuousClock.now < deadline else { throw SpeechError("Chatter took too long to render") }
            try await Task.sleep(for: .milliseconds(100))
        }
        return try await call("/v1/jobs/\(id)/audio", limit: Self.maxAudio)
    }
}

/// Chatter never redirects; a redirect is refused rather than followed.
final class RefuseRedirects: NSObject, URLSessionTaskDelegate, Sendable {
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest) async -> URLRequest? { nil }
}
