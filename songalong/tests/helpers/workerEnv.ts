// Builds a Worker Env on the fakes and a cookie-keeping client that calls the Hono app directly.

import { readFileSync } from "node:fs";
import app from "../../worker/index";
import type { Env } from "../../worker/env";
import { FakeD1 } from "./fakeD1";
import { FakeR2 } from "./fakeR2";

export const ORIGIN = "https://songalong.test";

export interface SentMail {
  from: string | { email: string; name?: string };
  to: string;
  subject: string;
  html: string;
  text: string;
}

const MIGRATIONS = ["0001_init.sql", "0002_youtube.sql", "0003_oauth.sql", "0004_engine.sql", "0005_hidden.sql", "0006_library_order.sql"].map((f) =>
  readFileSync(new URL(`../../migrations/${f}`, import.meta.url), "utf8"),
);

export function makeEnv(overrides: Partial<Record<keyof Env, unknown>> = {}) {
  const db = new FakeD1();
  for (const m of MIGRATIONS) db.sqlite.exec(m);
  const r2 = new FakeR2();
  const mail: SentMail[] = [];
  const email = {
    async send(message: SentMail) {
      mail.push(message);
      return { messageId: `msg-${mail.length}` };
    },
  };
  const env = {
    DB: db,
    AUDIO: r2,
    EMAIL: email,
    APP_NAME: "SongAlong",
    MAIL_FROM: "SongAlong <no-reply@example.com>",
    ...overrides,
  } as unknown as Env;
  return { env, db, r2, mail };
}

export interface RequestOptions {
  json?: unknown;
  body?: BodyInit;
  headers?: Record<string, string>;
  /** Origin header to send on writes; null sends none. Defaults to the app's origin (as a browser would). */
  origin?: string | null;
  /** Base URL, default ORIGIN */
  base?: string;
}

export class Client {
  cookie = "";
  lastSetCookie: string | null = null;

  constructor(private readonly env: Env) {}

  async fetch(method: string, path: string, opts: RequestOptions = {}): Promise<Response> {
    const base = opts.base ?? ORIGIN;
    const headers: Record<string, string> = { ...opts.headers };
    const origin = opts.origin === undefined ? base : opts.origin;
    if (method !== "GET" && method !== "HEAD" && origin !== null) headers.Origin = origin;
    if (this.cookie) headers.Cookie = this.cookie;
    let body = opts.body;
    if (opts.json !== undefined) {
      body = JSON.stringify(opts.json);
      headers["Content-Type"] ??= "application/json";
    }
    const res = await app.request(`${base}${path}`, { method, headers, body }, this.env);
    this.lastSetCookie = res.headers.get("Set-Cookie");
    const m = this.lastSetCookie ? /(?:^|,\s*)sa_session=([^;]*)/.exec(this.lastSetCookie) : null;
    if (m) this.cookie = m[1] ? `sa_session=${m[1]}` : "";
    return res;
  }

  /** Calls the API and parses the JSON reply. */
  async call<T = any>(method: string, path: string, json?: unknown, opts: RequestOptions = {}) {
    const res = await this.fetch(method, path, json === undefined ? opts : { ...opts, json });
    const text = await res.text();
    const body = (text ? JSON.parse(text) : null) as T;
    return { status: res.status, body, res };
  }
}

/** Registers a new user on a fresh client. */
export async function signUp(env: Env, email: string, password = "correct horse battery", name = "Test User") {
  const client = new Client(env);
  const r = await client.call("POST", "/api/auth/register", { email, password, name });
  if (r.status !== 200) throw new Error(`register failed: ${r.status} ${JSON.stringify(r.body)}`);
  return { client, user: r.body.user as { id: string; email: string; name: string } };
}
