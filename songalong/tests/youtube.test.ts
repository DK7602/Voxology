import { afterEach, describe, expect, it, vi } from "vitest";
import type { PrepAnalysis, PrepEvent, PrepareEvent } from "../shared/prep";
import { cleanTitle, guessSongName, tidyTitle } from "../shared/songName";
import { estimateOffset, matchInOrder } from "../worker/align";
import { findLyrics, parseLrc, retryDelays } from "../worker/lyrics";
import { isoDuration } from "../worker/youtube";
import { Client, makeEnv, signUp } from "./helpers/workerEnv";

// Made-up lyric lines for tests (not from any real song).
const LINES = [
  "Candles in the morning window",
  "Paper boats along the river",
  "Every bell is ringing slower",
  "Carry me home across the water",
];
// LRC timing relative to "the record"; the video below has a 5 second intro.
const LRC = LINES.map((l, i) => `[00:${String(10 + i * 4).padStart(2, "0")}.00] ${l}`).join("\n") + "\n[00:28.00]";
const INTRO = 5;

function analysis(videoId: string): PrepAnalysis {
  const beatTimes = Array.from({ length: 160 }, (_, i) => 2 + i * 0.5);
  return {
    videoId,
    meta: { title: "Test Artist - Paper Boats (Official Video)", channel: "TestArtistVEVO", duration: 82 },
    detect: {
      bpm: 120,
      beatsPerBar: 4,
      beatTimes,
      key: "G",
      chords: [
        { beat: 0, name: "G" },
        { beat: 8, name: "D" },
        { beat: 16, name: "Em" },
        { beat: 24, name: "C" },
      ],
      length: beatTimes.length,
      duration: 82,
      confidence: 0.8,
    },
    chunks: [0, 30, 60].map((start) => ({ start, mp3: "AAAA" })),
    engine: 2,
  };
}

/** Speech recognition that "hears" the lines at record time + INTRO. */
const fakeAi = {
  async run(_model: string, input: Record<string, unknown>) {
    void input;
    return {
      segments: LINES.map((text, i) => ({ start: 10 + i * 4 + INTRO, end: 13 + i * 4 + INTRO, text })),
    };
  },
};

/** The AI fake returns absolute times for chunk 0 only; other chunks hear nothing. */
function chunkAwareAi() {
  return {
    async run(_m: string, input: Record<string, unknown>) {
      // chunks all carry "AAAA"; answer once, then silence
      if ((this as { done?: boolean }).done) return { segments: [] };
      (this as { done?: boolean }).done = true;
      void input;
      return fakeAi.run(_m, input);
    },
  };
}

function fakeLrclib() {
  return vi.fn(async (url: string | URL | Request) => {
    const u = String(url);
    if (u.startsWith("https://lrclib.net/api/get")) {
      return new Response(
        JSON.stringify({
          id: 1,
          trackName: "Paper Boats",
          artistName: "Test Artist",
          duration: 80,
          instrumental: false,
          plainLyrics: LINES.join("\n"),
          syncedLyrics: LRC,
        }),
        { headers: { "Content-Type": "application/json" } },
      );
    }
    return new Response("not found", { status: 404 });
  });
}

function ndjsonResponse(events: PrepEvent[]): Response {
  return new Response(events.map((e) => JSON.stringify(e)).join("\n") + "\n", { headers: { "Content-Type": "application/x-ndjson" } });
}

function fakePrep(events: (videoId: string) => PrepEvent[]) {
  return {
    idFromName: (n: string) => n,
    get: () => ({
      fetch: async (req: Request) => {
        const { videoId } = (await req.json()) as { videoId: string };
        return ndjsonResponse(events(videoId));
      },
    }),
  };
}

async function readEvents(res: Response): Promise<PrepareEvent[]> {
  const text = await res.text();
  return text.trim().split("\n").filter(Boolean).map((l) => JSON.parse(l));
}

afterEach(() => vi.unstubAllGlobals());

describe("song names from YouTube", () => {
  it("cleans titles and finds artist / title", () => {
    expect(cleanTitle("Test Artist - Paper Boats (Official Video) [HD]")).toBe("Test Artist - Paper Boats");
    expect(guessSongName({ title: "Test Artist - Paper Boats (Official Video)", channel: "x" })).toEqual({ artist: "Test Artist", title: "Paper Boats" });
    expect(guessSongName({ title: "Paper Boats", channel: "Test Artist - Topic" })).toEqual({ artist: "Test Artist", title: "Paper Boats" });
    expect(guessSongName({ title: "whatever", channel: "c", artist: "Real Artist, Guest", track: "Song ft. Someone" })).toEqual({
      artist: "Real Artist",
      title: "Song",
    });
    expect(guessSongName({ title: "Test Artist - Paper Boats feat. Guest | Lyrics", channel: "c" }).title).toBe("Paper Boats");
  });
  it("drops the artist from a lyrics-site title", () => {
    expect(tidyTitle("Test Artist - Paper Boats", "Test Artist")).toBe("Paper Boats");
    expect(tidyTitle("test artist: Paper Boats", "Test Artist")).toBe("Paper Boats");
    expect(tidyTitle("Paper Boats", "Test Artist")).toBe("Paper Boats");
    expect(tidyTitle("A.B. - Song (Live)", "A.B.")).toBe("Song (Live)");
    expect(tidyTitle("Test Artist", "Test Artist")).toBe("Test Artist");
  });
  it("reads ISO durations", () => {
    expect(isoDuration("PT3M45S")).toBe(225);
    expect(isoDuration("PT1H2M")).toBe(3720);
  });
});

describe("lyrics timing", () => {
  it("tries LRCLIB again when it is busy", async () => {
    const saved = retryDelays.ms;
    retryDelays.ms = [0, 0, 0];
    try {
      const ok = fakeLrclib();
      let busy = 2;
      const flaky = vi.fn(async (url: string | URL | Request) => {
        if (busy-- > 0) return new Response('{"name":"ServerOverloaded"}', { status: 503 });
        return ok(url);
      });
      const found = await findLyrics({ artist: "Test Artist", title: "Paper Boats" }, 80, flaky as unknown as typeof fetch);
      expect(found?.synced.length).toBeGreaterThan(0);
      expect(flaky).toHaveBeenCalledTimes(3);
      // a plain "not found" is not retried
      const missing = vi.fn(async () => new Response("not found", { status: 404 }));
      expect(await findLyrics({ artist: "A", title: "B" }, 80, missing as unknown as typeof fetch)).toBeNull();
      expect(missing).toHaveBeenCalledTimes(3); // get + two searches, once each
    } finally {
      retryDelays.ms = saved;
    }
  });
  it("parses LRC", () => {
    const l = parseLrc("[ar: x]\n[00:10.50] one two\n[01:02.00][01:30.00] again\n");
    expect(l).toEqual([
      { time: 10.5, text: "one two" },
      { time: 62, text: "again" },
      { time: 90, text: "again" },
    ]);
  });
  it("finds the offset between the record and the video", () => {
    const lines = parseLrc(LRC);
    const segs = LINES.map((text, i) => ({ start: 10 + i * 4 + INTRO + 0.3, end: 0, text }));
    const est = estimateOffset(lines, segs);
    expect(est.matches).toBe(4);
    expect(est.offset).toBeCloseTo(INTRO + 0.3, 1);
  });
  it("places plain lyrics in order, filling gaps", () => {
    const segs = [
      { start: 12, end: 14, text: LINES[0] },
      { start: 20, end: 22, text: LINES[2] },
    ];
    const out = matchInOrder(LINES.slice(0, 3), segs);
    expect(out.map((l) => l.time)).toEqual([12, 16, 20]);
  });
});

describe("preparing a song", () => {
  it("cloud path: prepares, lines the lyrics up, saves to the catalog", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    const { env } = makeEnv({
      PREP: fakePrep((videoId) => [
        { type: "progress", stage: "Getting the song", fraction: 0.1 },
        { type: "result", analysis: analysis(videoId) },
      ]),
      AI: chunkAwareAi(),
    });
    const { client } = await signUp(env, "yt@example.com");
    const res = await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "abcdefghijk", title: "t", channel: "c", duration: 82 } });
    const events = await readEvents(res);
    const ready = events.at(-1)!;
    expect(ready.type).toBe("ready");
    const songId = (ready as { songId: string }).songId;
    const song = (await client.call("GET", `/api/songs/${songId}`)).body.song;
    expect(song.title).toBe("Paper Boats");
    expect(song.artist).toBe("Test Artist");
    expect(song.isPublic).toBe(true);
    expect(song.mine).toBe(false);
    expect(song.doc.media).toEqual({ kind: "youtube", videoId: "abcdefghijk" });
    expect(song.doc.credits).toContain("LRCLIB");
    // first line: record 10 s + 5 s intro = 15 s -> beat (15 - 2) / 0.5 = 26
    expect(song.doc.lines[0]).toMatchObject({ text: LINES[0], beat: 26 });
    expect(song.doc.lines).toHaveLength(4);
    // the empty last LRC line ends the final line
    expect(song.doc.lines[3].end).toBe(Math.round(((28 + INTRO - 2) / 0.5) * 4) / 4);
    // which detection version made it
    expect((await env.DB.prepare("SELECT engine FROM yt_songs WHERE video_id = 'abcdefghijk'").first<{ engine: number }>())?.engine).toBe(2);
    // asking again is instant
    const again = await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "abcdefghijk" } }));
    expect(again).toEqual([{ type: "ready", songId }]);
    // another user sees it too
    const { client: other } = await signUp(env, "other@example.com");
    expect((await other.call("GET", `/api/songs/${songId}`)).status).toBe(200);
  });

  it("blocked in the cloud -> the user's PC helper prepares it", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    const { env } = makeEnv({
      PREP: fakePrep(() => [{ type: "error", code: "blocked", message: "YouTube refused this computer for now." }]),
      AI: chunkAwareAi(),
    });
    const { client } = await signUp(env, "pc@example.com");
    const ev = await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "zyxwvutsrqp" } }));
    expect(ev.at(-1)!.type).toBe("waiting_pc");
    let status = (await client.call("GET", "/api/youtube/status/zyxwvutsrqp")).body;
    expect(status).toMatchObject({ status: "waiting_pc", helperOnline: false });

    // the user connects a PC helper
    const { token } = (await client.call("POST", "/api/helper/tokens", { name: "Studio PC" })).body;
    const pc = new Client(env);
    const auth = { headers: { Authorization: `Bearer ${token}` }, origin: null };
    const next = await pc.call("GET", "/api/helper/next", undefined, auth);
    expect(next.body.job).toMatchObject({ videoId: "zyxwvutsrqp" });
    expect((await pc.call("GET", "/api/helper/next", undefined, auth)).body.job).toBeNull();
    await pc.call("POST", "/api/helper/jobs/zyxwvutsrqp/progress", { stage: "Getting the song", fraction: 0.2 }, auth);
    status = (await client.call("GET", "/api/youtube/status/zyxwvutsrqp")).body;
    expect(status).toMatchObject({ status: "working", helperOnline: true });
    const done = await pc.call("POST", "/api/helper/jobs/zyxwvutsrqp/result", { analysis: analysis("zyxwvutsrqp") }, auth);
    expect(done.status).toBe(200);
    status = (await client.call("GET", "/api/youtube/status/zyxwvutsrqp")).body;
    expect(status).toMatchObject({ status: "ready", songId: done.body.songId });

    // keys are private to their owner and can be revoked
    expect((await pc.call("GET", "/api/helper/next", undefined, { headers: { Authorization: "Bearer sah_nottherealkeyatall123" }, origin: null })).status).toBe(401);
    const list = (await client.call("GET", "/api/helper/tokens")).body.helpers;
    await client.call("DELETE", `/api/helper/tokens/${list[0].id}`);
    expect((await pc.call("GET", "/api/helper/next", undefined, auth)).status).toBe(401);
  });

  it("an unusable video fails with a clear message", async () => {
    const { env } = makeEnv({
      PREP: fakePrep(() => [{ type: "error", code: "unavailable", message: "This video can't be used." }]),
    });
    const { client } = await signUp(env, "un@example.com");
    const ev = await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "aaaaaaaaaaa" } }));
    expect(ev.at(-1)).toEqual({ type: "error", message: "This video can't be used." });
  });

  it("without the cloud container, songs wait for the PC helper", async () => {
    const { env } = makeEnv();
    const { client } = await signUp(env, "nocloud@example.com");
    const ev = await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "bbbbbbbbbbb" } }));
    expect(ev.at(-1)!.type).toBe("waiting_pc");
  });

  it("search marks songs that are ready", async () => {
    vi.stubGlobal(
      "fetch",
      vi.fn(async (url: string | URL | Request) => {
        const u = String(url);
        if (u.includes("/youtube/v3/search")) {
          return Response.json({ items: [{ id: { videoId: "abcdefghijk" }, snippet: { title: "Test &amp; Song", channelTitle: "Chan" } }] });
        }
        if (u.includes("/youtube/v3/videos")) return Response.json({ items: [{ id: "abcdefghijk", contentDetails: { duration: "PT3M1S" } }] });
        return new Response("", { status: 404 });
      }),
    );
    const { env, db } = makeEnv({ YOUTUBE_API_KEY: "test-key" });
    const { client } = await signUp(env, "search@example.com");
    db.sqlite.exec(
      "INSERT INTO yt_songs (video_id, status, song_id, created_at, updated_at) VALUES ('abcdefghijk', 'ready', 'song123', 0, 0)",
    );
    const r = (await client.call("GET", "/api/youtube/search?q=test")).body.results;
    expect(r[0]).toMatchObject({ videoId: "abcdefghijk", title: "Test & Song", duration: 181, status: "ready", songId: "song123" });
  });
});
