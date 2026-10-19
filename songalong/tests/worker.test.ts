import { describe, expect, it } from "vitest";
import { validateSongDoc } from "../shared/validate";
import { emptySong, type SongDoc } from "../shared/types";
import { Client, makeEnv, signUp } from "./helpers/workerEnv";

function song(title: string, chords = ["G", "D", "Em", "C"]): SongDoc {
  return {
    ...emptySong(),
    title,
    artist: "Tester",
    key: "G",
    chords: chords.map((name, i) => ({ beat: i * 4, name })),
    lines: [{ beat: 0, text: "a line of words", section: "Verse" }],
    length: chords.length * 4,
    lead: [{ beat: 0, name: "note:G4" }, { beat: 1, name: "N.C." }],
  };
}

describe("auth", () => {
  it("registers, keeps the session, signs out and back in", async () => {
    const { env } = makeEnv();
    const { client, user } = await signUp(env, "Player@Example.com");
    expect(user.email).toBe("player@example.com");
    expect(client.lastSetCookie).toMatch(/HttpOnly/);
    expect(client.lastSetCookie).toMatch(/SameSite=Lax/);
    expect((await client.call("GET", "/api/auth/me")).body.user.id).toBe(user.id);
    await client.call("POST", "/api/auth/logout");
    expect((await client.call("GET", "/api/auth/me")).status).toBe(401);
    const bad = await client.call("POST", "/api/auth/login", { email: "player@example.com", password: "nope nope nope" });
    expect(bad.status).toBe(401);
    expect(bad.body.error).toBe("Email or password is incorrect");
    const ok = await client.call("POST", "/api/auth/login", { email: "player@example.com", password: "correct horse battery" });
    expect(ok.status).toBe(200);
  });

  it("rejects duplicate emails and short passwords", async () => {
    const { env } = makeEnv();
    await signUp(env, "a@example.com");
    const c = new Client(env);
    expect((await c.call("POST", "/api/auth/register", { email: "A@example.com", password: "long enough pw" })).status).toBe(409);
    expect((await c.call("POST", "/api/auth/register", { email: "b@example.com", password: "short" })).status).toBe(400);
  });

  it("rate-limits repeated failed sign-ins", async () => {
    const { env } = makeEnv();
    await signUp(env, "r@example.com");
    const c = new Client(env);
    let last = 0;
    for (let i = 0; i < 12; i++) last = (await c.call("POST", "/api/auth/login", { email: "r@example.com", password: "wrong password!" })).status;
    expect(last).toBe(429);
  }, 30_000);

  it("resets a password by email link, once, and ends old sessions", async () => {
    const { env, mail } = makeEnv();
    const { client: old } = await signUp(env, "forgot@example.com");
    const anon = new Client(env);
    expect((await anon.call("POST", "/api/auth/forgot", { email: "forgot@example.com" })).body).toEqual({ ok: true });
    expect((await anon.call("POST", "/api/auth/forgot", { email: "nobody@example.com" })).body).toEqual({ ok: true });
    await new Promise((r) => setTimeout(r, 50)); // the mail is sent after the response
    expect(mail).toHaveLength(1);
    expect(mail[0].to).toBe("forgot@example.com");
    const token = /token=([A-Za-z0-9_-]+)/.exec(mail[0].text)![1];
    const reset = await anon.call("POST", "/api/auth/reset", { token, password: "brand new password" });
    expect(reset.status).toBe(200);
    expect((await old.call("GET", "/api/auth/me")).status).toBe(401);
    expect((await anon.call("POST", "/api/auth/reset", { token, password: "another password" })).status).toBe(400);
    const c = new Client(env);
    expect((await c.call("POST", "/api/auth/login", { email: "forgot@example.com", password: "brand new password" })).status).toBe(200);
  });

  it("blocks cross-site writes", async () => {
    const { env } = makeEnv();
    const { client } = await signUp(env, "csrf@example.com");
    const r = await client.call("POST", "/api/songs", { doc: song("X") }, { origin: "https://evil.example" });
    expect(r.status).toBe(403);
  });
});

describe("songs, library and playlists", () => {
  it("keeps songs private to their owner unless public", async () => {
    const { env } = makeEnv();
    const { client: a } = await signUp(env, "a@example.com");
    const { client: b } = await signUp(env, "b@example.com");
    const created = await a.call("POST", "/api/songs", { doc: song("Private one") });
    expect(created.status).toBe(200);
    const id = created.body.song.id;
    expect(created.body.song.doc.lead).toHaveLength(2);
    expect((await b.call("GET", `/api/songs/${id}`)).status).toBe(404);
    expect((await b.call("PUT", `/api/songs/${id}`, { doc: song("Hacked") })).status).toBeGreaterThanOrEqual(403);
    await a.call("PUT", `/api/songs/${id}`, { doc: song("Private one"), isPublic: true });
    expect((await b.call("GET", `/api/songs/${id}`)).status).toBe(200);
    expect((await b.call("DELETE", `/api/songs/${id}`)).status).toBeGreaterThanOrEqual(403);
    expect((await a.call("DELETE", `/api/songs/${id}`)).status).toBe(200);
  });

  it("saves favourites, finds songs by chord, lists demos and similar songs", async () => {
    const { env } = makeEnv();
    const { client } = await signUp(env, "lib@example.com");
    const mine = (await client.call("POST", "/api/songs", { doc: song("Bb Song", ["Bbm7", "Gb", "Db"]) })).body.song.id;
    expect((await client.call("PUT", "/api/songs/demo-porch-light/favorite")).body).toEqual({ favorite: true });
    const lib = (await client.call("GET", "/api/songs?scope=library")).body.songs.map((s: { id: string }) => s.id);
    expect(lib).toEqual(expect.arrayContaining([mine, "demo-porch-light"]));
    const found = (await client.call("GET", "/api/songs?scope=search&q=Bbm7")).body.songs.map((s: { id: string }) => s.id);
    expect(found).toContain(mine);
    const discover = (await client.call("GET", "/api/songs?scope=discover")).body.songs;
    expect(discover.some((s: { isDemo: boolean }) => s.isDemo)).toBe(true);
    expect((await client.call("GET", `/api/songs/${mine}/similar`)).status).toBe(200);
    await client.call("PUT", "/api/songs/demo-porch-light/progress", { settings: { part: "lead", capo: 2 }, played: true });
    const progress = (await client.call("GET", "/api/songs/demo-porch-light/progress")).body;
    expect(progress.settings).toMatchObject({ part: "lead", capo: 2 });
    expect(progress.plays).toBe(1);
    const recent = (await client.call("GET", "/api/songs?scope=recent")).body.songs;
    expect(recent[0].id).toBe("demo-porch-light");
  });

  it("manages playlists", async () => {
    const { env } = makeEnv();
    const { client } = await signUp(env, "pl@example.com");
    const pl = (await client.call("POST", "/api/playlists", { name: "Gig" })).body.playlist;
    await client.call("POST", `/api/playlists/${pl.id}/songs`, { songId: "demo-iron-sparks" });
    await client.call("POST", `/api/playlists/${pl.id}/songs`, { songId: "demo-rain-waltz" });
    await client.call("PUT", `/api/playlists/${pl.id}/order`, { songIds: ["demo-rain-waltz", "demo-iron-sparks"] });
    const got = (await client.call("GET", `/api/playlists/${pl.id}`)).body;
    expect(got.songs.map((s: { id: string }) => s.id)).toEqual(["demo-rain-waltz", "demo-iron-sparks"]);
    const { client: other } = await signUp(env, "other@example.com");
    expect((await other.call("GET", `/api/playlists/${pl.id}`)).status).toBe(404);
  });
});

describe("audio", () => {
  it("uploads, serves byte ranges to the owner only", async () => {
    const { env } = makeEnv();
    const { client } = await signUp(env, "au@example.com");
    const bytes = new Uint8Array(1000).map((_, i) => i % 256);
    const up = await client.fetch("POST", "/api/audio", {
      body: bytes,
      headers: { "Content-Type": "audio/mpeg", "X-File-Name": encodeURIComponent("take 1.mp3"), "Content-Length": "1000" },
    });
    expect(up.status).toBe(200);
    const { audio } = (await up.json()) as { audio: { key: string } };
    const part = await client.fetch("GET", `/api/audio/${encodeURIComponent(audio.key)}`, { headers: { Range: "bytes=10-19" } });
    expect(part.status).toBe(206);
    expect(Array.from(new Uint8Array(await part.arrayBuffer()))).toEqual([10, 11, 12, 13, 14, 15, 16, 17, 18, 19]);
    const { client: other } = await signUp(env, "nope@example.com");
    expect((await other.fetch("GET", `/api/audio/${encodeURIComponent(audio.key)}`)).status).toBe(404);
  });
});

describe("validateSongDoc", () => {
  it("rejects garbage and cleans what it keeps", () => {
    expect(() => validateSongDoc("nope")).toThrow();
    expect(() => validateSongDoc({ chords: "G D" })).toThrow();
    const d = validateSongDoc({ ...song("  Hi  "), bpm: 9999, extra: 1, lead: [] });
    expect(d.bpm).toBe(400);
    expect(d.title).toBe("Hi");
    expect("extra" in d).toBe(false);
    expect(d.lead).toBeUndefined();
  });
});
