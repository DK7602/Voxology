import { afterEach, describe, expect, it, vi } from "vitest";
import { Client, makeEnv, ORIGIN, signUp } from "./helpers/workerEnv";

afterEach(() => vi.unstubAllGlobals());

const b64url = (o: object | Uint8Array) =>
  Buffer.from(o instanceof Uint8Array ? o : JSON.stringify(o)).toString("base64url");

/** An id token as a provider's token endpoint returns it (signature not checked). */
const idToken = (claims: object) => `${b64url({ alg: "RS256" })}.${b64url(claims)}.sig`;

/** Starts a sign-in; returns where it sends the browser and the flow cookie to send back. */
async function start(client: Client, provider: string, query = "") {
  const res = await client.fetch("GET", `/api/auth/oauth/${provider}/start${query}`);
  const flow = res.headers.getSetCookie().find((c) => c.startsWith("sa_oauth="));
  return { res, location: res.headers.get("Location") ?? "", flowCookie: flow ? flow.split(";")[0] : "" };
}

async function finish(client: Client, provider: string, flowCookie: string, params: Record<string, string>, post = false) {
  const saved = client.cookie;
  client.cookie = [saved, flowCookie].filter(Boolean).join("; ");
  const q = new URLSearchParams(params);
  const res = post
    ? await client.fetch("POST", `/api/auth/oauth/${provider}/callback`, {
        body: q.toString(),
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        origin: "https://appleid.apple.com",
      })
    : await client.fetch("GET", `/api/auth/oauth/${provider}/callback?${q}`);
  if (client.cookie.includes("sa_oauth=")) client.cookie = client.cookie.split("; ").filter((c) => !c.startsWith("sa_oauth=")).join("; ");
  return res;
}

const GOOGLE = { GOOGLE_CLIENT_ID: "gid.apps", GOOGLE_CLIENT_SECRET: "gsecret" };

describe("sign in with Apple / Google / Facebook", () => {
  it("lists providers and sends people back when one isn't set up", async () => {
    const { env } = makeEnv(GOOGLE);
    const client = new Client(env);
    const list = await client.call("GET", "/api/auth/oauth/providers");
    expect(list.body.providers).toEqual({ apple: false, google: true, facebook: false });
    const { res, location } = await start(client, "apple");
    expect(res.status).toBe(302);
    expect(location).toBe("/login?oauth_error=not_configured&provider=apple");
  });

  it("signs in with Google, makes the account, and needs the same browser's state", async () => {
    const { env } = makeEnv(GOOGLE);
    const client = new Client(env);
    const { location, flowCookie } = await start(client, "google", "?from=/song/abc");
    const url = new URL(location);
    expect(url.origin + url.pathname).toBe("https://accounts.google.com/o/oauth2/v2/auth");
    expect(url.searchParams.get("redirect_uri")).toBe(`${ORIGIN}/api/auth/oauth/google/callback`);
    expect(url.searchParams.get("code_challenge_method")).toBe("S256");
    const state = url.searchParams.get("state")!;

    const calls: { url: string; body: string }[] = [];
    vi.stubGlobal("fetch", async (u: string, init?: RequestInit) => {
      calls.push({ url: String(u), body: String(init?.body ?? "") });
      return Response.json({
        id_token: idToken({ iss: "https://accounts.google.com", aud: "gid.apps", sub: "g-123", email: "New.Player@gmail.com", email_verified: true, name: "New Player", exp: Date.now() / 1000 + 600 }),
      });
    });

    // a different state (another browser's sign-in) is refused
    const forged = await finish(new Client(env), "google", flowCookie, { code: "c", state: "not-the-state" });
    expect(forged.headers.get("Location")).toBe("/login?oauth_error=expired&provider=google");

    const res = await finish(client, "google", flowCookie, { code: "the-code", state });
    expect(res.status).toBe(302);
    expect(res.headers.get("Location")).toBe("/song/abc");
    expect(calls[0].url).toBe("https://oauth2.googleapis.com/token");
    expect(calls[0].body).toContain("code_verifier=");
    const me = await client.call("GET", "/api/auth/me");
    expect(me.body.user).toMatchObject({ email: "new.player@gmail.com", name: "New Player", hasPassword: false });

    // a second sign-in finds the same account
    const again = new Client(env);
    const s2 = await start(again, "google");
    await finish(again, "google", s2.flowCookie, { code: "c2", state: new URL(s2.location).searchParams.get("state")! });
    expect((await again.call("GET", "/api/auth/me")).body.user.id).toBe(me.body.user.id);

    // no password yet: one can be set without a current one; deleting needs DELETE typed
    expect((await client.call("POST", "/api/auth/password", { newPassword: "a brand new pass" })).status).toBe(200);
    expect((await client.call("GET", "/api/auth/me")).body.user.hasPassword).toBe(true);
  });

  it("links Google to an existing account with the same verified email", async () => {
    const { env } = makeEnv(GOOGLE);
    const { user } = await signUp(env, "player@example.com");
    const client = new Client(env);
    const s = await start(client, "google");
    vi.stubGlobal("fetch", async () =>
      Response.json({ id_token: idToken({ iss: "accounts.google.com", aud: "gid.apps", sub: "g-9", email: "player@example.com", email_verified: true }) }),
    );
    await finish(client, "google", s.flowCookie, { code: "c", state: new URL(s.location).searchParams.get("state")! });
    expect((await client.call("GET", "/api/auth/me")).body.user).toMatchObject({ id: user.id, hasPassword: true });
  });

  it("refuses an unverified email and a token meant for another app", async () => {
    const { env } = makeEnv(GOOGLE);
    const run = async (claims: object) => {
      const client = new Client(env);
      const s = await start(client, "google");
      vi.stubGlobal("fetch", async () => Response.json({ id_token: idToken(claims) }));
      return finish(client, "google", s.flowCookie, { code: "c", state: new URL(s.location).searchParams.get("state")! });
    };
    const unverified = await run({ iss: "accounts.google.com", aud: "gid.apps", sub: "g-1", email: "x@example.com", email_verified: false });
    expect(unverified.headers.get("Location")).toBe("/login?oauth_error=no_email&provider=google");
    const otherApp = await run({ iss: "accounts.google.com", aud: "someone-else", sub: "g-2", email: "y@example.com", email_verified: true });
    expect(otherApp.headers.get("Location")).toBe("/login?oauth_error=failed&provider=google");
  });

  it("signs in with Facebook", async () => {
    const { env } = makeEnv({ FACEBOOK_APP_ID: "fb1", FACEBOOK_APP_SECRET: "fbsecret" });
    const client = new Client(env);
    const s = await start(client, "facebook");
    expect(s.location.startsWith("https://www.facebook.com/dialog/oauth?")).toBe(true);
    vi.stubGlobal("fetch", async (u: string) => {
      const url = new URL(String(u));
      if (url.pathname === "/oauth/access_token") return Response.json({ access_token: "fb-token" });
      expect(url.searchParams.get("appsecret_proof")).toMatch(/^[0-9a-f]{64}$/);
      return Response.json({ id: "fb-77", name: "Face Player", email: "face@example.com" });
    });
    const res = await finish(client, "facebook", s.flowCookie, { code: "c", state: new URL(s.location).searchParams.get("state")! });
    expect(res.headers.get("Location")).toBe("/");
    expect((await client.call("GET", "/api/auth/me")).body.user.email).toBe("face@example.com");
  });

  it("signs in with Apple (form post from Apple's site, signed client secret)", async () => {
    const pair = (await crypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"])) as CryptoKeyPair;
    const pkcs8 = Buffer.from(await crypto.subtle.exportKey("pkcs8", pair.privateKey)).toString("base64");
    const pem = `-----BEGIN PRIVATE KEY-----\n${pkcs8.match(/.{1,64}/g)!.join("\n")}\n-----END PRIVATE KEY-----`;
    const { env } = makeEnv({ APPLE_CLIENT_ID: "com.songalong.web", APPLE_TEAM_ID: "TEAM1", APPLE_KEY_ID: "KEY1", APPLE_PRIVATE_KEY: pem });
    const client = new Client(env);
    const s = await start(client, "apple");
    expect(new URL(s.location).searchParams.get("response_mode")).toBe("form_post");
    expect(s.flowCookie).not.toBe("");

    let secretOk = false;
    vi.stubGlobal("fetch", async (_u: string, init?: RequestInit) => {
      const secret = new URLSearchParams(String(init?.body)).get("client_secret")!;
      const [h, p, sig] = secret.split(".");
      secretOk = await crypto.subtle.verify(
        { name: "ECDSA", hash: "SHA-256" },
        pair.publicKey,
        Buffer.from(sig, "base64url"),
        new TextEncoder().encode(`${h}.${p}`),
      );
      expect(JSON.parse(Buffer.from(p, "base64url").toString())).toMatchObject({ iss: "TEAM1", sub: "com.songalong.web" });
      return Response.json({ id_token: idToken({ iss: "https://appleid.apple.com", aud: "com.songalong.web", sub: "apple-1", email: "abc@privaterelay.appleid.com", email_verified: "true" }) });
    });
    const res = await finish(
      client,
      "apple",
      s.flowCookie,
      { code: "c", state: new URL(s.location).searchParams.get("state")!, user: JSON.stringify({ name: { firstName: "Ada", lastName: "Apple" } }) },
      true,
    );
    expect(res.status).toBe(302);
    expect(res.headers.get("Location")).toBe("/");
    expect(secretOk).toBe(true);
    expect((await client.call("GET", "/api/auth/me")).body.user).toMatchObject({ name: "Ada Apple", email: "abc@privaterelay.appleid.com" });

    // deleting a password-less account needs DELETE typed
    expect((await client.call("POST", "/api/auth/delete", {})).status).toBe(400);
    expect((await client.call("POST", "/api/auth/delete", { confirm: "DELETE" })).status).toBe(200);
  });
});
