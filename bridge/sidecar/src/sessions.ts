// Claude Code sessions for Loki Bridge (RFC-003, ADR-014). The Agent SDK is passed in,
// so this logic is tested without it. Each session reads the user's words from an
// inbox (streaming input), reports what Claude does, and turns every permission
// prompt and question into a request the bridge answers from the board.
import { MOODS, type Incoming, type Outgoing } from "./protocol.ts";

type Message = Record<string, any>;
type PermissionResult =
  | { behavior: "allow"; updatedInput: Record<string, unknown> }
  | { behavior: "deny"; message: string };

export interface Query extends AsyncIterable<Message> {
  interrupt(): Promise<unknown>;
  close?(): void;
}

export interface Sdk {
  query(args: { prompt: AsyncIterable<Message>; options: Record<string, unknown> }): Query;
  createSdkMcpServer(config: { name: string; version: string; tools: unknown[] }): unknown;
  tool(name: string, description: string, shape: unknown, handler: (args: any) => Promise<unknown>): unknown;
  /** Zod shapes for the device tools' arguments. */
  schema: { speak: unknown; progress: unknown };
}

export type Send = (message: Outgoing) => void;

/** User messages for one session, delivered as the SDK reads them. */
class Inbox implements AsyncIterable<Message> {
  private queue: Message[] = [];
  private waiting: ((result: IteratorResult<Message>) => void) | undefined;
  private done = false;

  push(text: string): void {
    const message = { type: "user", message: { role: "user", content: text }, parent_tool_use_id: null };
    if (this.waiting) {
      const resolve = this.waiting;
      this.waiting = undefined;
      resolve({ value: message, done: false });
    } else {
      this.queue.push(message);
    }
  }

  close(): void {
    this.done = true;
    this.waiting?.({ value: undefined, done: true });
    this.waiting = undefined;
  }

  [Symbol.asyncIterator](): AsyncIterator<Message> {
    return {
      next: () => {
        const next = this.queue.shift();
        if (next) return Promise.resolve({ value: next, done: false });
        if (this.done) return Promise.resolve({ value: undefined, done: true });
        return new Promise((resolve) => (this.waiting = resolve));
      },
    };
  }
}

/** A short line for the board's caption about what a tool is doing. */
export function summarize(tool: string, input: Record<string, unknown>): string {
  const text = (value: unknown) => (typeof value === "string" ? value : "");
  const base = (path: unknown) => text(path).split("/").filter(Boolean).pop() ?? "";
  const pick = (() => {
    switch (tool) {
      case "Bash":
        return text(input.description) || text(input.command);
      case "Read":
      case "Edit":
      case "Write":
      case "NotebookEdit":
        return `${tool} ${base(input.file_path)}`;
      case "Grep":
      case "Glob":
        return `Searching ${text(input.pattern)}`;
      case "WebFetch":
        try {
          return `Reading ${new URL(text(input.url)).host}`;
        } catch {
          return "Reading the web";
        }
      case "WebSearch":
        return `Searching the web for ${text(input.query)}`;
      case "Task":
      case "Agent":
        return text(input.description) || "Delegating";
      default:
        return tool.startsWith("mcp__") ? "" : tool;
    }
  })();
  const line = pick.replace(/\s+/g, " ").trim();
  if (line.length <= 120) return line;
  // Cut between characters, never inside one: half a surrogate pair is not JSON the
  // bridge can read.
  let cut = "";
  for (const character of line) {
    if (cut.length + character.length > 119) break;
    cut += character;
  }
  return cut + "…";
}

class Session {
  private readonly inbox = new Inbox();
  private readonly pending = new Map<
    string,
    { input: Record<string, unknown>; resolve: (result: PermissionResult) => void }
  >();
  private counter = 0;
  private readonly query: Query;
  private readonly id: string;
  private readonly send: Send;
  private closed = false;

  private readonly forget: () => void;

  /** forget: called once the session has ended, so its id is no longer known. */
  constructor(
    id: string,
    options: Record<string, unknown>,
    sdk: Sdk,
    send: Send,
    environment: Record<string, string | undefined>,
    forget: () => void = () => {},
  ) {
    this.id = id;
    this.send = send;
    this.forget = forget;
    const device = sdk.createSdkMcpServer({
      name: "device",
      version: "1.0.0",
      tools: [
        sdk.tool(
          "speak",
          "Say one or two plain sentences aloud on the user's Loki device: what you did or found. " +
            "Call it once when you finish. No markdown, code, or lists.",
          sdk.schema.speak,
          async (args: { text: string; mood?: string }) => {
            const mood = MOODS.includes(args.mood as (typeof MOODS)[number]) ? args.mood : "neutral";
            this.send({ type: "speak", session: this.id, text: String(args.text), mood });
            return { content: [{ type: "text", text: "Said on the device." }] };
          },
        ),
        sdk.tool(
          "progress",
          "Show a short status line (and optionally how far along, 0 to 1) on the user's Loki device.",
          sdk.schema.progress,
          async (args: { message: string; fraction?: number }) => {
            this.send({ type: "progress", session: this.id, message: String(args.message), fraction: args.fraction });
            return { content: [{ type: "text", text: "Shown." }] };
          },
        ),
      ],
    });
    const allowed = Array.isArray(options.allowedTools) ? (options.allowedTools as string[]) : [];
    // The SDK replaces the environment with options.env, so the session's variables
    // go on top of the sidecar's own: without HOME and PATH, Claude Code finds no login.
    const extra = typeof options.env === "object" && options.env !== null ? (options.env as Record<string, string>) : {};
    this.query = sdk.query({
      prompt: this.inbox,
      options: {
        ...options,
        env: { ...environment, ...extra },
        // The Claude Code the bridge chose (the user's own, which holds their login),
        // else the SDK's bundled copy.
        ...(environment.LOKI_CLAUDE_PATH ? { pathToClaudeCodeExecutable: environment.LOKI_CLAUDE_PATH } : {}),
        mcpServers: { device },
        allowedTools: [...allowed, "mcp__device__speak", "mcp__device__progress"],
        canUseTool: (tool: string, input: Record<string, unknown>, context: Record<string, any>) =>
          this.permission(tool, input, context),
      },
    });
    void this.pump();
  }

  prompt(text: string): void {
    this.inbox.push(text);
  }

  async interrupt(): Promise<void> {
    await this.query.interrupt();
  }

  decide(decision: Extract<Incoming, { type: "decision" }>): void {
    const waiting = this.pending.get(decision.request);
    if (!waiting) return;
    this.pending.delete(decision.request);
    if (!decision.allow) {
      waiting.resolve({ behavior: "deny", message: decision.message ?? "The user said no on the Loki device." });
    } else {
      const input = decision.answers ? { ...waiting.input, answers: decision.answers } : waiting.input;
      waiting.resolve({ behavior: "allow", updatedInput: input });
    }
  }

  close(): void {
    if (this.closed) return;
    this.closed = true;
    for (const [request, waiting] of this.pending) {
      this.pending.delete(request);
      waiting.resolve({ behavior: "deny", message: "The session closed." });
    }
    this.inbox.close();
    this.query.close?.();
  }

  private permission(
    tool: string,
    input: Record<string, unknown>,
    context: Record<string, any>,
  ): Promise<PermissionResult> {
    const request = `r${++this.counter}`;
    return new Promise((resolve) => {
      const signal: AbortSignal | undefined = context?.signal;
      if (signal?.aborted) return resolve({ behavior: "deny", message: "Cancelled." });
      this.pending.set(request, { input, resolve });
      signal?.addEventListener("abort", () => {
        if (this.pending.delete(request)) resolve({ behavior: "deny", message: "Cancelled." });
      });
      if (tool === "AskUserQuestion") {
        this.send({ type: "question", session: this.id, request, questions: input.questions });
      } else {
        this.send({
          type: "permission",
          session: this.id,
          request,
          tool,
          input,
          reason: typeof context?.decisionReason === "string" ? context.decisionReason : undefined,
          blockedPath: typeof context?.blockedPath === "string" ? context.blockedPath : undefined,
        });
      }
    });
  }

  private report(message: Message): void {
    if (message.type === "system" && message.subtype === "session_state_changed") {
      this.send({ type: "state", session: this.id, state: message.state });
    } else if (message.type === "assistant" && Array.isArray(message.message?.content)) {
      for (const block of message.message.content) {
        if (block?.type !== "tool_use") continue;
        const summary = summarize(String(block.name), block.input ?? {});
        if (summary) this.send({ type: "tool", session: this.id, tool: block.name, summary });
      }
    } else if (message.type === "result") {
      this.send({
        type: "result",
        session: this.id,
        ok: message.subtype === "success" && !message.is_error,
        text: typeof message.result === "string" ? message.result : "",
        turns: message.num_turns ?? 0,
        cost: message.total_cost_usd ?? 0,
      });
    }
  }

  private async pump(): Promise<void> {
    try {
      for await (const message of this.query) this.report(message);
    } catch (error) {
      this.send({ type: "error", session: this.id, message: error instanceof Error ? error.message : String(error) });
    } finally {
      this.close();
      this.forget();
      this.send({ type: "closed", session: this.id });
    }
  }
}

/** All sessions, driven by the bridge's messages. */
export class Sessions {
  private readonly sessions = new Map<string, Session>();
  private readonly sdk: Sdk;
  private readonly send: Send;
  private readonly environment: Record<string, string | undefined>;

  /** environment: the sidecar's own (process.env), under each session's options.env. */
  constructor(sdk: Sdk, send: Send, environment: Record<string, string | undefined> = {}) {
    this.sdk = sdk;
    this.send = send;
    this.environment = environment;
  }

  handle(message: Incoming): void {
    const session = this.sessions.get(message.session);
    switch (message.type) {
      case "start": {
        if (session) throw new Error(`session ${message.session} exists`);
        const id = message.session;
        try {
          const started: Session = new Session(id, message.options, this.sdk, this.send, this.environment, () => {
            if (this.sessions.get(id) === started) this.sessions.delete(id);
          });
          this.sessions.set(id, started);
        } catch (error) {
          // The bridge learns which session failed only from what is sent.
          this.send({ type: "error", session: id, message: error instanceof Error ? error.message : String(error) });
          this.send({ type: "closed", session: id });
        }
        return;
      }
      case "close":
        session?.close();
        this.sessions.delete(message.session);
        return;
    }
    if (!session) throw new Error(`no session ${message.session}`);
    if (message.type === "prompt") session.prompt(message.text);
    else if (message.type === "decision") session.decide(message);
    else
      session.interrupt().catch((error: unknown) =>
        this.send({ type: "error", session: message.session, message: error instanceof Error ? error.message : String(error) }),
      );
  }

  closeAll(): void {
    for (const session of this.sessions.values()) session.close();
    this.sessions.clear();
  }
}
