// Cookie sessions. The cookie holds a random token; the database only stores its SHA-256.

import type { MiddlewareHandler } from "hono";
import { deleteCookie, getCookie, setCookie } from "hono/cookie";
import { randomToken, sha256Hex } from "./crypto";
import type { AppEnv, UserRow } from "./env";
import { HttpError, isHttps, type Ctx } from "./http";

export const SESSION_COOKIE = "sa_session";
const DAY_MS = 24 * 60 * 60 * 1000;
const REMEMBER_DAYS = 30;
const TOKEN_SHAPE = /^[A-Za-z0-9_-]{43}$/;

const USER_COLUMNS = "u.id, u.email, u.name, u.password_hash, u.settings, u.created_at, u.password_changed_at";

/** Loads the signed-in user (if any) into c.var.user. */
export const loadSession: MiddlewareHandler<AppEnv> = async (c, next) => {
  c.set("user", null);
  c.set("sessionHash", null);
  const token = getCookie(c, SESSION_COOKIE);
  if (token && TOKEN_SHAPE.test(token)) {
    const hash = await sha256Hex(token);
    const user = await c.env.DB.prepare(
      `SELECT ${USER_COLUMNS} FROM sessions s JOIN users u ON u.id = s.user_id
       WHERE s.token_hash = ? AND s.expires_at > ?`,
    )
      .bind(hash, Date.now())
      .first<UserRow>();
    if (user) {
      c.set("user", user);
      c.set("sessionHash", hash);
    }
  }
  await next();
};

export const requireAuth: MiddlewareHandler<AppEnv> = async (c, next) => {
  if (!c.get("user")) throw new HttpError(401, "Not signed in");
  await next();
};

/** The signed-in user; throws 401 when there is none. */
export function currentUser(c: Ctx): UserRow {
  const user = c.get("user");
  if (!user) throw new HttpError(401, "Not signed in");
  return user;
}

/** Creates a session and sets the cookie. `remember` makes the cookie outlive the browser
 *  session (30 days); otherwise it is a session cookie backed by a 1-day database expiry. */
export async function startSession(c: Ctx, userId: string, remember: boolean): Promise<void> {
  const token = randomToken();
  const hash = await sha256Hex(token);
  const now = Date.now();
  const expires = now + (remember ? REMEMBER_DAYS : 1) * DAY_MS;
  const userAgent = (c.req.header("User-Agent") ?? "").slice(0, 200);
  await c.env.DB.prepare(
    "INSERT INTO sessions (token_hash, user_id, created_at, expires_at, user_agent) VALUES (?, ?, ?, ?, ?)",
  )
    .bind(hash, userId, now, expires, userAgent)
    .run();
  setCookie(c, SESSION_COOKIE, token, {
    httpOnly: true,
    // Browsers drop Secure cookies on plain-http localhost, so only set it over https.
    secure: isHttps(c),
    sameSite: "Lax",
    path: "/",
    maxAge: remember ? REMEMBER_DAYS * 24 * 60 * 60 : undefined,
  });
  c.set("sessionHash", hash);
}

export function clearSessionCookie(c: Ctx): void {
  deleteCookie(c, SESSION_COOKIE, { path: "/", httpOnly: true, secure: isHttps(c), sameSite: "Lax" });
}

/** Ends the current session (if any) and clears the cookie. */
export async function endSession(c: Ctx): Promise<void> {
  const hash = c.get("sessionHash");
  if (hash) await c.env.DB.prepare("DELETE FROM sessions WHERE token_hash = ?").bind(hash).run();
  clearSessionCookie(c);
}
