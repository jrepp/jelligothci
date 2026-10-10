import Foundation
import Testing
@testable import LokiSessions

// The sidecar's wire: lines from a real child process (a shell script standing in for
// node, never Claude), split however the pipe delivers them; any line parsed without
// trapping, the same way twice; commands that are JSON the sidecar accepts; and failed
// runs said in a few plain words.

/// A shell script in the temp folder, run as the sidecar would be.
private func script(_ body: String) throws -> URL {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("loki-sidecar-\(UUID()).sh")
    try Data(("set -e\n" + body + "\n").utf8).write(to: url)
    return url
}

private func run(_ body: String, environment: [String: String] = [:]) throws -> (SidecarProcess, Sidecar) {
    let process = try SidecarProcess(node: URL(fileURLWithPath: "/bin/sh"), script: try script(body), environment: environment)
    return (process, Sidecar(transport: process))
}

/// Every event until the sidecar exits, or nil after `seconds`.
private func collect(_ sidecar: Sidecar, seconds: Double = 20) async -> [SidecarEvent]? {
    await withTaskGroup(of: [SidecarEvent]?.self) { group in
        group.addTask {
            var events: [SidecarEvent] = []
            for await event in sidecar.events { events.append(event) }
            return events
        }
        group.addTask {
            try? await Task.sleep(for: .seconds(seconds))
            return nil
        }
        let first = await group.next() ?? nil
        group.cancelAll()
        return first
    }
}

// MARK: SidecarProcess

/// Every event until `last` arrives, then stops the process and adds its exit; nil if
/// `last` does not come within `seconds`. The script stays alive until stopped, so this
/// sees framing alone, without the race at exit.
private func collect(_ sidecar: Sidecar, _ process: SidecarProcess, until last: SidecarEvent,
                     seconds: Double = 20) async -> [SidecarEvent]? {
    await withTaskGroup(of: [SidecarEvent]?.self) { group in
        group.addTask {
            var events: [SidecarEvent] = []
            for await event in sidecar.events {
                events.append(event)
                if event == last { process.stop() }
            }
            return events.contains(last) ? events : nil
        }
        group.addTask {
            try? await Task.sleep(for: .seconds(seconds))
            process.stop()
            return nil
        }
        let first = await group.next() ?? nil
        group.cancelAll()
        return first
    }
}

private let end = SidecarEvent.closed(session: "end")

/// The exit of a script stopped while waiting on its input: by the signal, or by the
/// end of input that stop() also gives it, whichever lands first.
private func stopped(_ event: SidecarEvent?) -> Bool { event == .exited(status: SIGTERM) || event == .exited(status: 0) }
private let waitForStop = "printf '{\"type\":\"closed\",\"session\":\"end\"}\\n'\nexec cat > /dev/null"

/// Long lines arrive split across many reads; each is put back together, in order, and
/// blank lines are skipped.
@Test func longLinesSplitAcrossReadsArriveWholeAndInOrder() async throws {
    let (process, sidecar) = try run("""
        filler=$(head -c 20000 /dev/zero | tr '\\0' x)
        i=0
        while [ $i -lt 60 ]; do
          printf '{"type":"speak","session":"s1","text":"%s-%d"}\\n\\n' "$filler" $i
          i=$((i + 1))
        done
        """ + "\n" + waitForStop)
    let events = try #require(await collect(sidecar, process, until: end))
    let filler = String(repeating: "x", count: 20_000)
    #expect(Array(events.prefix(60)) == (0..<60).map { .speak(session: "s1", text: "\(filler)-\($0)", mood: "neutral") })
    #expect(Array(events.dropFirst(60).prefix(1)) == [end] && events.count == 62 && stopped(events.last))
}

/// The exit status follows the last event.
@Test func theExitStatusFollowsTheLastEvent() async throws {
    let (_, sidecar) = try run("exit 3")
    #expect(try #require(await collect(sidecar)) == [.exited(status: 3)])
}

/// Many short lines written just before the process exits are all delivered: none is
/// lost when the exit is seen before the last of the output is read.
@Test(arguments: 0..<5)
func linesWrittenJustBeforeExitAreNotLost(_ round: Int) async throws {
    let (_, sidecar) = try run("""
        i=0
        while [ $i -lt 2000 ]; do printf '{"type":"closed","session":"s%d"}\\n' $i; i=$((i + 1)); done
        """)
    let events = try #require(await collect(sidecar))
    let closed = events.filter { if case .closed = $0 { true } else { false } }
    #expect(closed.count == 2000, "round \(round): \(closed.count) of 2000")
    #expect(events.last == .exited(status: 0))
}

/// A runaway line (over 4 MB without a line feed) is dropped, never parsed, even when
/// it is valid JSON, and the lines after it still arrive.
@Test func aRunawayLineIsDroppedAndTheNextLinesStillArrive() async throws {
    let (process, sidecar) = try run("""
        printf '{"type":"speak","session":"s1","text":"'
        head -c 5000000 /dev/zero | tr '\\0' x
        printf '"}\\n{"type":"ready","sdk":"x"}\\n'
        """ + "\n" + waitForStop)
    let events = try #require(await collect(sidecar, process, until: end))
    #expect(Array(events.prefix(2)) == [.ready, end] && events.count == 3 && stopped(events.last))
}

/// Commands go to the process's standard input as one JSON line each, and the process
/// sees only the environment it was given.
@Test func commandsReachStandardInputAsLinesAndOnlyTheGivenEnvironment() async throws {
    let out = FileManager.default.temporaryDirectory.appendingPathComponent("loki-sidecar-out-\(UUID())")
    let (process, sidecar) = try run("""
        read -r first
        read -r second
        printf '%s\\n%s\\n' "$first" "$second" > "$OUT"
        env >> "$OUT"
        """ + "\n" + waitForStop, environment: ["OUT": out.path, "LOKI_PROBE": "1"])
    sidecar.send(.prompt(session: "s1", text: "line one\nline \"two\" \u{1F984}"))
    sidecar.send(.decision(session: "s1", request: "r1", allow: false, message: nil, answers: ["Q?": "A"]))
    let events = try #require(await collect(sidecar, process, until: end))
    #expect(events.first == end && events.count == 2 && stopped(events.last))
    let lines = try String(contentsOf: out, encoding: .utf8).split(separator: "\n").map(String.init)
    let prompt = try #require(try JSONSerialization.jsonObject(with: Data(lines[0].utf8)) as? [String: Any])
    #expect(prompt["type"] as? String == "prompt" && prompt["text"] as? String == "line one\nline \"two\" \u{1F984}")
    let decision = try #require(try JSONSerialization.jsonObject(with: Data(lines[1].utf8)) as? [String: Any])
    #expect(decision["allow"] as? Bool == false && decision["message"] == nil
            && decision["answers"] as? [String: String] == ["Q?": "A"])
    let variables = Set(lines.dropFirst(2).compactMap { $0.split(separator: "=").first.map(String.init) })
    #expect(variables.contains("LOKI_PROBE") && !variables.contains("HOME") && !variables.contains("GITHUB_TOKEN"))
}

/// Stopping the process ends the events with its exit.
@Test func stoppingTheProcessEndsTheEvents() async throws {
    let (process, sidecar) = try run("exec sleep 30")
    #expect(await eventually { process.status == nil })
    process.stop()
    let events = try #require(await collect(sidecar, seconds: 10))
    #expect(events == [.exited(status: SIGTERM)])
}

// MARK: Parsing

private let types = ["ready", "state", "tool", "permission", "question", "speak", "progress", "result", "error", "closed",
                     "start", "dance", ""]

/// A JSON value of any kind, for a field that may be missing or of the wrong type.
private func anyValue(_ random: inout Seeded, depth: Int = 0) -> Any? {
    switch random.below(depth > 2 ? 6 : 9) {
    case 0: return nil
    case 1: return random.pick(["s1", "", "r1", "x y", "\u{1F984}", String(repeating: "z", count: 300)])
    case 2: return random.pick([0, 1, -1, 2.5, 1e300, -1e300, Double(Int.max)] as [Double])
    case 3: return random.chance(50)
    case 4: return NSNull()
    case 5: return random.pick(["neutral", "happy", "running"])
    case 6: return (0..<random.below(4)).compactMap { _ in anyValue(&random, depth: depth + 1) }
    case 7: return ["question": anyValue(&random, depth: depth + 1) ?? "Q?",
                    "options": [["label": "a"], ["label": random.chance(50) ? "b" : 7], [:]]] as [String: Any]
    default:
        var object: [String: Any] = [:]
        for key in ["command", "label", "x"] where random.chance(50) { object[key] = anyValue(&random, depth: depth + 1) }
        return object
    }
}

/// Property: any line, JSON or not, parses without trapping, the same way twice; a
/// parsed event for a session carries that session as sent.
@Test(arguments: 0..<300)
func anyLineParsesWithoutTrappingAndTheSameWayTwice(_ seed: Int) throws {
    var random = Seeded(seed)
    let line: Data
    if random.chance(15) {
        line = Data((0..<random.below(40)).map { _ in UInt8(random.below(256)) })
    } else {
        var object: [String: Any] = ["type": random.pick(types)]
        for key in ["session", "state", "tool", "summary", "request", "input", "reason", "blockedPath", "questions", "text",
                    "mood", "message", "fraction", "ok", "turns", "cost"] where random.chance(60) {
            object[key] = anyValue(&random)
        }
        line = try JSONSerialization.data(withJSONObject: object)
    }
    let event = SidecarParse.event(line)
    #expect(event == SidecarParse.event(line), "seed \(seed)")
    let sent = (try? JSONSerialization.jsonObject(with: line) as? [String: Any])?["session"] as? String
    switch event {
    case .state(let s, _), .tool(let s, _, _), .permission(let s, _, _, _, _, _), .question(let s, _, _),
         .speak(let s, _, _), .progress(let s, _, _), .result(let s, _, _, _, _), .closed(let s):
        #expect(s == sent, "seed \(seed)")
    case .error(let s, _): #expect(s == sent, "seed \(seed)")
    case .ready, .exited, nil: break
    }
}

/// Fields of the wrong type fall back rather than being taken as something else: a
/// result's ok must be true, a progress fraction a number.
@Test func fieldsOfTheWrongTypeFallBack() {
    func parse(_ text: String) -> SidecarEvent? { SidecarParse.event(Data(text.utf8)) }
    #expect(parse(#"{"type":"result","session":"s1","ok":"true","text":5}"#)
            == .result(session: "s1", ok: false, text: "", turns: 0, cost: 0))
    #expect(parse(#"{"type":"progress","session":"s1","message":"m","fraction":"0.5"}"#)
            == .progress(session: "s1", message: "m", fraction: nil))
    #expect(parse(#"{"type":"progress","session":"s1","message":"m","fraction":-1e999}"#)
            == .progress(session: "s1", message: "m", fraction: -.infinity))
    #expect(parse(#"{"type":"permission","session":"s1","request":"r1"}"#) == nil)  // no tool
    #expect(parse(#"{"type":"permission","session":"s1","request":"r1","tool":"Bash"}"#)
            == .permission(session: "s1", request: "r1", tool: "Bash", input: .null, reason: nil, blockedPath: nil))
    #expect(parse(#"{"type":"question","session":"s1","request":"r1","questions":[{"question":"Q","options":[{"label":"a"},{"x":1},{"label":2}]},{"options":[]}]}"#)
            == .question(session: "s1", request: "r1",
                         questions: [SidecarQuestion(question: "Q", options: ["a"], multiSelect: false)]))
    #expect(parse(#"{"type":"speak","session":7,"text":"hi"}"#) == nil)
    #expect(parse(#"{"type":"closed"}"#) == nil && parse("[]") == nil && parse("") == nil)
    // Booleans and numbers stay apart in tool inputs.
    #expect(parse(#"{"type":"permission","session":"s","request":"r","tool":"T","input":{"a":true,"b":1,"c":0}}"#)
            == .permission(session: "s", request: "r", tool: "T",
                           input: .object(["a": .bool(true), "b": .number(1), "c": .number(0)]), reason: nil, blockedPath: nil))
}

/// A progress fraction of minus infinity, which the parser lets through, is clamped
/// rather than trapping the conductor.
@Test func anInfiniteProgressFractionIsClamped() async {
    let board = Board(), sidecar = RecordingSidecar()
    let conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar), transcriber: FakeTranscriber(),
                              speaker: FakeSpeaker(), resolver: ProjectResolver(roots: [projectRoot([])]))
    await conductor.work("x", in: .general)
    await conductor.handle(.progress(session: "s1", message: "m", fraction: -.infinity))
    await conductor.handle(.progress(session: "s1", message: "m", fraction: .infinity))
    let progress = board.messages.compactMap { if case .state(.working, _, let p, _) = $0 { p } else { nil } }
    #expect(progress == [0, 1000])
}

// MARK: JSON values

private func randomValue(_ random: inout Seeded, depth: Int = 0) -> JSONValue {
    switch random.below(depth > 3 ? 4 : 6) {
    case 0: .string(random.pick(["", "a", "\u{1F984}", "line\nbreak", "q\"uote", "\\"]))
    case 1: .number(Double(random.below(2_000_000)) - 1_000_000)
    case 2: .bool(random.chance(50))
    case 3: .null
    case 4: .array((0..<random.below(4)).map { _ in randomValue(&random, depth: depth + 1) })
    default:
        .object(Dictionary(uniqueKeysWithValues: (0..<random.below(4)).map { ("k\($0)", randomValue(&random, depth: depth + 1)) }))
    }
}

/// Metamorphic: any JSON value survives the trip to Foundation, to JSON text and back.
@Test(arguments: 0..<200)
func jsonValuesRoundTripThroughJSONText(_ seed: Int) throws {
    var random = Seeded(seed)
    let value = JSONValue.object(["v": randomValue(&random)])
    let data = try JSONSerialization.data(withJSONObject: value.foundation)
    let back = try #require(JSONValue.from(try JSONSerialization.jsonObject(with: data)))
    #expect(back == value, "seed \(seed)")
}

@Test func onlyJSONCompatibleValuesConvert() {
    #expect(JSONValue.from(["a": Date()]) == nil)
    #expect(JSONValue.from(["a": Double.nan]) == nil)
    #expect(JSONValue.from(["a": [1, "x", true, NSNull()]] as [String: Any])
            == .object(["a": .array([.number(1), .string("x"), .bool(true), .null])]))
    #expect(JSONValue.from("plain") == .string("plain"))
}

// MARK: Failures said aloud

/// Property: a failed run is said as "That didn't work" and at most the first plain
/// words of its first line: never JSON, never a second line, at most 160 bytes.
@Test(arguments: 0..<300)
func failuresAreAFewPlainWordsWhateverTheError(_ seed: Int) {
    var random = Seeded(seed)
    let pieces = ["API Error: 529", " ", "{\"type\":\"error\"}", "[1,2]", "\n", "\r\n", "\u{2028}", "·", "overloaded", ":",
                  ".", "-", "\u{1F984}", "Request timed out", String(repeating: "word ", count: 40), "\t", "Failed!"]
    let text = (0..<random.below(12)).map { _ in random.pick(pieces) }.joined()
    let said = JobBook.failure(text)
    #expect(said.hasPrefix("That didn't work"), "seed \(seed)")
    #expect(said.utf8.count <= 160, "seed \(seed): \(said.utf8.count)")
    #expect(!said.contains("{") && !said.contains("[") && !said.contains { $0.isNewline }, "seed \(seed): \(said)")
    #expect(said.hasSuffix(".") || said.hasSuffix("..."), "seed \(seed): \(said)")
    #expect(said == JobBook.failure(text), "seed \(seed)")
}

@Test func failuresKeepTheirFirstPlainWords() {
    #expect(JobBook.failure("API Error: 529 {\"type\":\"overloaded_error\"}") == "That didn't work: API Error: 529.")
    #expect(JobBook.failure("\n\nRequest timed out.\nstack") == "That didn't work: Request timed out.")
    #expect(JobBook.failure("Error · details") == "That didn't work: Error.")
    #expect(JobBook.failure("{\"error\":1}") == "That didn't work.")
    #expect(JobBook.failure(" :.- ") == "That didn't work.")
    #expect(JobBook.failure("") == "That didn't work.")
    let long = JobBook.failure(String(repeating: "overloaded ", count: 30))
    #expect(long.hasSuffix("...") && long.utf8.count <= 160)
}
