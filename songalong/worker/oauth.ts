// /api/auth/oauth/*: Sign in with Apple, Google or Facebook (OAuth 2.0 authorization code flow).
//
// A provider is switched on by its secrets:
//   Google    GOOGLE_CLIENT_ID, GOOGLE_CLIENT_SECRET
//   Facebook  FACEBOOK_APP_ID, FACEBOOK_APP_SECRET
//   Apple     APPLE_CLIENT_ID (the Services ID), APPLE_TEAM_ID, APPLE_KEY_ID, APPLE_PRIVATE_KEY (the .p8 key)
// Each provider's console needs the redirect URL  <app>/api/auth/oauth/<provider>/callback.
//
// The browser goes to /start, which remembers a random `state` (and Google's PKCE verifier) in a
// short-lived cookie and sends it to the provider. The provider sends it back to /callback with a
// code; the code is exchanged server to server for the person's id and email, which sign them in
// to the account linked to that id, else the account with that (verified) email, else a new one.

import { Hono } from "hono";
import { deleteCookie, getCookie, setCookie } from "hono/cookie";
import { base64url, randomBytes, randomId, sha256Hex } from "./crypto";
import type { AppEnv, Env, UserRow } from "./env";
import { appOrigin, isHttps, type Ctx } from "./http";
import { startSession } from "./session";

export const PROVIDERS = ["apple", "google", "facebook"] as const;
export type Provider = (typeof PROVIDERS)[number];

const STATE_COOKIE = "sa_oauth";
const STATE_TTL_S = 600;

export interface Identity {
  subject: string;
  email: string;
  emailVerified: boolean;
  name: string;
}

export function isProvider(p: string): p is Provider {
  return (PROVIDERS as readonly string[]).includes(p);
}

export function configured(env: Env, p: Provider): boolean {
  if (p === "google") return !!(env.GOOGLE_CLIENT_ID && env.GOOGLE_CLIENT_SECRET);
  if (p === "facebook") return !!(env.FACEBOOK_APP_ID && env.FACEBOOK_APP_SECRET);
  return !!(env.APPLE_CLIENT_ID && env.APPLE_TEAM_ID && env.APPLE_KEY_ID && env.APPLE_PRIVATE_KEY);
}

interface FlowState {
  state: string;
  provider: Provider;
  verifier: string;
  remember: boolean;
  from: string;
}

/** Only same-site paths, so the sign-in can't bounce people to another site. */
function safeReturn(v: string | undefined): string {
  return v && v.startsWith("/") && !v.startsWith("//") && !v.startsWith("/\\") && v.length < 500 ? v : "/";
}

function redirectUri(c: Ctx, p: Provider): string {
  return `${appOrigin(c)}/api/auth/oauth/${p}/callback`;
}

function backToLogin(c: Ctx, error: string, p?: Provider) {
  const q = new URLSearchParams({ oauth_error: error });
  if (p) q.set("provider", p);
  return c.redirect(`/login?${q}`, 302);
}

/** The payload of a JWT. Only for tokens received straight from the provider's token endpoint
 *  over TLS, which is what Google and Apple document as trusted without checking the signature. */
export function jwtPayload(token: string): Record<string, unknown> {
  const part = token.split(".")[1] ?? "";
  const json = atob(part.replace(/-/g, "+").replace(/_/g, "/").padEnd(Math.ceil(part.length / 4) * 4, "="));
  const v: unknown = JSON.parse(new TextDecoder().decode(Uint8Array.from(json, (ch) => ch.charCodeAt(0))));
  if (typeof v !== "object" || v === null) throw new Error("Bad token");
  return v as Record<string, unknown>;
}

function checkIdToken(claims: Record<string, unknown>, issuers: string[], audience: string) {
  const aud = claims.aud;
  const okAud = aud === audience || (Array.isArray(aud) && aud.includes(audience));
  if (!okAud || !issuers.includes(String(claims.iss)) || typeof claims.sub !== "string" || !claims.sub) throw new Error("Unexpected sign-in token");
  if (typeof claims.exp === "number" && claims.exp * 1000 < Date.now() - 300_000) throw new Error("Expired sign-in token");
}

const truthy = (v: unknown) => v === true || v === "true";

// ---- Apple's client secret: a short JWT signed (ES256) with the .p8 key ----

async function appleClientSecret(env: Env): Promise<string> {
  const pem = env.APPLE_PRIVATE_KEY!.replace(/\\n/g, "\n");
  const der = Uint8Array.from(atob(pem.replace(/-----[^-]+-----/g, "").replace(/\s+/g, "")), (ch) => ch.charCodeAt(0));
  const key = await crypto.subtle.importKey("pkcs8", der, { name: "ECDSA", namedCurve: "P-256" }, false, ["sign"]);
  const enc = (o: object) => base64url(new TextEncoder().encode(JSON.stringify(o)));
  const now = Math.floor(Date.now() / 1000);
  const head = enc({ alg: "ES256", kid: env.APPLE_KEY_ID });
  const body = enc({ iss: env.APPLE_TEAM_ID, iat: now, exp: now + 300, aud: "https://appleid.apple.com", sub: env.APPLE_CLIENT_ID });
  const sig = await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, key, new TextEncoder().encode(`${head}.${body}`));
  return `${head}.${body}.${base64url(new Uint8Array(sig))}`;
}

// ---- per provider: where to send people, and how to turn the code into an identity ----

async function authorizeUrl(c: Ctx, p: Provider, state: string, verifier: string): Promise<string> {
  const env = c.env;
  const redirect = redirectUri(c, p);
  if (p === "google") {
    const challenge = base64url(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier))));
    const q = new URLSearchParams({
      client_id: env.GOOGLE_CLIENT_ID!,
      redirect_uri: redirect,
      response_type: "code",
      scope: "openid email profile",
      state,
      code_challenge: challenge,
      code_challenge_method: "S256",
      prompt: "select_account",
    });
    return `https://accounts.google.com/o/oauth2/v2/auth?${q}`;
  }
  if (p === "facebook") {
    const q = new URLSearchParams({ client_id: env.FACEBOOK_APP_ID!, redirect_uri: redirect, response_type: "code", scope: "email,public_profile", state });
    return `https://www.facebook.com/dialog/oauth?${q}`;
  }
  const q = new URLSearchParams({
    client_id: env.APPLE_CLIENT_ID!,
    redirect_uri: redirect,
    response_type: "code",
    response_mode: "form_post", // Apple only posts the name / email scopes back
    scope: "name email",
    state,
  });
  return `https://appleid.apple.com/auth/authorize?${q}`;
}

async function postForm(url: string, form: Record<string, string>, fetcher: typeof fetch): Promise<Record<string, unknown>> {
  const res = await fetcher(url, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded", Accept: "application/json" },
    body: new URLSearchParams(form),
  });
  const data = (await res.json().catch(() => ({}))) as Record<string, unknown>;
  if (!res.ok) throw new Error(`Token exchange failed (${res.status}): ${String(data.error ?? "")}`);
  return data;
}

export async function identify(
  c: Ctx,
  p: Provider,
  code: string,
  flow: FlowState,
  appleUser: string | undefined,
  fetcher: typeof fetch = fetch,
): Promise<Identity> {
  const env = c.env;
  const redirect = redirectUri(c, p);
  if (p === "google") {
    const tokens = await postForm(
      "https://oauth2.googleapis.com/token",
      {
        code,
        client_id: env.GOOGLE_CLIENT_ID!,
        client_secret: env.GOOGLE_CLIENT_SECRET!,
        redirect_uri: redirect,
        grant_type: "authorization_code",
        code_verifier: flow.verifier,
      },
      fetcher,
    );
    const claims = jwtPayload(String(tokens.id_token ?? ""));
    checkIdToken(claims, ["https://accounts.google.com", "accounts.google.com"], env.GOOGLE_CLIENT_ID!);
    return { subject: String(claims.sub), email: String(claims.email ?? ""), emailVerified: truthy(claims.email_verified), name: String(claims.name ?? "") };
  }
  if (p === "facebook") {
    const q = new URLSearchParams({ client_id: env.FACEBOOK_APP_ID!, client_secret: env.FACEBOOK_APP_SECRET!, redirect_uri: redirect, code });
    const res = await fetcher(`https://graph.facebook.com/oauth/access_token?${q}`);
    const tokens = (await res.json().catch(() => ({}))) as { access_token?: string };
    if (!res.ok || !tokens.access_token) throw new Error(`Facebook token exchange failed (${res.status})`);
    // appsecret_proof: proves the call comes from this app's server
    const key = await crypto.subtle.importKey("raw", new TextEncoder().encode(env.FACEBOOK_APP_SECRET!), { name: "HMAC", hash: "SHA-256" }, false, ["sign"]);
    const mac = new Uint8Array(await crypto.subtle.sign("HMAC", key, new TextEncoder().encode(tokens.access_token)));
    const proof = [...mac].map((b) => b.toString(16).padStart(2, "0")).join("");
    const me = await fetcher(
      `https://graph.facebook.com/me?${new URLSearchParams({ fields: "id,name,email", access_token: tokens.access_token, appsecret_proof: proof })}`,
    );
    const who = (await me.json().catch(() => ({}))) as { id?: string; name?: string; email?: string };
    if (!me.ok || !who.id) throw new Error(`Facebook profile lookup failed (${me.status})`);
    // Facebook only shares an email address the person has confirmed with them.
    return { subject: who.id, email: who.email ?? "", emailVerified: !!who.email, name: who.name ?? "" };
  }
  const tokens = await postForm(
    "https://appleid.apple.com/auth/token",
    { client_id: env.APPLE_CLIENT_ID!, client_secret: await appleClientSecret(env), code, grant_type: "authorization_code", redirect_uri: redirect },
    fetcher,
  );
  const claims = jwtPayload(String(tokens.id_token ?? ""));
  checkIdToken(claims, ["https://appleid.apple.com"], env.APPLE_CLIENT_ID!);
  // Apple sends the name once, on the first sign-in, as a form field.
  let name = "";
  try {
    const u = JSON.parse(appleUser ?? "{}") as { name?: { firstName?: string; lastName?: string } };
    name = [u.name?.firstName, u.name?.lastName].filter(Boolean).join(" ");
  } catch {
    /* no name */
  }
  return { subject: String(claims.sub), email: String(claims.email ?? ""), emailVerified: truthy(claims.email_verified), name };
}

const USER_COLUMNS = "id, email, name, password_hash, settings, created_at, password_changed_at";

/** The user for an outside identity: already linked, else the same verified email, else new. */
export async function userFor(db: D1Database, p: Provider, who: Identity): Promise<UserRow | null> {
  const linked = await db
    .prepare(`SELECT ${USER_COLUMNS.replace(/(\w+)/g, "u.$1")} FROM oauth_accounts o JOIN users u ON u.id = o.user_id WHERE o.provider = ? AND o.subject = ?`)
    .bind(p, who.subject)
    .first<UserRow>();
  if (linked) return linked;
  const email = who.email.trim().toLowerCase();
  if (!email || !who.emailVerified) return null; // can't create an account without a confirmed email
  const now = Date.now();
  let user = await db.prepare(`SELECT ${USER_COLUMNS} FROM users WHERE email = ?`).bind(email).first<UserRow>();
  if (!user) {
    user = {
      id: randomId(),
      email,
      name: who.name.replace(/[\u0000-\u001f\u007f]+/g, " ").trim().slice(0, 80),
      password_hash: "!", // no password yet; one can be set from Settings
      settings: "{}",
      created_at: now,
      password_changed_at: now,
    };
    await db
      .prepare(`INSERT INTO users (${USER_COLUMNS}) VALUES (?, ?, ?, ?, ?, ?, ?)`)
      .bind(user.id, user.email, user.name, user.password_hash, user.settings, user.created_at, user.password_changed_at)
      .run();
  }
  await db
    .prepare("INSERT OR IGNORE INTO oauth_accounts (provider, subject, user_id, email, created_at) VALUES (?, ?, ?, ?, ?)")
    .bind(p, who.subject, user.id, email, now)
    .run();
  return user;
}

function stateCookieOptions(c: Ctx) {
  const https = isHttps(c);
  // Apple posts the callback from its own site, so the cookie must be sent cross-site (needs https).
  return { httpOnly: true, secure: https, sameSite: https ? ("None" as const) : ("Lax" as const), path: "/api/auth/oauth" };
}

export const oauth = new Hono<AppEnv>();

oauth.get("/providers", (c) => c.json({ providers: Object.fromEntries(PROVIDERS.map((p) => [p, configured(c.env, p)])) }));

oauth.get("/:provider/start", async (c) => {
  const p = c.req.param("provider");
  if (!isProvider(p)) return backToLogin(c, "unknown");
  if (!configured(c.env, p)) return backToLogin(c, "not_configured", p);
  const flow: FlowState = {
    state: base64url(randomBytes(24)),
    provider: p,
    verifier: base64url(randomBytes(32)),
    remember: c.req.query("remember") !== "0",
    from: safeReturn(c.req.query("from")),
  };
  setCookie(c, STATE_COOKIE, base64url(new TextEncoder().encode(JSON.stringify(flow))), { ...stateCookieOptions(c), maxAge: STATE_TTL_S });
  return c.redirect(await authorizeUrl(c, p, flow.state, flow.verifier), 302);
});

async function callback(c: Ctx, params: Record<string, string | undefined>) {
  const p = c.req.param("provider") ?? "";
  if (!isProvider(p)) return backToLogin(c, "unknown");
  const raw = getCookie(c, STATE_COOKIE);
  deleteCookie(c, STATE_COOKIE, stateCookieOptions(c));
  let flow: FlowState | null = null;
  try {
    const json = atob((raw ?? "").replace(/-/g, "+").replace(/_/g, "/"));
    flow = JSON.parse(new TextDecoder().decode(Uint8Array.from(json, (ch) => ch.charCodeAt(0)))) as FlowState;
  } catch {
    flow = null;
  }
  if (params.error) return backToLogin(c, params.error === "access_denied" || params.error === "user_cancelled_authorize" ? "cancelled" : "failed", p);
  const stateOk = !!flow && !!params.state && flow.provider === p && (await sha256Hex(flow.state)) === (await sha256Hex(params.state));
  if (!flow || !stateOk || !params.code) return backToLogin(c, "expired", p);
  if (!configured(c.env, p)) return backToLogin(c, "not_configured", p);
  try {
    const who = await identify(c, p, params.code, flow, params.user);
    const user = await userFor(c.env.DB, p, who);
    if (!user) return backToLogin(c, "no_email", p);
    await startSession(c, user.id, flow.remember);
    return c.redirect(safeReturn(flow.from), 302);
  } catch (err) {
    console.error(`Sign in with ${p} failed:`, err);
    return backToLogin(c, "failed", p);
  }
}

oauth.get("/:provider/callback", (c) => callback(c, c.req.query()));
oauth.post("/:provider/callback", async (c) => {
  const form = await c.req.parseBody();
  const s = (k: string) => (typeof form[k] === "string" ? (form[k] as string) : undefined);
  return callback(c, { code: s("code"), state: s("state"), error: s("error"), user: s("user") });
});
