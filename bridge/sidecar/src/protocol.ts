// The sidecar's JSON-lines protocol with Loki Bridge, over its stdin and stdout
// (RFC-003). One JSON object per line; the bridge is trusted, but every message is
// still checked, so a broken line is reported instead of acted on.
//
// Bridge to sidecar:
//   {type:"start", session, options}     open a Claude Code session (options: SessionPolicy)
//   {type:"prompt", session, text}       what the user said
//   {type:"interrupt", session}          stop the current turn
//   {type:"decision", session, request, allow, message?, answers?}   answer a permission or question
//   {type:"close", session}
// Sidecar to bridge:
//   {type:"ready", sdk}                  once, at start
//   {type:"state", session, state}       running | idle | requires_action
//   {type:"tool", session, tool, summary}
//   {type:"permission", session, request, tool, input, reason?, blockedPath?}
//   {type:"question", session, request, questions}
//   {type:"speak", session, text, mood}  from the device server's speak tool
//   {type:"progress", session, message, fraction?}
//   {type:"result", session, ok, text, turns, cost}
//   {type:"error", session?, message}    and {type:"closed", session}

export type Incoming =
  | { type: "start"; session: string; options: Record<string, unknown> }
  | { type: "prompt"; session: string; text: string }
  | { type: "interrupt"; session: string }
  | {
      type: "decision";
      session: string;
      request: string;
      allow: boolean;
      message?: string;
      answers?: Record<string, string>;
    }
  | { type: "close"; session: string };

export type Outgoing = Record<string, unknown> & { type: string };

export const MOODS = ["neutral", "happy", "proud", "sorry", "concerned", "excited"] as const;

const ID = /^[A-Za-z0-9_-]{1,64}$/;
const MAX_TEXT = 100_000;

function isObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

/** Parses one line from the bridge; throws with a reason when it is not a message. */
export function parseIncoming(line: string): Incoming {
  let value: unknown;
  try {
    value = JSON.parse(line);
  } catch {
    throw new Error("not JSON");
  }
  if (!isObject(value) || typeof value.type !== "string") throw new Error("not a message");
  const session = value.session;
  if (typeof session !== "string" || !ID.test(session)) throw new Error("bad session id");
  switch (value.type) {
    case "start":
      if (!isObject(value.options)) throw new Error("start needs options");
      return { type: "start", session, options: value.options };
    case "prompt":
      if (typeof value.text !== "string" || value.text.length === 0 || value.text.length > MAX_TEXT) {
        throw new Error("prompt needs text");
      }
      return { type: "prompt", session, text: value.text };
    case "interrupt":
    case "close":
      return { type: value.type, session };
    case "decision": {
      const { request, allow, message, answers } = value;
      if (typeof request !== "string" || !ID.test(request) || typeof allow !== "boolean") {
        throw new Error("decision needs request and allow");
      }
      if (message !== undefined && typeof message !== "string") throw new Error("bad message");
      if (
        answers !== undefined &&
        (!isObject(answers) || !Object.values(answers).every((answer) => typeof answer === "string"))
      ) {
        throw new Error("bad answers");
      }
      return {
        type: "decision",
        session,
        request,
        allow,
        ...(message === undefined ? {} : { message }),
        ...(answers === undefined ? {} : { answers: answers as Record<string, string> }),
      };
    }
    default:
      throw new Error(`unknown type ${value.type}`);
  }
}

/** One line for the bridge. */
export function encode(message: Outgoing): string {
  return JSON.stringify(message) + "\n";
}
