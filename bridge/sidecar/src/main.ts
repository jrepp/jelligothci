// Loki Bridge's Agent SDK sidecar (RFC-003): JSON lines on stdin and stdout, logs on
// stderr. The bridge starts it and passes each session's options (SessionPolicy).
import { createInterface } from "node:readline";
import { encode, MOODS, parseIncoming, type Outgoing } from "./protocol.ts";
import { Sessions } from "./sessions.ts";

const sdk = await import("@anthropic-ai/claude-agent-sdk");
const { z } = await import("zod");

const send = (message: Outgoing) => process.stdout.write(encode(message));
const sessions = new Sessions(
  {
    query: sdk.query as any,
    createSdkMcpServer: sdk.createSdkMcpServer as any,
    tool: sdk.tool as any,
    schema: {
      speak: { text: z.string().min(1).max(600), mood: z.enum(MOODS).optional() },
      progress: { message: z.string().min(1).max(200), fraction: z.number().min(0).max(1).optional() },
    },
  },
  send,
  process.env,
);

send({ type: "ready", sdk: "@anthropic-ai/claude-agent-sdk" });
const lines = createInterface({ input: process.stdin });
lines.on("line", (line) => {
  try {
    sessions.handle(parseIncoming(line));
  } catch (error) {
    send({ type: "error", message: error instanceof Error ? error.message : String(error) });
  }
});
lines.on("close", () => {
  sessions.closeAll();
  process.exit(0);
});
