// /api/auth/*: accounts, sign-in, password reset.

import { Hono } from "hono";
import { PASSWORD_MAX, PASSWORD_MIN, type User } from "../shared/api";
import type { UserSettings } from "../shared/types";
import { validateUserSettings } from "../shared/validate";
import { deleteAllAudio } from "./audio";
import { hashPassword, randomId, randomToken, sha256Hex, verifyPassword } from "./crypto";
import { sendPasswordReset } from "./email";
import type { AppEnv, Env, UserRow } from "./env";
import { appOrigin, clientIp, errorText, HttpError, readJsonObject, runInBackground, type Ctx } from "./http";
import { deleteStale, exceeded, hit, type Limit } from "./rateLimit";
import { clearSessionCookie, currentUser, endSession, startSession } from "./session";

const MINUTE_MS = 60_000;
const LOGIN_WINDOW_MS = 15 * MINUTE_MS;
const RESET_WINDOW_MS = 60 * MINUTE_MS;
const RESET_TOKEN_TTL_MS = 60 * MINUTE_MS;
const TOKEN_SHAPE = /^[A-Za-z0-9_-]{43}$/;

export const MESSAGES = {
  emailTaken: "An account with that email already exists",
  badLogin: "Email or password is incorrect",
  tooManyLogins: "Too many failed sign-in attempts. Please wait 15 minutes and try again.",
  badReset: "This reset link is invalid or has expired",
  wrongPassword: "Your current password is incorrect",
} as const;

const USER_COLUMNS = "id, email, name, password_hash, settings, created_at, password_changed_at";

function parseSettings(json: string): UserSettings {
  try {
    const v: unknown = JSON.parse(json);
    return typeof v === "object" && v !== null && !Array.isArray(v) ? (v as UserSettings) : {};
  } catch {
    return {};
  }
}

export function toUser(row: UserRow): User {
  return { id: row.id, email: row.email, name: row.name, settings: parseSettings(row.settings), createdAt: row.created_at };
}

function normaliseEmail(v: unknown): string {
  if (typeof v !== "string" || !v.trim()) throw new HttpError(400, "Enter your email address");
  const email = v.trim().toLowerCase();
  if (email.length > 254 || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) {
    throw new HttpError(400, "Enter a valid email address");
  }
  return email;
}

function checkNewPassword(v: unknown, label = "Password"): string {
  if (typeof v !== "string" || v.length < PASSWORD_MIN) {
    throw new HttpError(400, `${label} must be at least ${PASSWORD_MIN} characters`);
  }
  if (v.length > PASSWORD_MAX) throw new HttpError(400, `${label} must be at most ${PASSWORD_MAX} characters`);
  return v;
}

function checkName(v: unknown): string {
  if (v === undefined || v === null) return "";
  if (typeof v !== "string") throw new HttpError(400, "Name must be text");
  const name = v.replace(/[\u0000-\u001f\u007f]+/g, " ").trim();
  if (name.length > 80) throw new HttpError(400, "Name must be 80 characters or fewer");
  return name;
}

function loginLimits(email: string, ip: string | null): Limit[] {
  const limits: Limit[] = [{ key: `login:email:${email}`, max: 10, windowMs: LOGIN_WINDOW_MS }];
  if (ip) limits.push({ key: `login:ip:${ip}`, max: 50, windowMs: LOGIN_WINDOW_MS });
  return limits;
}

function tooManyAttempts(): HttpError {
  return new HttpError(429, MESSAGES.tooManyLogins, { "Retry-After": String(LOGIN_WINDOW_MS / 1000) });
}

/** Re-checks the signed-in user's password; failures count towards the sign-in limit. */
async function confirmPassword(c: Ctx, user: UserRow, password: unknown, message: string): Promise<void> {
  const db = c.env.DB;
  const limits = loginLimits(user.email, clientIp(c));
  if (await exceeded(db, limits)) throw tooManyAttempts();
  const ok = typeof password === "string" && password.length <= PASSWORD_MAX && (await verifyPassword(password, user.password_hash));
  if (!ok) {
    await hit(db, limits);
    throw new HttpError(400, message);
  }
}

function cleanupExpired(db: D1Database): Promise<unknown> {
  const now = Date.now();
  return db.batch([
    db.prepare("DELETE FROM sessions WHERE expires_at <= ?").bind(now),
    db.prepare("DELETE FROM password_resets WHERE expires_at <= ?").bind(now),
    deleteStale(db, now),
  ]);
}

async function createResetAndSend(env: Env, email: string, origin: string): Promise<void> {
  const user = await env.DB.prepare("SELECT id, name FROM users WHERE email = ?")
    .bind(email)
    .first<{ id: string; name: string }>();
  if (!user) return;
  const token = randomToken();
  const now = Date.now();
  await env.DB.prepare("INSERT INTO password_resets (token_hash, user_id, created_at, expires_at) VALUES (?, ?, ?, ?)")
    .bind(await sha256Hex(token), user.id, now, now + RESET_TOKEN_TTL_MS)
    .run();
  await sendPasswordReset(env, email, user.name, `${origin}/reset-password?token=${token}`);
}

export const auth = new Hono<AppEnv>();

auth.post("/register", async (c) => {
  const body = await readJsonObject(c);
  const email = normaliseEmail(body.email);
  const password = checkNewPassword(body.password);
  const name = checkName(body.name);
  const db = c.env.DB;
  if (await db.prepare("SELECT 1 AS found FROM users WHERE email = ?").bind(email).first()) {
    throw new HttpError(409, MESSAGES.emailTaken);
  }
  const now = Date.now();
  const row: UserRow = {
    id: randomId(),
    email,
    name,
    password_hash: await hashPassword(password),
    settings: "{}",
    created_at: now,
    password_changed_at: now,
  };
  try {
    await db
      .prepare(`INSERT INTO users (${USER_COLUMNS}) VALUES (?, ?, ?, ?, ?, ?, ?)`)
      .bind(row.id, row.email, row.name, row.password_hash, row.settings, row.created_at, row.password_changed_at)
      .run();
  } catch (err) {
    if (/UNIQUE constraint failed/i.test(errorText(err))) throw new HttpError(409, MESSAGES.emailTaken);
    throw err;
  }
  await startSession(c, row.id, body.remember !== false);
  return c.json({ user: toUser(row) });
});

auth.post("/login", async (c) => {
  const body = await readJsonObject(c);
  const email = typeof body.email === "string" ? body.email.trim().toLowerCase() : "";
  const password = typeof body.password === "string" ? body.password : "";
  if (!email || !password) throw new HttpError(400, "Enter your email and password");
  const db = c.env.DB;
  const limits = loginLimits(email, clientIp(c));
  if (await exceeded(db, limits)) throw tooManyAttempts();

  const row = await db.prepare(`SELECT ${USER_COLUMNS} FROM users WHERE email = ?`).bind(email).first<UserRow>();
  // Always run PBKDF2 (against a dummy hash for unknown emails) so timing doesn't reveal accounts.
  const ok = password.length <= PASSWORD_MAX && (await verifyPassword(password, row?.password_hash ?? null));
  if (!row || !ok) {
    await hit(db, limits);
    throw new HttpError(401, MESSAGES.badLogin);
  }
  await runInBackground(c, cleanupExpired(db), "Expired session cleanup");
  await startSession(c, row.id, body.remember === true);
  return c.json({ user: toUser(row) });
});

auth.post("/logout", async (c) => {
  await endSession(c);
  return c.json({ ok: true });
});

auth.get("/me", (c) => c.json({ user: toUser(currentUser(c)) }));

auth.patch("/me", async (c) => {
  const user = currentUser(c);
  const body = await readJsonObject(c);
  const name = body.name === undefined ? user.name : checkName(body.name);
  const settings =
    body.settings === undefined ? parseSettings(user.settings) : validateUserSettings(body.settings, parseSettings(user.settings));
  const updated: UserRow = { ...user, name, settings: JSON.stringify(settings) };
  await c.env.DB.prepare("UPDATE users SET name = ?, settings = ? WHERE id = ?")
    .bind(updated.name, updated.settings, user.id)
    .run();
  return c.json({ user: toUser(updated) });
});

auth.post("/forgot", async (c) => {
  const body = await readJsonObject(c);
  const email = normaliseEmail(body.email);
  const db = c.env.DB;
  const ip = clientIp(c);
  const limits: Limit[] = [{ key: `forgot:email:${email}`, max: 3, windowMs: RESET_WINDOW_MS }];
  if (ip) limits.push({ key: `forgot:ip:${ip}`, max: 20, windowMs: RESET_WINDOW_MS });
  const limited = await exceeded(db, limits);
  await hit(db, limits);
  if (limited) {
    console.warn("Password reset request rate-limited");
  } else {
    // The lookup and the mail run after the response so its timing doesn't reveal accounts.
    await runInBackground(c, createResetAndSend(c.env, email, appOrigin(c)), "Password reset email");
  }
  return c.json({ ok: true });
});

auth.post("/reset", async (c) => {
  const body = await readJsonObject(c);
  const token = typeof body.token === "string" ? body.token.trim() : "";
  const password = checkNewPassword(body.password);
  if (!TOKEN_SHAPE.test(token)) throw new HttpError(400, MESSAGES.badReset);
  const db = c.env.DB;
  const now = Date.now();
  // Claiming the token in one statement makes it single-use even under concurrent requests.
  const claimed = await db
    .prepare(
      `UPDATE password_resets SET used_at = ?1
       WHERE token_hash = ?2 AND used_at IS NULL AND expires_at > ?1 RETURNING user_id`,
    )
    .bind(now, await sha256Hex(token))
    .first<{ user_id: string }>();
  if (!claimed) throw new HttpError(400, MESSAGES.badReset);

  const userId = claimed.user_id;
  await db.batch([
    db.prepare("UPDATE users SET password_hash = ?, password_changed_at = ? WHERE id = ?").bind(await hashPassword(password), now, userId),
    db.prepare("DELETE FROM sessions WHERE user_id = ?").bind(userId),
    db.prepare("UPDATE password_resets SET used_at = ? WHERE user_id = ? AND used_at IS NULL").bind(now, userId),
  ]);
  const user = await db.prepare(`SELECT ${USER_COLUMNS} FROM users WHERE id = ?`).bind(userId).first<UserRow>();
  if (!user) throw new HttpError(400, MESSAGES.badReset);
  await db.prepare("DELETE FROM rate_limits WHERE key = ?").bind(`login:email:${user.email}`).run();
  await startSession(c, user.id, body.remember !== false);
  return c.json({ user: toUser(user) });
});

auth.post("/password", async (c) => {
  const user = currentUser(c);
  const body = await readJsonObject(c);
  const newPassword = checkNewPassword(body.newPassword, "New password");
  await confirmPassword(c, user, body.currentPassword, MESSAGES.wrongPassword);
  const db = c.env.DB;
  const now = Date.now();
  await db.batch([
    db.prepare("UPDATE users SET password_hash = ?, password_changed_at = ? WHERE id = ?").bind(await hashPassword(newPassword), now, user.id),
    db.prepare("DELETE FROM sessions WHERE user_id = ? AND token_hash != ?").bind(user.id, c.get("sessionHash") ?? ""),
    db.prepare("UPDATE password_resets SET used_at = ? WHERE user_id = ? AND used_at IS NULL").bind(now, user.id),
  ]);
  return c.json({ ok: true });
});

auth.post("/delete", async (c) => {
  const user = currentUser(c);
  const body = await readJsonObject(c);
  await confirmPassword(c, user, body.password, "Password is incorrect");
  await deleteAllAudio(c.env, user.id);
  const db = c.env.DB;
  // Other users' favorites / playlists / progress may point at this user's songs (no foreign key,
  // because they can also hold demo ids), so clear those before the cascade removes the songs.
  const ownSongs = "SELECT id FROM songs WHERE owner_id = ?";
  await db.batch([
    db.prepare(`DELETE FROM favorites WHERE song_id IN (${ownSongs})`).bind(user.id),
    db.prepare(`DELETE FROM playlist_songs WHERE song_id IN (${ownSongs})`).bind(user.id),
    db.prepare(`DELETE FROM song_progress WHERE song_id IN (${ownSongs})`).bind(user.id),
    db.prepare("DELETE FROM users WHERE id = ?").bind(user.id),
  ]);
  clearSessionCookie(c);
  return c.json({ ok: true });
});
