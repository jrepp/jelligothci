import Foundation
import Testing
@testable import LokiSpeech

// Boundaries of Chatter's client (memo-027): the token file, job ids, reply sizes,
// redirects, and the text it lets through to a terminal. These extend the serialized
// ChatterTests suite, so they never race its shared fake.

/// Answers with any status, headers and body; can redirect instead.
final class ScriptedChatter: URLProtocol, @unchecked Sendable {
    enum Answer { case reply(Int, [String: String], Data), redirect(to: URL) }
    nonisolated(unsafe) static var answer: (@Sendable (URLRequest) -> Answer)?
    nonisolated(unsafe) static var seen: [URL] = []
    static let lock = NSLock()

    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func stopLoading() {}

    override func startLoading() {
        let answer = Self.lock.withLock {
            Self.seen.append(request.url!)
            return Self.answer?(request) ?? .reply(500, [:], Data())
        }
        switch answer {
        case .reply(let status, let headers, let data):
            let response = HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: headers)!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: data)
            client?.urlProtocolDidFinishLoading(self)
        case .redirect(let target):
            let response = HTTPURLResponse(url: request.url!, statusCode: 302, httpVersion: "HTTP/1.1",
                                           headerFields: ["Location": target.absoluteString])!
            client?.urlProtocol(self, wasRedirectedTo: URLRequest(url: target), redirectResponse: response)
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocol(self, didLoad: Data())
            client?.urlProtocolDidFinishLoading(self)
        }
    }

    /// A client on this fake, with the production redirect policy, and a valid token.
    static func client(timeout: Duration = .seconds(2)) -> ChatterClient {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [ScriptedChatter.self]
        configuration.timeoutIntervalForRequest = 3
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("chatter-token-\(UUID())")
        FileManager.default.createFile(atPath: file.path, contents: Data("t0ken".utf8), attributes: [.posixPermissions: 0o600])
        lock.withLock { seen = [] }
        return ChatterClient(tokenFile: file, session: URLSession(configuration: configuration, delegate: RefuseRedirects(),
                                                                  delegateQueue: nil), timeout: timeout)
    }
}

private func json(_ object: Any) -> Data { try! JSONSerialization.data(withJSONObject: object) }

/// A voices reply padded to exactly `size` bytes.
private func voicesReply(size: Int) -> Data {
    let bare = json(["voices": [[String: Any]](), "pad": ""])
    return json(["voices": [[String: Any]](), "pad": String(repeating: "p", count: size - bare.count)])
}

extension ChatterTests {
    @Test func jobIDsAreOneToSixtyFourLettersDigitsOrDashes() async throws {
        let cases: [(Any, Bool)] = [
            (String(repeating: "a", count: 64), true), ("A-9", true), ("-", true),
            (String(repeating: "a", count: 65), false), ("é", false), ("a b", false), ("a/b", false),
            ("a?b", false), ("a%2F", false), ("a.b", false), ("a\u{0}", false), (42, false),
        ]
        for (id, ok) in cases {
            let job = json(["jobID": id])  // the older key is honoured too
            FakeChatter.handler = { request in
                switch request.url!.path {
                case "/v1/speech": (200, job)
                case let path where path.hasSuffix("/audio"): (200, Data("RIFF".utf8))
                default: (200, json(["state": "completed"]))
                }
            }
            do {
                let audio = try await FakeChatter.client().render("hi", voice: "Scar")
                #expect(ok && audio == Data("RIFF".utf8), "\(id) was accepted")
            } catch {
                #expect(!ok && "\(error)".contains("job id"), "\(id): \(error)")
                // A refused id is never put into a path.
                #expect(FakeChatter.lock.withLock { FakeChatter.seen.count } == 1)
            }
        }
    }

    @Test func tokenFilesMustBeSmallPrivateRegularAndPrintable() async throws {
        FakeChatter.handler = { _ in (200, json(["voices": []])) }
        let client = FakeChatter.client()
        let file = client.tokenFile
        func accepted(_ contents: Data, mode: Int = 0o600) async -> Bool {
            try? FileManager.default.removeItem(at: file)
            FileManager.default.createFile(atPath: file.path, contents: contents, attributes: [.posixPermissions: mode])
            return (try? await client.voices()) != nil
        }
        #expect(await accepted(Data(String(repeating: "k", count: 4096).utf8)))
        #expect(await !accepted(Data(String(repeating: "k", count: 4097).utf8)))
        #expect(await accepted(Data("token\n".utf8)))
        #expect(await accepted(Data("token".utf8), mode: 0o400))
        for mode in [0o640, 0o604, 0o602, 0o620] { #expect(await !accepted(Data("token".utf8), mode: mode), "mode \(mode)") }
        for bad in ["to ken", "tök", "to\u{7F}ken", "to\tken", "\u{1B}[0m"] { #expect(await !accepted(Data(bad.utf8)), "\(bad)") }
        #expect(await !accepted(Data([0xFF, 0xFE])))  // not UTF-8
        try FileManager.default.removeItem(at: file)
        try FileManager.default.createDirectory(at: file, withIntermediateDirectories: false)
        await #expect(throws: SpeechError.self) { try await client.voices() }
        try FileManager.default.removeItem(at: file)
        let accepts = FakeChatter.lock.withLock { FakeChatter.seen.count }
        #expect(accepts == 3)  // only the three good tokens reached the network
    }

    @Test func aTokenRemovedMidRenderingStopsTheNextRequest() async {
        let client = FakeChatter.client()
        let file = client.tokenFile
        FakeChatter.handler = { request in
            if request.url!.path == "/v1/speech" {
                try? FileManager.default.removeItem(at: file)
                return (200, json(["id": "j"]))
            }
            return (200, json(["state": "completed"]))
        }
        await #expect(throws: SpeechError.self) { try await client.render("hi", voice: "Scar") }
        #expect(FakeChatter.lock.withLock { FakeChatter.seen.map { $0.url!.path } } == ["/v1/speech"])
    }

    @Test func repliesUpToTheLimitPassAndADeclaredOrStreamedExcessIsRefused() async throws {
        ScriptedChatter.answer = { _ in .reply(200, [:], voicesReply(size: ChatterClient.maxJSON)) }
        #expect(try await ScriptedChatter.client().voices().isEmpty)
        ScriptedChatter.answer = { _ in .reply(200, [:], voicesReply(size: ChatterClient.maxJSON + 1)) }
        await #expect(throws: SpeechError.self) { try await ScriptedChatter.client().voices() }
        // A declared length past the limit is refused before the body is read.
        ScriptedChatter.answer = { _ in .reply(200, ["Content-Length": "\(ChatterClient.maxJSON + 1)"], json(["voices": []])) }
        do {
            _ = try await ScriptedChatter.client().voices()
            Issue.record("a reply declared too large was accepted")
        } catch {
            #expect("\(error)".contains("too large"), "\(error)")
        }
    }

    @Test func aRedirectIsRefusedAndNeverFollowed() async {
        let elsewhere = URL(string: "http://198.51.100.7:18423/v1/voices")!
        ScriptedChatter.answer = { request in
            request.url!.host == "127.0.0.1" ? .redirect(to: elsewhere) : .reply(200, [:], json(["voices": []]))
        }
        await #expect(throws: SpeechError.self) { try await ScriptedChatter.client().voices() }
        let hosts = ScriptedChatter.lock.withLock { ScriptedChatter.seen.map(\.host) }
        #expect(hosts.allSatisfy { $0 == "127.0.0.1" }, "\(hosts)")
    }

    @Test func thePrivateSessionKeepsNothingAndUsesNoProxy() {
        let configuration = ChatterClient.privateSession.configuration
        #expect(configuration.urlCache == nil && configuration.httpCookieStorage == nil)
        #expect((configuration.connectionProxyDictionary ?? [:]).isEmpty)
        #expect(ChatterClient.privateSession.delegate is RefuseRedirects)
    }

    /// Seeded fuzz: printable never lets a C0, C1 or DEL scalar through, never exceeds
    /// 200 bytes, keeps a scalar-for-scalar prefix of its input, and is idempotent.
    @Test func printableTextIsBoundedAndControlFreeForAnyInput() {
        let pool: [UInt32] = [0x00, 0x07, 0x09, 0x0A, 0x0D, 0x1B, 0x1F, 0x20, 0x41, 0x7E, 0x7F, 0x80, 0x85, 0x9B, 0x9F,
                              0xA0, 0xE9, 0x301, 0x200B, 0x202E, 0x2028, 0xFEFF, 0x1F600, 0x10FFFF]
        var state: UInt64 = 0x9E37_79B9_7F4A_7C15
        func next(_ bound: Int) -> Int {
            state = state &* 6_364_136_223_846_793_005 &+ 1_442_695_040_888_963_407
            return Int((state >> 33) % UInt64(bound))
        }
        for seed in 0..<2_000 {
            let count = next(4) == 0 ? 150 + next(200) : next(60)
            var input = String.UnicodeScalarView()
            for _ in 0..<count { input.append(Unicode.Scalar(pool[next(pool.count)])!) }
            let text = String(input), out = ChatterClient.printable(text)
            #expect(out.utf8.count <= 200, "seed \(seed)")
            // Control characters and the invisible ones that reorder or hide text
            // (U+200B, U+202E, U+2028, U+FEFF in the pool) all become "?".
            func hidden(_ s: Unicode.Scalar) -> Bool {
                s.value < 0x20 || (0x7F...0x9F).contains(s.value)
                    || [.format, .lineSeparator, .paragraphSeparator].contains(s.properties.generalCategory)
            }
            #expect(!out.unicodeScalars.contains(where: hidden), "seed \(seed)")
            let mapped = text.unicodeScalars.map { hidden($0) ? "?" : $0 }
            #expect(Array(out.unicodeScalars) == Array(mapped.prefix(out.unicodeScalars.count)), "seed \(seed)")
            #expect(ChatterClient.printable(out) == out, "seed \(seed)")
            if text.utf8.count <= 200 { #expect(out.unicodeScalars.count == text.unicodeScalars.count, "seed \(seed)") }
        }
    }

    /// Named cases the fuzz pool does not draw: every format character (Cf) that hides or
    /// reorders text, and both separators, become "?"; visible text, including spaces,
    /// combining marks and emoji, passes unchanged.
    @Test func printableReplacesEveryInvisibleFormatCharacterAndKeepsVisibleText() {
        let hidden: [UInt32] = [0x00AD, 0x061C, 0x180E, 0x200B, 0x200C, 0x200D, 0x200E, 0x200F,
                                0x202A, 0x202B, 0x202C, 0x202D, 0x202E, 0x2028, 0x2029, 0x2060,
                                0x2066, 0x2067, 0x2068, 0x2069, 0xFEFF, 0xFFF9, 0xFFFA, 0xFFFB,
                                0x110BD, 0x1D173, 0xE0001, 0xE0041, 0xE007F]
        for value in hidden {
            let scalar = Unicode.Scalar(value)!
            #expect(ChatterClient.printable("a\(Character(scalar))b") == "a?b",
                    "U+\(String(value, radix: 16, uppercase: true))")
        }
        for kept in ["caf\u{E9}", "e\u{301}", "\u{A0}x", "\u{3000}", "\u{1F600}", "\u{1F1FA}\u{1F1F8}", "\u{05D0}\u{05D1}"] {
            #expect(ChatterClient.printable(kept) == kept, "\(kept.unicodeScalars.map { $0.value })")
        }
        // A family emoji's zero-width joiners are format characters: its people stay, apart.
        #expect(ChatterClient.printable("\u{1F468}\u{200D}\u{1F469}") == "\u{1F468}?\u{1F469}")
        #expect(ChatterClient.printable("") == "")
    }

    /// The 200-byte cap never splits a scalar: a character that would cross it is dropped
    /// whole, whatever its UTF-8 length.
    @Test func printableStopsBeforeACharacterThatWouldCrossTwoHundredBytes() {
        for (tail, bytes) in [("\u{E9}", 2), ("\u{20AC}", 3), ("\u{1F600}", 4)] {
            for room in 0..<bytes {
                let head = String(repeating: "a", count: 200 - room)
                #expect(ChatterClient.printable(head + tail) == head, "\(bytes)-byte, \(room) left")
            }
            let fits = String(repeating: "a", count: 200 - bytes) + tail
            #expect(ChatterClient.printable(fits + "a") == fits, "\(bytes)-byte fits exactly")
            #expect(ChatterClient.printable(fits).utf8.count == 200)
        }
        // Control characters count as the one byte of their "?".
        #expect(ChatterClient.printable(String(repeating: "\u{85}", count: 300)) == String(repeating: "?", count: 200))
    }
}
