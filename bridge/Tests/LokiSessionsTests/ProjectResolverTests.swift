import Foundation
import Testing
@testable import LokiSessions

/// A project root with the given folders (and files), under the temp folder.
private func root(_ folders: [String], files: [String] = []) throws -> URL {
    let url = FileManager.default.temporaryDirectory.appendingPathComponent("loki-projects-\(UUID())")
    for folder in folders { try FileManager.default.createDirectory(at: url.appendingPathComponent(folder), withIntermediateDirectories: true) }
    for file in files { try Data().write(to: url.appendingPathComponent(file)) }
    return url
}

private func names(_ resolution: ProjectResolver.Resolution) -> [String] {
    switch resolution {
    case .found(let project): [project.name]
    case .ambiguous(let projects): projects.map(\.name)
    case .none: []
    }
}

@Test func spokenNamesFindTheirProject() throws {
    let resolver = ProjectResolver(roots: [try root(["codergotchi", "chatter", "chatter-swift-engine", "Atrium", "jev"])])
    #expect(names(resolver.resolve("codergotchi")) == ["codergotchi"])
    #expect(names(resolver.resolve("Coder Gotchi")) == ["codergotchi"])
    #expect(names(resolver.resolve("coder-gotchi")) == ["codergotchi"])
    #expect(names(resolver.resolve("atrium")) == ["Atrium"])
    #expect(names(resolver.resolve("chatter")) == ["chatter"])            // exact beats the longer near miss
    #expect(names(resolver.resolve("codergotchy")) == ["codergotchi"])    // one letter off
    #expect(names(resolver.resolve("unknown thing")) == [])
    #expect(names(resolver.resolve("")) == [])
    #expect(names(resolver.resolve("jev")) == ["jev"])
}

@Test func nearMissesThatFitTwoProjectsAreAmbiguous() throws {
    let resolver = ProjectResolver(roots: [try root(["elysium-main", "elysium-lan", "elysium-arm"])])
    #expect(names(resolver.resolve("elysium")).count == 3)
    #expect(names(resolver.resolve("elysium main")) == ["elysium-main"])
}

@Test func utterancesNameProjectsOnlyAfterAPointer() throws {
    let resolver = ProjectResolver(roots: [try root(["codergotchi", "chatter", "context", "local", "models"])])
    #expect(names(resolver.mentioned(in: "Run the tests in codergotchi.")) == ["codergotchi"])
    #expect(names(resolver.mentioned(in: "in the chatter project, what failed?")) == ["chatter"])
    #expect(names(resolver.mentioned(in: "switch to coder gotchi")) == ["codergotchi"])
    #expect(names(resolver.mentioned(in: "add context to the readme")) == [])   // "context" is just a word here
    #expect(names(resolver.mentioned(in: "run it locally")) == [])
    #expect(names(resolver.mentioned(in: "what models do we use")) == [])
    #expect(names(resolver.mentioned(in: "compare chatter with codergotchi")) == [])
    #expect(names(resolver.mentioned(in: "in chatter or in codergotchi")).sorted() == ["chatter", "codergotchi"])
}

@Test func onlyRealProjectDirectoriesInsideTheRootCount() throws {
    let base = try root(["inside", ".hidden"], files: ["notes.txt", "git-token.txt"])
    let outside = try root(["elsewhere"])
    try FileManager.default.createSymbolicLink(at: base.appendingPathComponent("escape"),
                                               withDestinationURL: outside.appendingPathComponent("elsewhere"))
    try FileManager.default.createSymbolicLink(at: base.appendingPathComponent("alias"),
                                               withDestinationURL: base.appendingPathComponent("inside"))
    try FileManager.default.createSymbolicLink(at: base.appendingPathComponent("home"),
                                               withDestinationURL: FileManager.default.homeDirectoryForCurrentUser)
    let resolver = ProjectResolver(roots: [base])
    #expect(resolver.projects().map(\.name) == ["inside"])  // no files, hidden folders, escapes or duplicates
    #expect(names(resolver.resolve("escape")) == [])
    #expect(names(resolver.resolve("git token")) == [])
    #expect(names(resolver.resolve("home")) == [])
}

@Test func editDistanceIsTheUsualOne() {
    #expect(ProjectResolver.distance(Array("kitten"), Array("sitting")) == 3)
    #expect(ProjectResolver.distance(Array(""), Array("abc")) == 3)
    #expect(ProjectResolver.close("codergotchi", "codergotchy"))
    #expect(!ProjectResolver.close("jev", "jet"))  // too short to guess
}
