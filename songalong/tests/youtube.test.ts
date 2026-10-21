import { afterEach, describe, expect, it, vi } from "vitest";
import { PREP_ENGINE, type PrepAnalysis, type PrepEvent, type PrepareEvent } from "../shared/prep";
import { cleanTitle, guessSongName, nameCandidates, tidyTitle } from "../shared/songName";
import { alignVersion, anchorChain, estimateOffset, matchInOrder, oneOffsetFits, steadyChain, timeWords, transcribe, versionLines, warp, type Segment } from "../worker/align";
import { believableTiming, findLyrics, parseLrc, retryDelays } from "../worker/lyrics";
import { nextCajonJob, nextCajonTime, retryDelay, runCajonJob } from "../worker/cajon";
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
  it("reads an artist in brackets, and channel names written as one word", () => {
    const meta = { title: "Mercy Road [Harbor Light Music]", channel: "HarborLightMusic" };
    expect(nameCandidates(meta)[0]).toEqual({ artist: "Harbor Light Music", title: "Mercy Road" });
    expect(guessSongName(meta)).toEqual({ artist: "Harbor Light Music", title: "Mercy Road" });
    // a bracket that isn't the channel's artist stays part of the title
    expect(guessSongName({ title: "Mercy Road (Part Two)", channel: "HarborLightMusic" })).toEqual({ artist: "Harbor Light", title: "Mercy Road (Part Two)" });
    expect(guessSongName({ title: "Mercy Road", channel: "TestArtistVEVO" }).artist).toBe("Test Artist");
  });
  it("cleans titles and finds artist / title", () => {
    expect(cleanTitle("Test Artist - Paper Boats (Official Video) [HD]")).toBe("Test Artist - Paper Boats");
    expect(cleanTitle("Paper Boats (Performance Track In Key of E)")).toBe("Paper Boats");
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
  it("reads cover titles both ways round, without the decorations", () => {
    expect(nameCandidates({ title: "Paper Boats - Test Artist || ACOUSTIC", channel: "Some Covers" })).toEqual([
      { artist: "Paper Boats", title: "Test Artist" },
      { artist: "Test Artist", title: "Paper Boats" },
    ]);
    expect(nameCandidates({ title: "Paper Boats | Test Artist | Guitar Cover", channel: "x" }).slice(0, 2)).toEqual([
      { artist: "Paper Boats", title: "Test Artist" },
      { artist: "Test Artist", title: "Paper Boats" },
    ]);
    expect(nameCandidates({ title: "Test Artist - Paper Boats (Live)", channel: "x" })[0]).toEqual({ artist: "Test Artist", title: "Paper Boats" });
    expect(cleanTitle("Paper Boats (Piano Karaoke)")).toBe("Paper Boats");
  });
  it("reads ISO durations", () => {
    expect(isoDuration("PT3M45S")).toBe(225);
    expect(isoDuration("PT1H2M")).toBe(3720);
  });
});

describe("lyrics timing", () => {
  it("times another version's lyrics by where the lines are heard", async () => {
    // the record sings at 10, 14, 18, 22 s; this cover is faster and starts sooner
    const lines = LINES.map((text, i) => ({ time: 10 + i * 4, text }));
    const heard = [
      { start: 4, end: 6, text: LINES[0] },
      { start: 13, end: 15, text: LINES[3] },
    ];
    const chain = anchorChain(lines, heard);
    expect(chain).toEqual([{ lyric: 10, video: 4 }, { lyric: 22, video: 13 }]);
    expect(warp(chain, 16)).toBeCloseTo(8.5); // halfway between the anchors
    const out = alignVersion(lines, heard, 120, 90);
    expect(out.timing).toBe("aligned");
    expect(out.lines.map((l) => Math.round(l.time * 10) / 10)).toEqual([4, 7, 10, 13]);
    // nothing heard: the record's timing is scaled to the video's length
    expect(alignVersion(lines, [], 120, 60).lines[0].time).toBe(5);
  });
  it("finds lyrics for a cover by artist and title even when its length differs", async () => {
    const record = { id: 9, trackName: "Paper Boats", artistName: "Test Artist", duration: 200, instrumental: false, plainLyrics: LINES.join("\n"), syncedLyrics: LRC };
    const byName = vi.fn(async (url: string | URL | Request) => {
      const u = String(url);
      if (u.includes("/search?track_name=")) return Response.json([record]);
      if (u.includes("/search?q=")) return Response.json([record]);
      return new Response("not found", { status: 404 });
    });
    const found = await findLyrics({ artist: "Test Artist", title: "Paper Boats" }, 82, byName as unknown as typeof fetch);
    expect(found?.duration).toBe(200);
    // a loose full-text match that long is not trusted
    const looseOnly = vi.fn(async (url: string | URL | Request) =>
      String(url).includes("/search?q=") ? Response.json([record]) : new Response("not found", { status: 404 }),
    );
    expect(await findLyrics({ artist: "", title: "Paper Boats" }, 82, looseOnly as unknown as typeof fetch)).toBeNull();
  });
  it("doesn't trust words 'heard' in music without singing", () => {
    // a version that skips a part: steady apart from one jump
    expect(steadyChain([{ lyric: 10, video: 5 }, { lyric: 14, video: 9 }, { lyric: 34, video: 11 }, { lyric: 38, video: 15 }, { lyric: 42, video: 19 }])).toBe(true);
    // a piano cover the model "hears" bits of the tune in: jumps all over
    const erratic = [{ lyric: 10, video: 40 }, { lyric: 20, video: 41 }, { lyric: 60, video: 43 }, { lyric: 70, video: 90 }, { lyric: 90, video: 91 }, { lyric: 95, video: 92 }];
    expect(steadyChain(erratic)).toBe(false);
    const lines = erratic.map((a, i) => ({ time: a.lyric, text: `${LINES[i % 4]} ${i}` }));
    const heard = erratic.map((a, i) => ({ start: a.video, end: a.video + 1, text: lines[i].text }));
    // the record's timing, stretched to the video, instead
    expect(alignVersion(lines, heard, 100, 50)).toEqual({ lines: lines.map((l) => ({ ...l, time: l.time / 2 })), timing: "synced" });
  });
  it("leaves out the parts another version doesn't sing", async () => {
    // record: lines every 4 s; the version sings lines 0-1, skips 2-5, then sings 6-7
    const lines = Array.from({ length: 8 }, (_, i) => ({ time: 10 + i * 4, text: `line ${i} words here` }));
    const anchors = [
      { lyric: 10, video: 5 },
      { lyric: 14, video: 9 },
      { lyric: 34, video: 11 },
      { lyric: 38, video: 15 },
    ];
    const out = versionLines(lines, anchors, 1, 60);
    expect(out.map((l) => l.text.split(" ")[1])).toEqual(["0", "1", "6", "7"]);
    expect(out.map((l) => l.time)).toEqual([5, 9, 11, 15]);
  });
  it("listens without the voice filter, leaving out what the model makes up", async () => {
    const calls: unknown[] = [];
    const ai = {
      async run(_m: string, input: Record<string, unknown>) {
        calls.push(input.vad_filter);
        return {
          segments: [
            { start: 1, end: 3, text: LINES[0], no_speech_prob: 0.2, avg_logprob: -0.4, compression_ratio: 1.3 },
            // over the music: "no speech" and unsure
            { start: 5, end: 8, text: "Thanks for watching", no_speech_prob: 0.8, avg_logprob: -1.3, compression_ratio: 1.1 },
            // a loop of the same words
            { start: 9, end: 20, text: "la la la la la la la la la la la la", no_speech_prob: 0.1, avg_logprob: -0.3, compression_ratio: 3.1 },
          ],
        };
      },
    };
    const segs = await transcribe(ai, { start: 30, mp3: "AAAA" });
    expect(calls).toEqual([false]);
    expect(segs).toEqual([{ start: 31, end: 33, text: LINES[0] }]);
  });
  it("passes over lyrics whose timing was typed in carelessly", async () => {
    // first line at 0:00, nothing for two minutes, then the rest crammed together
    const careless = ["[00:00.03] " + LINES[0], "[02:14.66]", ...LINES.slice(1).concat(LINES).map((l, i) => `[02:${String(27 + i).padStart(2, "0")}.${i % 2 ? "10" : "60"}] ${l}`)].join("\n");
    expect(believableTiming(parseLrc(careless))).toBe(false);
    expect(believableTiming(parseLrc(LRC))).toBe(true);
    const track = (id: number, duration: number, syncedLyrics: string) => ({ id, trackName: "Paper Boats", artistName: "Test Artist", duration, instrumental: false, plainLyrics: LINES.join("\n"), syncedLyrics });
    const fetcher = vi.fn(async (url: string | URL | Request) =>
      String(url).includes("/search") ? Response.json([track(1, 258, careless), track(2, 265, LRC)]) : new Response("not found", { status: 404 }),
    );
    // the careless copy is closer in length to the video, but the well-timed one is used
    const found = await findLyrics({ artist: "Test Artist", title: "Paper Boats" }, 250, fetcher as unknown as typeof fetch);
    expect(found?.duration).toBe(265);
    expect(found?.synced[0]).toEqual({ time: 10, text: LINES[0] });
  });
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
  it("times lines by when their first word is sung, not when the heard phrase starts", async () => {
    // the record sings at 23.09 s; the video has 15 s more before it. Speech recognition starts
    // its phrases in the guitar before the singing, and puts two lines in one phrase.
    const lines = [
      { time: 23.09, text: "Today is gonna be the day" },
      { time: 25.4, text: "That they're gonna throw it back to you" },
      { time: 28.2, text: "By now, you should've somehow" },
      { time: 30.5, text: "Realized what you gotta do" },
    ];
    // word times: the given ones, then a word every quarter second
    const timed = (start: number, text: string, at: number[]) =>
      text.split(" ").map((word, i) => ({ word: ` ${word}`, start: at[i] ?? (at[at.length - 1] ?? start) + (i - at.length + 1) * 0.25, end: 0 }));
    const ai = {
      async run() {
        return {
          segments: [
            { start: 5.5, end: 12, text: "Today is gonna be the day that they're gonna throw it back to you", words: timed(8.1, "Today is gonna be the day that they're gonna throw it back to you", [8.1, 8.4, 8.6, 8.9, 9.1, 9.3, 10.4]) },
            { start: 12.5, end: 18, text: "By now, you should've somehow realized what you gotta do", words: timed(13.2, "By now, you should've somehow realized what you gotta do", [13.2, 13.5, 13.8, 14.1, 14.5, 15.5]) },
          ],
        };
      },
    };
    const segs = await transcribe(ai, { start: 30, mp3: "AAAA" });
    expect(segs[0].words?.[0]).toMatchObject({ word: " Today", start: 38.1 });
    const est = estimateOffset(lines, segs);
    expect(est.matches).toBe(4);
    expect(est.offset).toBeCloseTo(15, 1);
    // the phrase starts alone would have put the words 2.5 s early
    const phrasesOnly = segs.map(({ start, end, text }) => ({ start, end, text }));
    expect(estimateOffset(lines, phrasesOnly).offset).toBeLessThan(14);
  });
  it("tells the same recording with a longer intro from another version", () => {
    // sixteen lines over three minutes
    const lines = Array.from({ length: 16 }, (_, i) => ({ time: 20 + i * 12, text: `${LINES[i % 4]} ${["again", "once more", "tonight", "for good"][i >> 2]}` }));
    const heardAt = (shift: (t: number) => number) => lines.map((l) => ({ start: shift(l.time), end: shift(l.time) + 3, text: l.text }));
    // the record with 6 s more at the start (and so a video 14 s longer): one offset fits
    expect(oneOffsetFits(lines, heardAt((t) => t + 6))).toBe(true);
    // a cover a tenth faster: the gap grows line by line
    expect(oneOffsetFits(lines, heardAt((t) => 3 + t * 0.9))).toBe(false);
    // too little heard to tell
    expect(oneOffsetFits(lines, heardAt((t) => t + 6).slice(0, 2))).toBe(false);
  });
  it("times each word where it is sung", () => {
    // line times as the lyrics site has them (each a little off), and what is heard
    const lines = [
      { time: 10, text: "Candles in the morning window" },
      { time: 15.5, text: "Paper boats along the river, ooh" },
      { time: 19, text: "Every bell is ringing slower" },
      { time: 25, text: "Carry me home across the water" },
    ];
    const seg = (words: [string, number, number][]): Segment => ({
      start: words[0][1],
      end: words[words.length - 1][2],
      text: words.map((w) => w[0]).join(" "),
      words: words.map(([word, start, end]) => ({ word: ` ${word}`, start, end })),
    });
    const segs = [
      // the phrase starts in the intro: "Candles" seems to last 2.3 s
      seg([["Candles", 9, 11.3], ["in", 11.3, 11.5], ["the", 11.5, 11.7], ["morning", 11.7, 12.3], ["window.", 12.3, 13.4]]),
      // sung early; the held "ooh" isn't heard
      seg([["Paper", 14.2, 14.6], ["boats", 14.6, 15], ["along", 15, 15.5], ["the", 15.5, 15.8], ["river.", 15.8, 17.5]]),
      // "ringing" sung as "ringin'", "bell" misheard
      seg([["Every", 18.1, 18.4], ["bill", 18.4, 18.8], ["is", 18.8, 19], ["ringin", 19, 19.6], ["slower", 19.6, 20.8]]),
    ];
    const out = timeWords(lines, segs);
    expect(out[0].time).toBeCloseTo(10.58, 2); // shortly before "Candles" ends
    expect(out[0].words!.slice(1)).toEqual([11.3, 11.5, 11.7, 12.3]);
    expect(out[0].wordsEnd).toBe(13.4);
    expect(out[1].time).toBe(14.2); // earlier than the lyrics site said
    expect(out[1].words![5]).toBeGreaterThan(15.8); // "ooh" after "river"
    expect(out[1].words![5]).toBeLessThanOrEqual(19);
    expect(out[2].words).toEqual([18.1, 18.4, 18.8, 19, 19.6]); // "bell" placed between its neighbours
    // never heard: keeps its time, no word times
    expect(out[3]).toEqual({ time: 25, text: "Carry me home across the water" });
  });
  it("puts a backing vocal that wasn't heard after the sung phrase", () => {
    const lines = [
      { time: 10, text: "Candles in the morning window (glow on)" },
      { time: 16, text: "Paper boats along the river" },
    ];
    const segs: Segment[] = [
      {
        start: 10,
        end: 13,
        text: "Candles in the morning window",
        words: [
          { word: " Candles", start: 10, end: 10.5 },
          { word: " in", start: 10.5, end: 10.7 },
          { word: " the", start: 10.7, end: 10.9 },
          { word: " morning", start: 10.9, end: 11.4 },
          { word: " window", start: 11.4, end: 13 }, // held
        ],
      },
    ];
    const [line] = timeWords(lines, segs);
    expect(line.words!.slice(0, 5)).toEqual([10, 10.5, 10.7, 10.9, 11.4]);
    expect(line.words![5]).toBeCloseTo(13.15, 2); // "(glow" once "window" has ended
    expect(line.words![6]).toBeGreaterThan(line.words![5]); // "on)" after it
    expect(line.words![6]).toBeLessThan(16);
  });
  it("lets the last words of a line run past an instrumental break marked too early", () => {
    const lines = [
      { time: 10, text: "Candles in the morning window (glow)" },
      { time: 11.5, text: "♪" },
      { time: 16, text: "Paper boats along the river" },
    ];
    const segs: Segment[] = [
      {
        start: 10,
        end: 12,
        text: "Candles in the morning window",
        words: ["Candles", "in", "the", "morning", "window"].map((word, i) => ({ word: ` ${word}`, start: 10 + i * 0.4, end: 10.4 + i * 0.4 })),
      },
    ];
    const [line, rest] = timeWords(lines, segs);
    expect(line.words![4]).toBeCloseTo(11.6); // "window", after the break was said to start
    expect(line.words![5]).toBeCloseTo(12.15); // "(glow)" once it has ended
    expect(rest).toEqual({ text: "♪", time: 12.15 }); // the break starts after the line
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
    expect(song.doc.credits).toBe("Chords found automatically · Lyrics: LRCLIB");
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

  it("asks for the cajon part once a song is ready, and keeps the drums found", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    let kicks = 0;
    const CAJON = { idFromName: (n: string) => n, get: () => ({ kick: async () => void kicks++ }) };
    const { env } = makeEnv({ PREP: fakePrep((videoId) => [{ type: "result", analysis: analysis(videoId) }]), AI: chunkAwareAi(), CAJON });
    const { client } = await signUp(env, "cajon@example.com");
    const ready = (await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "drumsong001" } }))).at(-1)!;
    const songId = (ready as { songId: string }).songId;
    expect(kicks).toBe(1);
    expect((await client.call("GET", `/api/songs/${songId}`)).body.song.cajon).toBe("queued");

    // the container's turn: it hears a kick on beat 0 and a snare on beat 1 (beats are 0.5 s apart from 2 s)
    const job = (await nextCajonJob(env.DB))!;
    expect(job).toEqual({ videoId: "drumsong001", songId, tries: 0 });
    const done = await runCajonJob(env.DB, job, async () =>
      ndjsonResponse([
        { type: "progress", stage: "Listening for the drums" },
        { type: "result", onsets: { kick: [2, 3], snare: [2.5, 3.5], hat: [2.25] } },
      ] as never),
    );
    expect(done).toBe("ready");
    const song = (await client.call("GET", `/api/songs/${songId}`)).body.song;
    expect(song.cajon).toBe("ready");
    expect(song.doc.drums).toEqual({ v: 1, hits: "1040200010002" });
    expect(await nextCajonJob(env.DB)).toBeNull();
    expect(await nextCajonTime(env.DB)).toBeNull();

    // a song from before cajon parts is asked for when it is opened
    await env.DB.prepare("UPDATE yt_songs SET cajon = NULL").run();
    expect((await client.call("GET", `/api/songs/${songId}`)).body.song.cajon).toBe("queued");
    expect(kicks).toBe(2);
  });

  it("tries the cajon part again later when YouTube refuses the cloud", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    const CAJON = { idFromName: (n: string) => n, get: () => ({ kick: async () => {} }) };
    const { env } = makeEnv({ PREP: fakePrep((videoId) => [{ type: "result", analysis: analysis(videoId) }]), AI: chunkAwareAi(), CAJON });
    const { client } = await signUp(env, "later@example.com");
    await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "drumsong002" } }));
    const now = Date.now();
    const refuse = async () => ndjsonResponse([{ type: "error", code: "blocked", message: "YouTube refused this computer for now." }]);
    expect(await runCajonJob(env.DB, (await nextCajonJob(env.DB, now))!, refuse, () => now)).toBe("waiting");
    expect(await nextCajonJob(env.DB, now + 60_000)).toBeNull();
    expect(await nextCajonTime(env.DB)).toBe(now + retryDelay(1));
    const again = (await nextCajonJob(env.DB, now + retryDelay(1)))!;
    expect(again.tries).toBe(1);
    // a container that falls over counts as a failure, and so is tried again too
    expect(await runCajonJob(env.DB, again, async () => new Response("no", { status: 500 }), () => now)).toBe("waiting");
    // a video that can't be used is given up on at once
    const last = (await nextCajonJob(env.DB, now + retryDelay(3)))!;
    expect(await runCajonJob(env.DB, last, async () => ndjsonResponse([{ type: "error", code: "unavailable", message: "Removed" }]))).toBe("failed");
    expect(await nextCajonTime(env.DB)).toBeNull();
  });

  it("without the cajon container, songs simply have no cajon state", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    const { env } = makeEnv({ PREP: fakePrep((videoId) => [{ type: "result", analysis: analysis(videoId) }]), AI: chunkAwareAi() });
    const { client } = await signUp(env, "nocajon@example.com");
    const ready = (await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "drumsong003" } }))).at(-1)!;
    expect((await client.call("GET", `/api/songs/${(ready as { songId: string }).songId}`)).body.song.cajon).toBeNull();
  });

  it("keeps the lead part found with the chords", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    const lead = [{ beat: 4, name: "note:D4" }, { beat: 4.5, name: "note:D5" }, { beat: 5, name: "N.C." }];
    const withLead = (videoId: string) => {
      const a = analysis(videoId);
      return { ...a, detect: { ...a.detect, lead } };
    };
    const { env } = makeEnv({ PREP: fakePrep((videoId) => [{ type: "result", analysis: withLead(videoId) }]), AI: chunkAwareAi() });
    const { client } = await signUp(env, "riff@example.com");
    const ready = (await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "riffsong001" } }))).at(-1)!;
    const song = (await client.call("GET", `/api/songs/${(ready as { songId: string }).songId}`)).body.song;
    expect(song.doc.lead).toEqual(lead);
    expect(song.doc.credits).toBe("Chords and lead found automatically · Lyrics: LRCLIB");
  });

  it("says when the lyric timing is a guess (no singing heard)", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    const { env } = makeEnv({
      PREP: fakePrep((videoId) => [{ type: "result", analysis: analysis(videoId) }]),
      AI: { async run() { return { segments: [] }; } }, // a piano cover: nothing sung
    });
    const { client } = await signUp(env, "piano@example.com");
    const ready = (await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "pianocover1" } }))).at(-1)!;
    const song = (await client.call("GET", `/api/songs/${(ready as { songId: string }).songId}`)).body.song;
    expect(song.doc.lines).toHaveLength(4);
    expect(song.doc.credits).toBe("Chords found automatically · Lyrics: LRCLIB · timing estimated (no singing heard)");
  });

  it("says when the video is an instrumental version", async () => {
    vi.stubGlobal("fetch", fakeLrclib());
    const piano = (videoId: string) => {
      const a = analysis(videoId);
      return { ...a, meta: { ...a.meta, title: "Test Artist - Paper Boats | Piano Cover" } };
    };
    const { env } = makeEnv({ PREP: fakePrep((videoId) => [{ type: "result", analysis: piano(videoId) }]), AI: chunkAwareAi() });
    const { client } = await signUp(env, "pianist@example.com");
    const ready = (await readEvents(await client.fetch("POST", "/api/youtube/prepare", { json: { videoId: "pianocover2" } }))).at(-1)!;
    const song = (await client.call("GET", `/api/songs/${(ready as { songId: string }).songId}`)).body.song;
    expect(song.doc.credits).toBe("Chords found automatically · Lyrics: LRCLIB · timing estimated (instrumental version)");
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
    // a helper with older song-preparing code is told to update itself, and gets no song
    for (const old of ["/api/helper/next", `/api/helper/next?v=1.0.0&engine=${PREP_ENGINE - 1}`]) {
      expect((await pc.call("GET", old, undefined, auth)).body).toEqual({ job: null, update: true });
    }
    const next = await pc.call("GET", `/api/helper/next?engine=${PREP_ENGINE}`, undefined, auth);
    expect(next.body.job).toMatchObject({ videoId: "zyxwvutsrqp" });
    expect((await pc.call("GET", `/api/helper/next?engine=${PREP_ENGINE}`, undefined, auth)).body.job).toBeNull();
    await pc.call("POST", "/api/helper/jobs/zyxwvutsrqp/progress", { stage: "Getting the song", fraction: 0.2 }, auth);
    status = (await client.call("GET", "/api/youtube/status/zyxwvutsrqp")).body;
    expect(status).toMatchObject({ status: "working", helperOnline: true });
    const done = await pc.call("POST", "/api/helper/jobs/zyxwvutsrqp/result", { analysis: analysis("zyxwvutsrqp") }, auth);
    expect(done.status).toBe(200);
    status = (await client.call("GET", "/api/youtube/status/zyxwvutsrqp")).body;
    expect(status).toMatchObject({ status: "ready", songId: done.body.songId });

    // keys are private to their owner and can be revoked
    expect((await pc.call("GET", `/api/helper/next?engine=${PREP_ENGINE}`, undefined, { headers: { Authorization: "Bearer sah_nottherealkeyatall123" }, origin: null })).status).toBe(401);
    const list = (await client.call("GET", "/api/helper/tokens")).body.helpers;
    await client.call("DELETE", `/api/helper/tokens/${list[0].id}`);
    expect((await pc.call("GET", `/api/helper/next?engine=${PREP_ENGINE}`, undefined, auth)).status).toBe(401);
  });

  it("a new PC helper key replaces keys that never connected", async () => {
    const { env } = makeEnv();
    const { client } = await signUp(env, "keys@example.com");
    const make = async () => (await client.call("POST", "/api/helper/tokens", { name: "Windows PC" })).body.token as string;
    const list = async () => (await client.call("GET", "/api/helper/tokens")).body.helpers as { lastSeenAt: number | null }[];
    const pc = new Client(env);
    const ask = (token: string) => pc.call("GET", `/api/helper/next?engine=${PREP_ENGINE}`, undefined, { headers: { Authorization: `Bearer ${token}` }, origin: null });

    const first = await make();
    const second = await make(); // pressed again before running the first download
    expect(await list()).toHaveLength(1);
    expect((await ask(first)).status).toBe(401);
    expect((await ask(second)).status).toBe(200); // the newest download works, and is now connected
    await make(); // another computer: the connected one stays
    expect((await list()).map((k) => k.lastSeenAt !== null).sort()).toEqual([false, true]);
    expect((await ask(second)).status).toBe(200);
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
