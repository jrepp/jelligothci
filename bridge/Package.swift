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
    ],
    targets: [
        .target(name: "LokiProtocol"),
        .testTarget(name: "LokiProtocolTests", dependencies: ["LokiProtocol"]),
    ]
)
