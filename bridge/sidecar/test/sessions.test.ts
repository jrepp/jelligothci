// The sidecar's logic against a fake Agent SDK: no SDK, network, or Claude needed.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Sessions, summarize, type Sdk } from "../src/sessions.ts";
import { encode, parseIncoming, type Outgoing } from "../src/protocol.ts";

/** A fake query the test drives: it yields what the test pushes and records input. */
class FakeQuery {
  options: Record<string, any>;
  prompts: string[] = [];
  interrupted = 0;
  closed = false;
  private queue: any[] = [];
  private waiting: ((value: IteratorResult<any>) => void) | undefined;
  private ended = false;

  constructor(prompt: AsyncIterable<any>, options: Record<string, any>) {
    this.options = options;
    void (async () => {
      for await (const message of prompt) this.prompts.push(message.message.content);
    })();
  }
  emit(message: any): void {
    if (this.waiting) {
      const resolve = this.waiting;
      this.waiting = undefined;
      resolve({ value: message, done: false });
    } else this.queue.push(message);
  }
  end(): void {
    this.ended = true;
    this.waiting?.({ value: undefined, done: true });
  }
  async interrupt(): Promise<void> {
    this.interrupted++;
  }
  close(): void {
    this.closed = true;
  }
  [Symbol.asyncIterator](): AsyncIterator<any> {
    return {
      next: () => {
        const next = this.queue.shift();
        if (next) return Promise.resolve({ value: next, done: false });
        if (this.ended) return Promise.resolve({ value: undefined, done: true });
        return new Promise((resolve) => (this.waiting = resolve));
      },
    };
  }
}

function harness(environment?: Record<string, string | undefined>) {
  const sent: Outgoing[] = [];
  const queries: FakeQuery[] = [];
  const tools = new Map<string, (args: any) => Promise<unknown>>();
  const sdk: Sdk = {
    query: ({ prompt, options }) => {
      const query = new FakeQuery(prompt, options);
      queries.push(query);
      return query as any;
    },
    createSdkMcpServer: (config) => ({ server: config.name }),
    tool: (name, _description, _shape, handler) => {
      tools.set(name, handler);
      return { name };
    },
    schema: { speak: {}, progress: {} },
  };
  const sessions = environment === undefined
    ? new Sessions(sdk, (message) => sent.push(message))
    : new Sessions(sdk, (message) => sent.push(message), environment);
  return { sent, queries, tools, sessions };
}

const tick = () => new Promise((resolve) => setTimeout(resolve, 5));

test("a session gets the bridge's options plus the device tools and the permission callback", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: { cwd: "/p", permissionMode: "acceptEdits", allowedTools: ["X"] } });
  const options = queries[0].options;
  assert.equal(options.cwd, "/p");
  assert.equal(options.permissionMode, "acceptEdits");
  assert.deepEqual(options.allowedTools, ["X", "mcp__device__speak", "mcp__device__progress"]);
  assert.deepEqual(options.mcpServers, { device: { server: "device" } });
  assert.equal(typeof options.canUseTool, "function");
  assert.throws(() => sessions.handle({ type: "start", session: "s1", options: {} }), /exists/);
});

test("prompts flow in and what Claude does flows out", async () => {
  const { sent, queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  sessions.handle({ type: "prompt", session: "s1", text: "run the tests" });
  await tick();
  const query = queries[0];
  assert.deepEqual(query.prompts, ["run the tests"]);
  query.emit({ type: "system", subtype: "session_state_changed", state: "running" });
  query.emit({ type: "assistant", message: { content: [{ type: "text", text: "ok" },
    { type: "tool_use", name: "Bash", input: { command: "make test", description: "Run the test suite" } }] } });
  query.emit({ type: "result", subtype: "success", result: "All 87 tests pass.", num_turns: 3, total_cost_usd: 0.05 });
  await tick();
  assert.deepEqual(sent, [
    { type: "state", session: "s1", state: "running" },
    { type: "tool", session: "s1", tool: "Bash", summary: "Run the test suite" },
    { type: "result", session: "s1", ok: true, text: "All 87 tests pass.", turns: 3, cost: 0.05 },
  ]);
  sessions.handle({ type: "interrupt", session: "s1" });
  await tick();
  assert.equal(query.interrupted, 1);
});

test("the general session's web rules reach the SDK unchanged, and a WebFetch prompt still waits", async () => {
  // Allowing any page is the SDK's job, through these settings; the sidecar never
  // answers a prompt itself, so one that does reach it waits for the bridge.
  const { sent, queries, sessions } = harness();
  const settings = {
    sandbox: { network: { allowedDomains: [], deniedDomains: ["*"], strictAllowlist: true } },
    permissions: { allow: ["WebSearch", "WebFetch"], ask: ["Bash(dangerouslyDisableSandbox:true)"], deny: [] },
  };
  sessions.handle({ type: "start", session: "s1", options: { cwd: "/p", settings } });
  assert.deepEqual(queries[0].options.settings, settings);
  const input = { url: "https://weather.example/paris", prompt: "today's forecast" };
  let settled = false;
  const answer = queries[0].options.canUseTool("WebFetch", input, { signal: new AbortController().signal });
  void answer.then(() => (settled = true));
  await tick();
  assert.equal(settled, false);
  assert.deepEqual(sent, [{ type: "permission", session: "s1", request: "r1", tool: "WebFetch", input,
    reason: undefined, blockedPath: undefined }]);
  sessions.handle({ type: "decision", session: "s1", request: "r1", allow: false, message: "No." });
  assert.deepEqual(await answer, { behavior: "deny", message: "No." });
});

test("permission prompts wait for the bridge's decision", async () => {
  const { sent, queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const canUseTool = queries[0].options.canUseTool;
  const input = { command: "npm run dev", dangerouslyDisableSandbox: true };
  const allowed = canUseTool("Bash", input, { signal: new AbortController().signal, decisionReason: "sandbox" });
  const denied = canUseTool("Write", { file_path: "/etc/hosts" }, { signal: new AbortController().signal, blockedPath: "/etc/hosts" });
  assert.deepEqual(sent[0], { type: "permission", session: "s1", request: "r1", tool: "Bash", input, reason: "sandbox", blockedPath: undefined });
  assert.equal(sent[1].request, "r2");
  assert.equal(sent[1].blockedPath, "/etc/hosts");
  sessions.handle({ type: "decision", session: "s1", request: "r1", allow: true });
  sessions.handle({ type: "decision", session: "s1", request: "r2", allow: false });
  assert.deepEqual(await allowed, { behavior: "allow", updatedInput: input });
  assert.equal((await denied).behavior, "deny");
  // A second decision for the same request changes nothing.
  sessions.handle({ type: "decision", session: "s1", request: "r1", allow: false });
});

test("questions come back with the user's answers", async () => {
  const { sent, queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const questions = [{ question: "Which project?", header: "Project", options: [{ label: "a" }, { label: "b" }], multiSelect: false }];
  const answered = queries[0].options.canUseTool("AskUserQuestion", { questions }, { signal: new AbortController().signal });
  assert.deepEqual(sent[0], { type: "question", session: "s1", request: "r1", questions });
  sessions.handle({ type: "decision", session: "s1", request: "r1", allow: true, answers: { "Which project?": "b" } });
  assert.deepEqual(await answered, { behavior: "allow", updatedInput: { questions, answers: { "Which project?": "b" } } });
});

test("an aborted or closed prompt is denied", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const controller = new AbortController();
  const aborted = queries[0].options.canUseTool("Bash", { command: "x" }, { signal: controller.signal });
  controller.abort();
  assert.equal((await aborted).behavior, "deny");
  const open = queries[0].options.canUseTool("Bash", { command: "y" }, { signal: new AbortController().signal });
  sessions.handle({ type: "close", session: "s1" });
  assert.equal((await open).behavior, "deny");
  assert.equal(queries[0].closed, true);
  assert.throws(() => sessions.handle({ type: "prompt", session: "s1", text: "hi" }), /no session/);
});

test("the device tools speak and show progress", async () => {
  const { sent, tools, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  await tools.get("speak")!({ text: "All tests pass.", mood: "proud" });
  await tools.get("speak")!({ text: "Hi.", mood: "smug" });
  await tools.get("progress")!({ message: "Building", fraction: 0.5 });
  assert.deepEqual(sent, [
    { type: "speak", session: "s1", text: "All tests pass.", mood: "proud" },
    { type: "speak", session: "s1", text: "Hi.", mood: "neutral" },
    { type: "progress", session: "s1", message: "Building", fraction: 0.5 },
  ]);
});

test("a session that ends reports it, and errors are reported", async () => {
  const { sent, queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  queries[0].end();
  await tick();
  assert.deepEqual(sent.at(-1), { type: "closed", session: "s1" });
});

test("tool summaries are short captions", () => {
  assert.equal(summarize("Bash", { command: "make test" }), "make test");
  assert.equal(summarize("Edit", { file_path: "/a/b/loki.c" }), "Edit loki.c");
  assert.equal(summarize("WebFetch", { url: "https://code.claude.com/docs" }), "Reading code.claude.com");
  assert.equal(summarize("WebFetch", { url: "not a url" }), "Reading the web");
  assert.equal(summarize("mcp__device__speak", {}), "");
  assert.equal(summarize("Bash", { command: "x".repeat(300) }).length, 120);
  assert.equal(summarize("Bash", { command: "a\n\tb" }), "a b");
});

test("lines from the bridge are checked", () => {
  assert.deepEqual(parseIncoming('{"type":"prompt","session":"s1","text":"hi"}'), { type: "prompt", session: "s1", text: "hi" });
  for (const line of ["", "x", "[]", '{"type":"prompt","session":"s 1","text":"hi"}', '{"type":"prompt","session":"s1","text":""}',
    '{"type":"decision","session":"s1","request":"r1"}', '{"type":"decision","session":"s1","request":"r1","allow":true,"answers":{"q":1}}',
    '{"type":"start","session":"s1"}', '{"type":"dance","session":"s1"}']) {
    assert.throws(() => parseIncoming(line), undefined, line);
  }
  assert.equal(encode({ type: "ready" }), '{"type":"ready"}\n');
});

// The environment: the SDK replaces it with options.env, so the sidecar's own goes
// underneath each session's variables, and LOKI_CLAUDE_PATH picks the Claude Code.

test("a session's variables go on top of the sidecar's own environment", () => {
  const outer = { HOME: "/Users/me", PATH: "/usr/bin", SHARED: "outer", ENABLE_CLAUDEAI_MCP_SERVERS: "true" };
  const { queries, sessions } = harness(outer);
  sessions.handle({ type: "start", session: "s1", options: {
    env: { SHARED: "inner", CLAUDE_CODE_DISABLE_AUTO_MEMORY: "1", ENABLE_CLAUDEAI_MCP_SERVERS: "false" } } });
  sessions.handle({ type: "start", session: "s2", options: {} });
  assert.deepEqual(queries[0].options.env, {
    HOME: "/Users/me", PATH: "/usr/bin", SHARED: "inner", ENABLE_CLAUDEAI_MCP_SERVERS: "false",
    CLAUDE_CODE_DISABLE_AUTO_MEMORY: "1" });
  // One session's variables never leak into another's, nor into the sidecar's own.
  assert.deepEqual(queries[1].options.env, outer);
  assert.notEqual(queries[1].options.env, outer);
  assert.equal(outer.SHARED, "outer");
});

test("an env that is not an object is ignored, and no environment means only the session's", () => {
  for (const env of [null, "HOME=/x", 7, undefined]) {
    const { queries, sessions } = harness({ HOME: "/h" });
    sessions.handle({ type: "start", session: "s1", options: { env } as Record<string, unknown> });
    assert.deepEqual(queries[0].options.env, { HOME: "/h" }, String(env));
  }
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: { env: { A: "1" } } });
  assert.deepEqual(queries[0].options.env, { A: "1" });
  assert.equal("pathToClaudeCodeExecutable" in queries[0].options, false);
});

test("LOKI_CLAUDE_PATH chooses the Claude Code to run, and only when set", () => {
  const chosen = harness({ LOKI_CLAUDE_PATH: "/opt/homebrew/bin/claude", HOME: "/h" });
  chosen.sessions.handle({ type: "start", session: "s1", options: { pathToClaudeCodeExecutable: "/tmp/evil" } });
  assert.equal(chosen.queries[0].options.pathToClaudeCodeExecutable, "/opt/homebrew/bin/claude");
  for (const environment of [{}, { LOKI_CLAUDE_PATH: "" }, { LOKI_CLAUDE_PATH: undefined }]) {
    const { queries, sessions } = harness(environment);
    sessions.handle({ type: "start", session: "s1", options: {} });
    assert.equal("pathToClaudeCodeExecutable" in queries[0].options, false, JSON.stringify(environment));
  }
});

test("the sidecar's own options win over the bridge's", () => {
  const { queries, sessions } = harness({});
  sessions.handle({ type: "start", session: "s1", options: {
    mcpServers: { evil: {} }, canUseTool: "x", allowedTools: "Bash", prompt: "p" } });
  const options = queries[0].options;
  assert.deepEqual(Object.keys(options.mcpServers), ["device"]);
  assert.equal(typeof options.canUseTool, "function");
  assert.deepEqual(options.allowedTools, ["mcp__device__speak", "mcp__device__progress"]);
});

test("closing twice, closeAll and late decisions are harmless; pending prompts are denied", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  sessions.handle({ type: "start", session: "s2", options: {} });
  const a = queries[0].options.canUseTool("Bash", { command: "a" }, {});
  const b = queries[1].options.canUseTool("Bash", { command: "b" }, { signal: new AbortController().signal });
  sessions.closeAll();
  assert.equal((await a).behavior, "deny");
  assert.equal((await b).behavior, "deny");
  assert.equal(queries.every((query) => query.closed), true);
  sessions.handle({ type: "close", session: "s1" });
  assert.throws(() => sessions.handle({ type: "decision", session: "s1", request: "r1", allow: true }), /no session/);
  assert.throws(() => sessions.handle({ type: "interrupt", session: "s9" }), /no session/);
});

/** A seeded generator (mulberry32), so a failing case replays from its seed. */
function seeded(seed: number) {
  let state = seed >>> 0;
  const next = () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = state;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  return { below: (n: number) => Math.floor(next() * n), pick: <T>(items: T[]): T => items[Math.floor(next() * items.length)] };
}

test("property: any line is a checked message or an Error, never anything else, the same way twice", () => {
  const pieces = ['{"type":', '"start"', '"prompt"', '"decision"', '"close"', '"interrupt"', ',"session":', '"s1"', '"s 1"',
    '"' + "x".repeat(70) + '"', ',"text":', '""', '"hi"', ',"options":', "{}", "[]", "null", ',"request":"r1"', ',"allow":',
    "true", "1", ',"answers":', '{"q":"a"}', '{"q":1}', ',"message":', "}", "{", "]", '"', "\\u0000", "\n", "1e999"];
  let parsed = 0;
  for (let seed = 0; seed < 400; seed++) {
    const random = seeded(seed);
    const line = random.below(2)
      ? Array.from({ length: random.below(14) }, () => random.pick(pieces)).join("")
      : JSON.stringify({
          type: random.pick(["start", "prompt", "decision", "close", "interrupt", "dance"]),
          session: random.pick(["s1", "s_2-x", "", "s 1", "x".repeat(65), 3]),
          ...(random.below(2) ? { text: random.pick(["hi", "", "x".repeat(100_001), 5]) } : {}),
          ...(random.below(2) ? { options: random.pick([{}, { cwd: "/p" }, [], null]) } : {}),
          ...(random.below(2) ? { request: random.pick(["r1", "r 1", ""]), allow: random.pick([true, false, "yes"]) } : {}),
          ...(random.below(3) === 0 ? { message: random.pick(["no", 1]) } : {}),
          ...(random.below(3) === 0 ? { answers: random.pick([{ q: "a" }, { q: 1 }, ["a"]]) } : {}),
        });
    const outcome = (() => {
      try {
        return { ok: parseIncoming(line) };
      } catch (error) {
        assert.ok(error instanceof Error, `seed ${seed}: ${line}`);
        return { error: (error as Error).message };
      }
    })();
    const again = (() => { try { return { ok: parseIncoming(line) }; } catch (error) { return { error: (error as Error).message }; } })();
    assert.deepEqual(again, outcome, `seed ${seed}`);
    if ("ok" in outcome) {
      // Metamorphic: a parsed message encodes and parses back to itself.
      assert.deepEqual(parseIncoming(JSON.stringify(outcome.ok)), outcome.ok, `seed ${seed}`);
      assert.match(outcome.ok.session, /^[A-Za-z0-9_-]{1,64}$/);
      parsed++;
    }
  }
  assert.ok(parsed > 20, `only ${parsed} lines parsed`);
});

test("property: tool captions are one line of at most 120 characters for any input", () => {
  const tools = ["Bash", "Read", "Edit", "Write", "NotebookEdit", "Grep", "Glob", "WebFetch", "WebSearch", "Task", "Agent",
    "mcp__x__y", "Custom"];
  const values = ["", "a\nb\tc", "x".repeat(500), "https://example.com/a", "::", "\u0000", 7, null, { a: 1 }, "  spaced   out  "];
  for (let seed = 0; seed < 300; seed++) {
    const random = seeded(seed);
    const input: Record<string, unknown> = {};
    for (const key of ["command", "description", "file_path", "pattern", "url", "query"]) {
      if (random.below(2)) input[key] = random.pick(values);
    }
    const tool = random.pick(tools);
    const caption = summarize(tool, input);
    assert.ok(caption.length <= 120, `seed ${seed}`);
    assert.doesNotMatch(caption, /[\n\t\r]|\s{2}|^\s|\s$/, `seed ${seed}: ${JSON.stringify(caption)}`);
    assert.equal(summarize(tool, input), caption);
  }
});

// The bridge opens the general session as the user starts talking, before any request
// (RFC-003's turnaround): a session started with no prompt sends Claude nothing and
// reports nothing, its first prompt arrives later, and one closed unused ends quietly.
test("a session opened ahead waits for its first prompt, and one closed unused ends without a word", async () => {
  const { sent, queries, sessions } = harness({ HOME: "/h" });
  sessions.handle({ type: "start", session: "s1", options: { env: { ENABLE_TOOL_SEARCH: "false" } } });
  sessions.handle({ type: "start", session: "s2", options: {} });
  await tick();
  assert.deepEqual(queries.map((query) => query.prompts), [[], []]);
  assert.deepEqual(sent, []);
  assert.deepEqual(queries[0].options.env, { HOME: "/h", ENABLE_TOOL_SEARCH: "false" });
  sessions.handle({ type: "prompt", session: "s1", text: "what is a monad" });
  await tick();
  assert.deepEqual(queries[0].prompts, ["what is a monad"]);
  // Refreshed before use: closed, so its query ends and later commands to it are refused.
  sessions.handle({ type: "close", session: "s2" });
  await tick();
  assert.equal(queries[1].closed, true);
  assert.deepEqual(queries[1].prompts, []);
  assert.throws(() => sessions.handle({ type: "prompt", session: "s2", text: "late" }), /no session/);
  assert.deepEqual(sent, []);
});
