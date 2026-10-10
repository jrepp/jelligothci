// The sidecar at its edges, against a fake Agent SDK: prompts in order however early
// they come, permission prompts settled exactly once, every result shape reported,
// lines from the bridge checked at their limits, and captions the bridge can parse.
import { test } from "node:test";
import assert from "node:assert/strict";
import { Sessions, summarize, type Sdk } from "../src/sessions.ts";
import { parseIncoming, type Outgoing } from "../src/protocol.ts";

/** A fake query: yields what the test emits, records the prompts it reads, can fail. */
class FakeQuery {
  options: Record<string, any>;
  prompts: string[] = [];
  promptsEnded = false;
  closed = false;
  interruptError: Error | undefined;
  private queue: any[] = [];
  private waiting: { resolve: (value: IteratorResult<any>) => void; reject: (error: unknown) => void } | undefined;
  private ended = false;
  private failure: Error | undefined;

  constructor(prompt: AsyncIterable<any>, options: Record<string, any>) {
    this.options = options;
    void (async () => {
      for await (const message of prompt) this.prompts.push(message.message.content);
      this.promptsEnded = true;
    })();
  }
  emit(message: any): void {
    if (this.waiting) {
      const { resolve } = this.waiting;
      this.waiting = undefined;
      resolve({ value: message, done: false });
    } else this.queue.push(message);
  }
  end(): void {
    this.ended = true;
    this.waiting?.resolve({ value: undefined, done: true });
  }
  fail(error: Error): void {
    this.failure = error;
    this.waiting?.reject(error);
  }
  async interrupt(): Promise<void> {
    if (this.interruptError) throw this.interruptError;
  }
  close(): void {
    this.closed = true;
  }
  [Symbol.asyncIterator](): AsyncIterator<any> {
    return {
      next: () => {
        const next = this.queue.shift();
        if (next) return Promise.resolve({ value: next, done: false });
        if (this.failure) return Promise.reject(this.failure);
        if (this.ended) return Promise.resolve({ value: undefined, done: true });
        return new Promise((resolve, reject) => (this.waiting = { resolve, reject }));
      },
    };
  }
}

function harness(options: { queryThrows?: Error } = {}) {
  const sent: Outgoing[] = [];
  const queries: FakeQuery[] = [];
  const sdk: Sdk = {
    query: ({ prompt, options: given }) => {
      if (options.queryThrows) throw options.queryThrows;
      const query = new FakeQuery(prompt, given);
      queries.push(query);
      return query as any;
    },
    createSdkMcpServer: (config) => ({ server: config.name }),
    tool: (name) => ({ name }),
    schema: { speak: {}, progress: {} },
  };
  return { sent, queries, sessions: new Sessions(sdk, (message) => sent.push(message), {}) };
}

const tick = () => new Promise((resolve) => setTimeout(resolve, 5));
const signal = () => new AbortController().signal;

// Prompts

test("prompts given before the SDK reads, and while it waits, arrive in order", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  for (const text of ["one", "two", "three"]) sessions.handle({ type: "prompt", session: "s1", text });
  await tick();
  sessions.handle({ type: "prompt", session: "s1", text: "four" });
  await tick();
  assert.deepEqual(queries[0].prompts, ["one", "two", "three", "four"]);
  // Closing ends the prompts the SDK reads, so its query can finish.
  sessions.handle({ type: "close", session: "s1" });
  await tick();
  assert.equal(queries[0].promptsEnded, true);
});

test("sessions are independent: a request id answers only its own session", async () => {
  const { sent, queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  sessions.handle({ type: "start", session: "s2", options: {} });
  const one = queries[0].options.canUseTool("Bash", { command: "a" }, { signal: signal() });
  const two = queries[1].options.canUseTool("Bash", { command: "b" }, { signal: signal() });
  assert.deepEqual(sent.map((m) => [m.session, m.request]), [["s1", "r1"], ["s2", "r1"]]);
  sessions.handle({ type: "decision", session: "s2", request: "r1", allow: false, message: "No." });
  sessions.handle({ type: "decision", session: "s1", request: "r1", allow: true });
  assert.deepEqual(await one, { behavior: "allow", updatedInput: { command: "a" } });
  assert.deepEqual(await two, { behavior: "deny", message: "No." });
});

// Permission prompts are settled exactly once

test("an abort after the decision changes nothing, and a decision after an abort is ignored", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const canUseTool = queries[0].options.canUseTool;
  const first = new AbortController();
  const decided = canUseTool("Bash", { command: "a" }, { signal: first.signal });
  sessions.handle({ type: "decision", session: "s1", request: "r1", allow: true });
  first.abort();
  assert.equal((await decided).behavior, "allow");
  const second = new AbortController();
  const aborted = canUseTool("Bash", { command: "b" }, { signal: second.signal });
  second.abort();
  sessions.handle({ type: "decision", session: "s1", request: "r2", allow: true });
  assert.deepEqual(await aborted, { behavior: "deny", message: "Cancelled." });
});

test("a denial without a message says the user said no; answers go only with an allow", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const canUseTool = queries[0].options.canUseTool;
  const denied = canUseTool("Bash", { command: "a" }, { signal: signal() });
  sessions.handle({ type: "decision", session: "s1", request: "r1", allow: false, answers: { q: "a" } });
  assert.deepEqual(await denied, { behavior: "deny", message: "The user said no on the Loki device." });
  const input = { questions: [{ question: "q" }] };
  const unanswered = canUseTool("AskUserQuestion", input, { signal: signal() });
  sessions.handle({ type: "decision", session: "s1", request: "r2", allow: true });
  assert.deepEqual(await unanswered, { behavior: "allow", updatedInput: input });
  assert.equal("answers" in input, false); // The SDK's input is never changed in place.
});

test("a prompt whose signal was already aborted is denied at once", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const controller = new AbortController();
  controller.abort();
  const result = await Promise.race([
    queries[0].options.canUseTool("Bash", { command: "a" }, { signal: controller.signal }),
    tick().then(() => "still waiting"),
  ]);
  assert.deepEqual(result, { behavior: "deny", message: "Cancelled." });
});

// What Claude does, reported

test("every result shape is reported, ok only for a success that is not an error", async () => {
  const { sent, queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const query = queries[0];
  query.emit({ type: "result", subtype: "error_max_turns", num_turns: 30, total_cost_usd: 1.5 });
  query.emit({ type: "result", subtype: "success", is_error: true, result: "API Error: 529" });
  query.emit({ type: "result", subtype: "success", result: { not: "text" } });
  query.emit({ type: "system", subtype: "init" });
  query.emit({ type: "assistant", message: { content: "plain text, not blocks" } });
  query.emit({ type: "assistant", message: { content: [null, { type: "tool_use", name: "Read" }] } });
  await tick();
  assert.deepEqual(sent, [
    { type: "result", session: "s1", ok: false, text: "", turns: 30, cost: 1.5 },
    { type: "result", session: "s1", ok: false, text: "API Error: 529", turns: 0, cost: 0 },
    { type: "result", session: "s1", ok: true, text: "", turns: 0, cost: 0 },
    { type: "tool", session: "s1", tool: "Read", summary: "Read" },
  ]);
});

test("a query that fails reports the error with its session, then closes, once", async () => {
  const { sent, queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  const pending = queries[0].options.canUseTool("Bash", { command: "a" }, { signal: signal() });
  sent.length = 0;
  queries[0].fail(new Error("Claude Code process exited with code 1"));
  await tick();
  assert.deepEqual(sent, [
    { type: "error", session: "s1", message: "Claude Code process exited with code 1" },
    { type: "closed", session: "s1" },
  ]);
  assert.deepEqual(await pending, { behavior: "deny", message: "The session closed." });
  assert.equal(queries[0].closed, true);
  sessions.handle({ type: "close", session: "s1" });
  await tick();
  assert.equal(sent.length, 2);
});

test("a session whose query ended is forgotten, so a late prompt is reported", async () => {
  const { queries, sessions } = harness();
  sessions.handle({ type: "start", session: "s1", options: {} });
  queries[0].end();
  await tick();
  assert.throws(() => sessions.handle({ type: "prompt", session: "s1", text: "hello?" }), /no session/);
});

test("a session that cannot start is reported as closed with its id", () => {
  const { sent, sessions } = harness({ queryThrows: new Error("bad options") });
  try {
    sessions.handle({ type: "start", session: "s1", options: {} });
  } catch {
    // The bridge only learns which session failed from what is sent.
  }
  assert.deepEqual(sent.at(-1), { type: "closed", session: "s1" });
});

// Lines from the bridge, at their limits

test("ids, text and fields are checked at their exact limits", () => {
  const line = (message: Record<string, unknown>) => JSON.stringify(message);
  const ok = (message: Record<string, unknown>) => parseIncoming(line(message));
  const bad = (message: Record<string, unknown>) => assert.throws(() => parseIncoming(line(message)), Error, line(message).slice(0, 80));
  assert.equal(ok({ type: "close", session: "a".repeat(64) }).session.length, 64);
  bad({ type: "close", session: "a".repeat(65) });
  for (const session of ["s/1", "s.1", "", "é", "s1\n", "../s1"]) bad({ type: "close", session });
  assert.equal((ok({ type: "prompt", session: "s1", text: "x".repeat(100_000) }) as any).text.length, 100_000);
  bad({ type: "prompt", session: "s1", text: "x".repeat(100_001) });
  bad({ type: "prompt", session: "s1", text: ["hi"] });
  bad({ type: "decision", session: "s1", request: "r 1", allow: true });
  bad({ type: "decision", session: "s1", request: "r1", allow: "true" });
  bad({ type: "decision", session: "s1", request: "r1", allow: true, message: 7 });
  bad({ type: "decision", session: "s1", request: "r1", allow: true, answers: ["a"] });
  bad({ type: "decision", session: "s1", request: "r1", allow: true, answers: null });
  bad({ type: "start", session: "s1", options: [] });
  bad({ type: 7, session: "s1" });
  // Fields a type does not have are not carried.
  assert.deepEqual(ok({ type: "close", session: "s1", text: "x", options: {} }), { type: "close", session: "s1" });
  assert.deepEqual(ok({ type: "decision", session: "s1", request: "r1", allow: false, extra: 1 }),
    { type: "decision", session: "s1", request: "r1", allow: false });
});

test("a __proto__ key in a session's options pollutes nothing", () => {
  const message = parseIncoming('{"type":"start","session":"s1","options":{"__proto__":{"polluted":true},"cwd":"/p"}}');
  const { queries, sessions } = harness();
  sessions.handle(message);
  assert.equal(({} as any).polluted, undefined);
  assert.equal(queries[0].options.cwd, "/p");
  assert.equal(Object.getPrototypeOf(queries[0].options), Object.prototype);
});

// Captions

test("captions for each tool, and the fallbacks when an input is missing", () => {
  assert.equal(summarize("WebSearch", { query: "swift 7 release" }), "Searching the web for swift 7 release");
  assert.equal(summarize("Task", { description: "Explore the code" }), "Explore the code");
  assert.equal(summarize("Agent", {}), "Delegating");
  assert.equal(summarize("Read", {}), "Read");
  assert.equal(summarize("Grep", {}), "Searching");
  assert.equal(summarize("Edit", { file_path: "/a/b/" }), "Edit b");
  assert.equal(summarize("Bash", { description: "", command: "ls" }), "ls");
  assert.equal(summarize("TodoWrite", { todos: [] }), "TodoWrite");
});

test("a long caption is never cut inside a character, so the bridge can parse it", () => {
  for (const tail of ["\u{1F984}\u{1F984}", "é".repeat(3)]) {
    for (let pad = 115; pad <= 121; pad++) {
      const caption = summarize("Bash", { command: "x".repeat(pad) + tail });
      assert.ok(caption.isWellFormed(), `${pad}: ${JSON.stringify(caption)}`);
    }
  }
});
