import Foundation
import Testing
import LokiProtocol
@testable import LokiSessions

// Permission cards: files whose change runs code later are refused without a card,
// however the path is written; every other action is shown whole or refused, never
// shortened; and a card that is shown is one the board can receive.

private let codergotchi = Project(name: "codergotchi", url: URL(fileURLWithPath: "/Users/me/dev/codergotchi"))

private func card(_ tool: String, _ input: [String: JSONValue], project: Project? = codergotchi,
                  blockedPath: String? = nil) -> PermissionCards.Card? {
    PermissionCards.card(tool: tool, input: .object(input), project: project, blockedPath: blockedPath)
}

// MARK: Protected files

/// Claude Code's and git's configuration and hooks, shell start-up files, launch agents,
/// SSH: absolute, relative, under ~, with a trailing slash, or behind "..".
@Test(arguments: [
    "/Users/me/.claude/settings.json", "/Users/me/dev/codergotchi/.claude/settings.local.json", "~/.claude/CLAUDE.md",
    "~/.ssh/authorized_keys", "/Users/me/.ssh/config", "~/Library/LaunchAgents/com.evil.plist",
    "/Library/LaunchDaemons/com.evil.plist", ".git/hooks/pre-commit", "/Users/me/dev/codergotchi/.git/config",
    "sub/.git/info/attributes", ".git/hooks", "~/.zshrc", "/Users/me/.zprofile", ".zshenv", "../.zlogin", "~/.bashrc",
    "~/.bash_profile", "~/.profile", "~/.gitconfig", "/Users/me/dev/codergotchi/.mcp.json", ".mcp.json",
    "~/.zshrc/", "/Users/me/.ssh/", "src/../.claude/agents/x.md", "a/b/../../.zshrc", "//Users//me//.ssh//id_rsa",
    "/Users/me/dev/vendor/.git/modules/x/../../.git/hooks/post-merge",
])
func protectedFilesAreRecognisedHoweverWritten(_ path: String) {
    #expect(PermissionCards.protected(path), "\(path)")
}

/// Ordinary files, and names that only resemble a protected one, still get a card.
@Test(arguments: ["/Users/me/dev/codergotchi/README.md", "/tmp/x.claude", "/Users/me/notes/.claude-notes.md",
                  "/Users/me/dev/p/.github/workflows/ci.yml", "/Users/me/dev/p/.git/objects/ab/cdef",
                  "/Users/me/dev/p/.git/HEAD", "/Users/me/dev/p/hooks/pre-commit", "/Users/me/.zshrc.bak",
                  "/Users/me/dev/p/src/profile", "/Users/me/dev/p/docs/ssh.md", "/Users/me/dev/p/.gitignore", ""])
func lookalikesAreNotProtected(_ path: String) {
    #expect(!PermissionCards.protected(path), "\(path)")
}

/// Every editing tool is refused without a card for a protected file, by file_path or
/// notebook_path, whatever else the input holds.
@Test(arguments: ["Edit", "Write", "MultiEdit", "NotebookEdit"])
func editsToProtectedFilesNeverMakeACard(_ tool: String) {
    for path in ["~/.zshrc", "/Users/me/.claude/settings.json", ".git/hooks/pre-push", "~/.ssh/authorized_keys"] {
        #expect(card(tool, ["file_path": .string(path), "content": .string("x")]) == nil, "\(tool) \(path)")
        #expect(card(tool, ["notebook_path": .string(path)]) == nil, "\(tool) \(path)")
    }
    // An ordinary file outside the project gets a card naming it whole.
    #expect(card(tool, ["file_path": .string("/Users/me/notes/todo.md")])
            == PermissionCards.Card(title: "Change a file outside the project?", detail: "/Users/me/notes/todo.md\nin codergotchi"))
}

/// Path tricks that land on a protected file once the path is resolved: "." and ".."
/// after .git, or a trailing "/." on a start-up file.
@Test(arguments: ["/Users/me/dev/p/.git/./hooks/pre-commit", "/Users/me/dev/p/.git/objects/../hooks/pre-commit",
                  "/Users/me/dev/p/.git/x/../config", "/Users/me/.zshrc/.", "/Users/me/.gitconfig/."])
func dotSegmentsDoNotHideAProtectedFile(_ path: String) {
    let resolved = URL(fileURLWithPath: path).standardizedFileURL.path
    #expect(PermissionCards.protected(resolved), "\(resolved)")  // the place the write lands
    #expect(PermissionCards.protected(path), "\(path)")
}

/// Whether the temp folder's volume folds case, as the macOS default does: a file made as
/// .zshrc opens as .ZSHRC (and, with Unicode case folding, ".\u{17F}sh" opens ".ssh").
private let caseInsensitiveVolume: Bool = {
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("loki-case-\(UUID())")
    try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    try? Data("x".utf8).write(to: folder.appendingPathComponent(".zshrc"))
    return FileManager.default.fileExists(atPath: folder.appendingPathComponent(".ZSHRC").path)
}()

/// On a case-insensitive volume ".SSH" and ".Zshrc" are the same files as ".ssh" and
/// ".zshrc", so they must be refused the same way.
@Test(.enabled(if: caseInsensitiveVolume, "only a case-insensitive volume lets case reach a protected file"),
      arguments: ["/Users/me/.SSH/authorized_keys", "/Users/me/.Claude/settings.json", "/Users/me/Library/launchagents/x.plist",
                  "/Users/me/dev/p/.GIT/hooks/pre-commit", "/Users/me/dev/p/.git/HOOKS/pre-commit", "/Users/me/.ZSHRC",
                  "/Users/me/.GitConfig", "/Users/me/dev/p/.MCP.json", "/Users/me/.\u{17F}sh/authorized_keys"])
func caseDoesNotHideAProtectedFile(_ path: String) {
    #expect(PermissionCards.protected(path), "\(path)")
}

/// NotebookEdit writes its notebook_path: the check and the card must use that field,
/// not a harmless file_path sent beside it.
@Test func aNotebookEditIsJudgedByThePathItWrites() {
    let input: [String: JSONValue] = ["file_path": .string("/Users/me/notes/harmless.ipynb"),
                                      "notebook_path": .string("/Users/me/.claude/settings.json")]
    #expect(card("NotebookEdit", input) == nil)
}

// MARK: What each tool's card shows

@Test func eachToolsCardShowsItsActionAndPlace() {
    #expect(card("Bash", ["command": .string("npm run dev"), "dangerouslyDisableSandbox": .bool(true)])
            == PermissionCards.Card(title: "Run outside the sandbox?", detail: "npm run dev\nin codergotchi"))
    // Only a real true leaves the sandbox title; "true" as text is an ordinary command.
    #expect(card("Bash", ["command": .string("ls"), "dangerouslyDisableSandbox": .string("true")])?.title == "Run this command?")
    #expect(card("Read", ["file_path": .string("/etc/hosts")])?.detail == "/etc/hosts\nin codergotchi")
    // A search shows what it looks for, where, and in which files.
    #expect(card("Grep", ["pattern": .string("TODO"), "path": .string("/Users/me/other"), "glob": .string("*.swift")])
            == PermissionCards.Card(title: "Search outside the project?",
                                    detail: "TODO\nunder /Users/me/other\nfiles *.swift\nin codergotchi"))
    #expect(card("Glob", ["pattern": .string("/Users/me/other/**/*.swift")])?.detail
            == "/Users/me/other/**/*.swift\nin codergotchi")
    #expect(card("WebFetch", ["url": .string("https://example.com/a"), "prompt": .string("summarise")])
            == PermissionCards.Card(title: "Read this web page?", detail: "https://example.com/a\nin codergotchi"))
    // A tool without a known shape shows its input as one line of sorted JSON.
    #expect(card("mcp__x__deploy", ["b": .number(1), "a": .array([.bool(true), .null])])
            == PermissionCards.Card(title: "Allow mcp__x__deploy?", detail: "{\"a\":[true,null],\"b\":1}\nin codergotchi"))
    // No project: the action alone.
    #expect(card("Bash", ["command": .string("ls")], project: nil)?.detail == "ls")
}

/// Nothing to show, or something that cannot be shown whole, is no card: denied.
@Test func anActionThatCannotBeShownMakesNoCard() {
    #expect(card("Bash", [:]) == nil)
    #expect(card("Bash", ["command": .number(1)]) == nil)
    #expect(card("WebFetch", ["url": .string("")]) == nil)
    #expect(card("Read", [:]) == nil)
    #expect(PermissionCards.card(tool: "Bash", input: .string("ls"), project: codergotchi) == nil)
    #expect(card("Bash", ["command": .string(String(repeating: "x", count: 590))]) == nil)  // 590 + "\nin codergotchi" > 600
    #expect(card("Bash", ["command": .string("ls\u{2028}rm -rf ~")]) == nil)
    let tooBig: [String: JSONValue] = ["blob": .string(String(repeating: "y", count: 700))]
    #expect(card("mcp__x__y", tooBig) == nil)
    // A project whose name the card cannot show refuses every card in it.
    let odd = Project(name: "evil\u{202E}name", url: URL(fileURLWithPath: "/tmp/evil"))
    #expect(card("Bash", ["command": .string("ls")], project: odd) == nil)
}

// MARK: ApprovalCard.content

/// Every scalar that hides, reorders or separates text is refused, in the title and in
/// the action: controls but the line feed, format characters, separators but the space.
@Test(arguments: [0x00, 0x07, 0x09, 0x0B, 0x0C, 0x0D, 0x1B, 0x7F, 0x85, 0x9B, 0xA0, 0xAD, 0x061C, 0x180E, 0x200B,
                  0x200C, 0x200D, 0x200E, 0x200F, 0x2028, 0x2029, 0x202A, 0x202E, 0x2060, 0x2066, 0x2069, 0x3000,
                  0xFEFF, 0xFFF9, 0xE0001, 0xE0041] as [UInt32])
func everyHidingScalarIsRefused(_ value: UInt32) throws {
    let scalar = try #require(Unicode.Scalar(value))
    let odd = "rm\(Character(scalar))-rf"
    #expect(ApprovalCard.content(title: "Run?", action: odd, place: "") == nil, "U+\(String(value, radix: 16))")
    #expect(ApprovalCard.content(title: "Run \(odd)?", action: "ls", place: "") == nil, "U+\(String(value, radix: 16))")
    #expect(ApprovalCard.content(title: "Run?", action: "ls", place: "p\(Character(scalar))") == nil)
}

/// Lines and bytes at their limits: eight lines with the place, 120 bytes of title,
/// 600 of detail; one more is refused, never cut.
@Test func cardLimitsAreExactAndNothingIsShortened() {
    let seven = Array(repeating: "ls", count: 7).joined(separator: "\n")
    #expect(ApprovalCard.content(title: "Run?", action: seven, place: "p")?.detail == seven + "\nin p")
    #expect(ApprovalCard.content(title: "Run?", action: seven + "\nls", place: "p") == nil)
    #expect(ApprovalCard.content(title: "Run?", action: seven + "\nls", place: "") != nil)
    #expect(ApprovalCard.content(title: "Run?", action: seven + "\nls\n", place: "") == nil)  // a trailing line counts
    #expect(ApprovalCard.content(title: "Run?", action: "\n\n\n\n\n\n\n\n", place: "") == nil)
    let title = String(repeating: "t", count: 120)
    #expect(ApprovalCard.content(title: title, action: "ls", place: "")?.title == title)
    #expect(ApprovalCard.content(title: title + "t", action: "ls", place: "") == nil)
    // Bytes, not characters: 200 three-byte characters is 600 bytes.
    let wide = String(repeating: "\u{4E2D}", count: 200)
    #expect(ApprovalCard.content(title: "Run?", action: wide, place: "")?.detail == wide)
    #expect(ApprovalCard.content(title: "Run?", action: wide + "x", place: "") == nil)
    #expect(ApprovalCard.content(title: "Run?", action: String(repeating: "x", count: 596), place: "p") == nil)
    #expect(ApprovalCard.content(title: "Run?", action: String(repeating: "x", count: 595), place: "p")?.detail.utf8.count
            == 600)
}

/// Text from many scripts, emoji and combining marks is shown as it is.
@Test func ordinaryTextInAnyScriptIsShown() {
    for action in ["echo \u{00E9}t\u{00E9}", "git commit -m \"\u{4E2D}\u{6587}\"", "say \u{1F984}", "e\u{301}cho",
                   "printf '%s' \"$HOME\"", "echo \u{0639}\u{0631}\u{0628}\u{064A}"] {
        #expect(ApprovalCard.content(title: "Run this command?", action: action, place: "p")?.detail == action + "\nin p",
                "\(action)")
    }
}

private let cardAlphabet: [Character] = Array("abcdefgh XYZ019 -_./~*?\"\\'$;&|<>(){}[]\n")
    + ["\u{00E9}", "\u{4E2D}", "\u{1F984}", "e\u{301}", "\t", "\r", "\u{0000}", "\u{1B}", "\u{7F}", "\u{85}", "\u{A0}",
       "\u{200B}", "\u{200D}", "\u{202E}", "\u{2066}", "\u{2028}", "\u{2029}", "\u{FEFF}", "\u{3000}", "\u{E0041}"]

private let refused: Set<Unicode.Scalar> = ["\t", "\r", "\u{0000}", "\u{1B}", "\u{7F}", "\u{85}", "\u{A0}", "\u{200B}",
                                            "\u{200D}", "\u{202E}", "\u{2066}", "\u{2028}", "\u{2029}", "\u{FEFF}",
                                            "\u{3000}", "\u{E0041}"]

/// Property: for any action, the card is the action whole plus its place, or nothing;
/// it is nothing exactly when the action holds a refused scalar or does not fit; and a
/// card that is shown always encodes as an ask the board accepts.
@Test(arguments: 0..<300)
func aCardIsTheWholeActionOrNothingAndAlwaysEncodes(_ seed: Int) throws {
    var random = Seeded(seed)
    let length = random.chance(10) ? 400 + random.below(400) : random.below(60)
    let action = String((0..<length).map { _ in random.pick(cardAlphabet) })
    let place = random.pick(["", "codergotchi", "chatter-swift-engine", "\u{1F984}"])
    let title = random.pick(["Run this command?", "Run outside the sandbox?", "Read this web page?"])
    let content = ApprovalCard.content(title: title, action: action, place: place)
    let detail = place.isEmpty ? action : "\(action)\nin \(place)"
    let fits = detail.utf8.count <= 600 && detail.split(separator: "\n", omittingEmptySubsequences: false).count <= 8
    let clean = !detail.unicodeScalars.contains { refused.contains($0) }
    #expect((content != nil) == (fits && clean), "seed \(seed): \(action.debugDescription)")
    guard let content else { return }
    #expect(content.title == title && content.detail == detail, "seed \(seed)")
    let ask = Ask(id: "a1", nonce: String(repeating: "0", count: 32), kind: .approve, title: content.title,
                  detail: content.detail, options: ["Allow", "Deny"], timeoutMs: 60_000)
    let bytes = try LokiCodec.encode(.ask(ask), seq: 1)
    #expect(bytes.count <= Loki.maxBridgeMessage, "seed \(seed)")
}

/// The worst case for the wire: 600 bytes of quotes and backslashes, each escaped to
/// two, with the longest title, still fits one message.
@Test func theMostEscapedCardStillFitsOneMessage() throws {
    let action = String(repeating: "\"\\", count: 300)
    let content = try #require(ApprovalCard.content(title: String(repeating: "\"", count: 120), action: action, place: ""))
    let ask = Ask(id: "a99999999", nonce: String(repeating: "f", count: 32), kind: .approve, title: content.title,
                  detail: content.detail, options: ["Allow", "Deny"], timeoutMs: 600_000)
    #expect(try LokiCodec.encode(.ask(ask), seq: UInt32.max).count <= Loki.maxBridgeMessage)
}

// MARK: Answers

/// Property: of any seeded mix of answers (another card's id or nonce, a digest of a
/// different card, a choice not offered, the same answer twice), only the exact id,
/// nonce, digest and an offered choice settle a card, and only once.
@Test(arguments: 0..<24)
func onlyTheExactAnswerSettlesACardAndOnlyOnce(_ seed: Int) async throws {
    var random = Seeded(seed)
    let board = Board(), approvals = ApprovalBroker { board.send($0) }
    let count = 2 + random.below(3)
    let options = (0..<count).map { "option \($0)" }
    let outcome = Task { await approvals.ask(kind: .choose, title: "Which?", options: options) }
    let card = try #require(await board.lastAsk())
    let other = Ask(id: card.id, nonce: card.nonce, kind: .choose, title: "Other?", options: options, timeoutMs: card.timeoutMs)
    for _ in 0..<(5 + random.below(10)) {
        let wrong: (String, String, String, UInt32) = switch random.below(5) {
        case 0: ("a\(9_000 + random.below(1000))", card.nonce, card.digest, 0)
        case 1: (card.id, String(card.nonce.reversed()), card.digest, 0)
        case 2: (card.id, card.nonce, other.digest, 0)
        case 3: (card.id, card.nonce, card.digest, UInt32(count + random.below(100)))
        default: (card.id, card.nonce.uppercased(), card.digest, 0)
        }
        #expect(await !approvals.answer(id: wrong.0, nonce: wrong.1, digest: wrong.2, choice: wrong.3), "seed \(seed)")
    }
    #expect(await approvals.openCount == 1, "seed \(seed)")
    let choice = UInt32(random.below(count))
    #expect(await approvals.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: choice), "seed \(seed)")
    #expect(await !approvals.answer(id: card.id, nonce: card.nonce, digest: card.digest, choice: choice), "seed \(seed)")
    #expect(await outcome.value == .chosen(Int(choice)), "seed \(seed)")
}

/// The board's cancel with nothing running and no one talking stops the audio, closes
/// nothing and leaves the board idle.
@Test func cancelWithNothingRunningOnlyStopsTheAudio() async {
    let board = Board(), sidecar = RecordingSidecar()
    let conductor = Conductor(board: board, sidecar: Sidecar(transport: sidecar), transcriber: FakeTranscriber(),
                              speaker: FakeSpeaker(), resolver: ProjectResolver(roots: [projectRoot([])]))
    await conductor.handle(.cancel)
    #expect(sidecar.commands.isEmpty)
    #expect(board.messages == [.stopAudio, .state(.idle, caption: "", progress: -1)])
}

// MARK: Reads of secrets

/// Keys, credentials and the configuration that runs code are never read by a tap:
/// a read of one, outside the project, is refused without a card, however the name is
/// cased, and through a link that leads to one.
@Test(arguments: [
    "/Users/me/.ssh/id_rsa", "~/.ssh/id_ed25519.pub", "/Users/me/.aws/credentials", "/Users/me/other/.ENV",
    "/Users/me/other/.env.production", "/Users/me/keys/AuthKey_ABC.p8", "/Users/me/.docker/config.json",
    "/Users/me/.claude/settings.json", "/Users/me/other/terraform.tfstate", "/Users/me/.netrc", "~/.npmrc",
])
func readsOfSecretsAreRefusedWithoutACard(_ path: String) {
    for tool in ["Read", "Glob", "Grep"] {
        let key = tool == "Read" ? "file_path" : "path"
        #expect(card(tool, [key: .string(path)]) == nil, "\(tool) \(path)")
    }
    #expect(card("Read", ["file_path": .string("/Users/me/notes.md")], blockedPath: path) == nil, "blocked \(path)")
}

@Test func aReadThroughALinkToASecretIsRefused() throws {
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("loki-read-\(UUID())")
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    let secret = folder.appendingPathComponent("id_rsa")
    try Data("key".utf8).write(to: secret)
    let link = folder.appendingPathComponent("notes.txt")
    try FileManager.default.createSymbolicLink(at: link, withDestinationURL: secret)
    #expect(card("Read", ["file_path": .string(link.path)]) == nil)
}

/// An ordinary file outside the project still gets a card naming it.
@Test func anOrdinaryReadOutsideTheProjectStillGetsACard() {
    #expect(card("Read", ["file_path": .string("/Users/me/notes/todo.md")])
            == PermissionCards.Card(title: "Read outside the project?", detail: "/Users/me/notes/todo.md\nin codergotchi"))
    #expect(!PermissionCards.secret("/Users/me/src/environment.md") && !PermissionCards.secret("/Users/me/src/keys.md"))
}

// MARK: Searches and private places

private let home = FileManager.default.homeDirectoryForCurrentUser.path

/// A search over the home folder or above it, or over a folder that holds
/// credentials, is refused without a card: it would reach the keys inside.
@Test(arguments: ["~", "/", "/Users", home, "\(home)/", "~/.aws", "~/.config", "~/Library", "~/.gnupg", "~/.kube",
                  "~/Library/Application Support/Chatter", "~/dev/.."])
func searchesOverPrivatePlacesAreRefused(_ root: String) {
    #expect(card("Grep", ["pattern": .string("SECRET-CANARY-0002"), "path": .string(root)]) == nil, "Grep \(root)")
    #expect(card("Glob", ["pattern": .string("**/*"), "path": .string(root)]) == nil, "Glob \(root)")
    #expect(card("Glob", ["pattern": .string(root + "/**/*")]) == nil, "Glob pattern \(root)")
}

/// Credentials and the bridge's own secrets are refused by name or by the place they
/// lie, however the path is written.
@Test(arguments: ["~/Library/Application Support/Chatter/api-token", "~/Library/Application Support/Loki Bridge/identity.p12",
                  "~/.config/gh/hosts.yml", "~/.kube/config", "~/.gnupg/private-keys-v1.d/x.key", "~/.cargo/credentials.toml",
                  "~/.config/gcloud/application_default_credentials.json", "~/.claude.json", "~/.CLAUDE.JSON",
                  "\(home)/dev/../.aws/config"])
func credentialStoresAreRefused(_ path: String) {
    #expect(card("Read", ["file_path": .string(path)]) == nil, "\(path)")
}

/// Patterns and filters name what a search reads: a Glob for keys, or a Grep limited
/// to key files, is refused; so is any path field of any tool, and every one of them.
@Test func patternsFiltersAndEveryPathFieldAreChecked() {
    #expect(card("Glob", ["pattern": .string("~/.ssh/*")]) == nil)
    #expect(card("Grep", ["pattern": .string("PRIVATE"), "path": .string("/Users/me/other"), "glob": .string("*.pem")]) == nil)
    #expect(card("Read", ["file_path": .string("/Users/me/notes.md"), "path": .string("~/.ssh/id_rsa")]) == nil)
    #expect(card("NotebookRead", ["notebook_path": .string("~/.aws/credentials")]) == nil)
    #expect(card("LS", ["path": .string("~/.ssh")]) == nil)
    #expect(card("Read", ["file_path": .string("/net/host.example/x")]) == nil)  // never looked up
    #expect(card("Write", ["file_path": .string("/net/host.example/x")]) == nil)
}

/// A relative path is the project's, and a link in the project to a key is followed.
@Test func aRelativeLinkInTheProjectToAKeyIsRefused() throws {
    let folder = FileManager.default.temporaryDirectory.appendingPathComponent("loki-rel-\(UUID())")
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
    try Data("key".utf8).write(to: folder.appendingPathComponent("id_ed25519"))
    try FileManager.default.createSymbolicLink(at: folder.appendingPathComponent("creds"),
                                               withDestinationURL: folder.appendingPathComponent("id_ed25519"))
    let project = Project(name: "p", url: folder)
    #expect(card("Grep", ["pattern": .string("x"), "path": .string("creds")], project: project) == nil)
    #expect(card("Read", ["file_path": .string("creds")], project: nil) == nil)  // relative with no project
}

/// Ordinary reads and searches outside the project still get a card.
@Test func ordinarySearchesStillGetACard() {
    #expect(card("Grep", ["pattern": .string("TODO"), "path": .string("\(home)/dev/other")]) != nil)
    #expect(card("Glob", ["pattern": .string("\(home)/Documents/**/*.md")]) != nil)
    #expect(card("Read", ["file_path": .string("/etc/hosts")]) != nil)
}
