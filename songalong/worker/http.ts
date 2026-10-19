import type { Context } from "hono";
import type { ContentfulStatusCode } from "hono/utils/http-status";
import type { AppEnv } from "./env";

/** An error whose message is safe to show the user. */
export class HttpError extends Error {
  constructor(
    readonly status: ContentfulStatusCode,
    message: string,
    readonly headers?: Record<string, string>,
  ) {
    super(message);
    this.name = "HttpError";
  }
}

export type Ctx = Context<AppEnv>;

const DEFAULT_JSON_LIMIT = 64 * 1024;

/** Reads a JSON object body, enforcing Content-Type and a size limit (streamed, so a missing
 *  Content-Length can't be used to send more). */
export async function readJsonObject(c: Ctx, maxBytes = DEFAULT_JSON_LIMIT): Promise<Record<string, unknown>> {
  const type = c.req.header("Content-Type") ?? "";
  if (!/^application\/json\s*(;|$)/i.test(type)) {
    throw new HttpError(415, "Expected a JSON request (Content-Type: application/json)");
  }
  const declared = Number(c.req.header("Content-Length") ?? "0");
  if (declared > maxBytes) throw new HttpError(413, "That request is too large");
  const body = c.req.raw.body;
  if (!body) throw new HttpError(400, "The request body is empty");

  const reader = body.getReader();
  const chunks: Uint8Array[] = [];
  let size = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.byteLength;
    if (size > maxBytes) {
      await reader.cancel().catch(() => {});
      throw new HttpError(413, "That request is too large");
    }
    chunks.push(value);
  }
  const bytes = new Uint8Array(size);
  let at = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, at);
    at += chunk.byteLength;
  }

  let parsed: unknown;
  try {
    parsed = JSON.parse(new TextDecoder().decode(bytes));
  } catch {
    throw new HttpError(400, "The request body is not valid JSON");
  }
  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
    throw new HttpError(400, "The request body must be a JSON object");
  }
  return parsed as Record<string, unknown>;
}

/** A required string field from a JSON body. */
export function stringField(body: Record<string, unknown>, field: string, label: string): string {
  const v = body[field];
  if (typeof v !== "string") throw new HttpError(400, `${label} is required`);
  return v;
}

/** Runs `task` after the response is sent when the runtime allows it (waitUntil), otherwise
 *  waits for it. Failures are logged, never surfaced to the client. */
export async function runInBackground(c: Ctx, task: Promise<unknown>, what: string): Promise<void> {
  const safe = task.catch((err: unknown) => console.error(`${what} failed:`, errorText(err)));
  let ctx: { waitUntil(p: Promise<unknown>): void } | undefined;
  try {
    ctx = c.executionCtx;
  } catch {
    ctx = undefined; // no ExecutionContext (tests, some local runners)
  }
  if (ctx) ctx.waitUntil(safe);
  else await safe;
}

export function errorText(err: unknown): string {
  return err instanceof Error ? `${err.name}: ${err.message}` : String(err);
}

export function clientIp(c: Ctx): string | null {
  return c.req.header("CF-Connecting-IP") ?? null;
}

export function isHttps(c: Ctx): boolean {
  return new URL(c.req.url).protocol === "https:";
}

/** The app's public origin: APP_URL when configured, else the origin the request came to. */
export function appOrigin(c: Ctx): string {
  if (c.env.APP_URL) {
    try {
      return new URL(c.env.APP_URL).origin;
    } catch {
      console.error("APP_URL is not a valid URL:", c.env.APP_URL);
    }
  }
  return new URL(c.req.url).origin;
}
