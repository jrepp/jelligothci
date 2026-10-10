// swift-tools-version: 6.2
import PackageDescription

// Loki Bridge, the Mac side of the voice agent (RFC-003). LokiProtocol is loki/1
// (docs/loki-protocol.md), tested against tests/vectors/loki-1.tsv like the board's C
// codec. Only Apple frameworks: nothing is fetched.
let package = Package(
    name: "LokiBridge",
    platforms: [.macOS(.v26)],
    products: [
        .library(name: "LokiProtocol", targets: ["LokiProtocol"]),
        .library(name: "LokiBridgeCore", targets: ["LokiBridgeCore"]),
        .library(name: "LokiSpeech", targets: ["LokiSpeech"]),
        .executable(name: "loki-bridge", targets: ["loki-bridge"]),
    ],
    targets: [
        .target(name: "LokiProtocol"),
        // The link: TLS identities, pairing, the mutually authenticated listener, and a
        // simulated board.
        .target(name: "LokiBridgeCore", dependencies: ["LokiProtocol"]),
        // Speech: on-device transcription (SpeechAnalyzer) and replies through Chatter.
        .target(name: "LokiSpeech"),
        // The bridge from the terminal, until the menu-bar app: identity, pairing, serve.
        .executableTarget(name: "loki-bridge", dependencies: ["LokiBridgeCore", "LokiProtocol", "LokiSpeech"]),
        .testTarget(name: "LokiProtocolTests", dependencies: ["LokiProtocol"]),
        .testTarget(name: "LokiBridgeCoreTests", dependencies: ["LokiBridgeCore", "LokiProtocol"]),
        .testTarget(name: "LokiSpeechTests", dependencies: ["LokiSpeech"]),
    ]
)
