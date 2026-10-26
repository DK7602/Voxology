// /api/youtube/*: search any song, prepare it (cloud first, the user's PC helper as backup),
// and report progress.

import { getContainer } from "@cloudflare/containers";
import { Hono } from "hono";
import {
  isVideoId,
  type PrepAnalysis,
  type PrepareEvent,
  type PrepEvent,
  type PrepStatus,
  type YouTubeResult,
} from "../shared/prep";
import { queueCajon } from "./cajon";
import { finishPrep } from "./catalog";
import type { AppEnv, Env } from "./env";
import { HttpError, readJsonObject } from "./http";
import { currentUser } from "./session";

export const youtube = new Hono<AppEnv>();

interface YtRow {
  video_id: string;
  status: "working" | "waiting_pc" | "ready" | "failed";
  song_id: string | null;
  title: string;
  channel: string;
  duration: number;
  stage: string;
  progress: number;
  error: string;
  source: string;
  requested_by: string | null;
  claimed_at: number | null;
  attempts: number;
  updated_at: number;
}

const HELPER_ONLINE_MS = 60_000;
const STALE_WORKING_MS = 6 * 60_000;

export async function getRow(db: D1Database, videoId: string): Promise<YtRow | null> {
  return db.prepare("SELECT * FROM yt_songs WHERE video_id = ?").bind(videoId).first<YtRow>();
}

export async function helperOnline(db: D1Database, userId: string): Promise<boolean> {
  const r = await db
    .prepare("SELECT 1 AS on_line FROM helper_tokens WHERE user_id = ? AND last_seen_at > ? LIMIT 1")
    .bind(userId, Date.now() - HELPER_ONLINE_MS)
    .first();
  return !!r;
}

export async function setStage(db: D1Database, videoId: string, stage: string, progress: number) {
  await db
    .prepare("UPDATE yt_songs SET stage = ?, progress = ?, updated_at = ? WHERE video_id = ?")
    .bind(stage, progress, Date.now(), videoId)
    .run();
}

// ---- search ----

function decodeEntities(s: string): string {
  return s
    .replace(/&amp;/g, "&")
    .replace(/&quot;/g, '"')
    .replace(/&#39;|&apos;/g, "'")
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">");
}

/** "PT3M45S" -> 225 */
export function isoDuration(s: string): number {
  const m = /^P(?:(\d+)D)?T?(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?$/.exec(s);
  if (!m) return 0;
  return (+(m[1] ?? 0)) * 86400 + (+(m[2] ?? 0)) * 3600 + (+(m[3] ?? 0)) * 60 + (+(m[4] ?? 0));
}

async function searchWithApi(q: string, key: string): Promise<YouTubeResult[]> {
  const params = new URLSearchParams({ part: "snippet", type: "video", videoEmbeddable: "true", maxResults: "12", q, key });
  const res = await fetch(`https://www.googleapis.com/youtube/v3/search?${params}`);
  if (!res.ok) throw new HttpError(502, res.status === 403 ? "YouTube search is over its daily limit. Try again tomorrow." : "YouTube search isn't answering right now.");
  const data = (await res.json()) as { items?: { id: { videoId?: string }; snippet: { title: string; channelTitle: string } }[] };
  const items = (data.items ?? []).filter((i) => i.id.videoId);
  const ids = items.map((i) => i.id.videoId!);
  const durations = new Map<string, number>();
  if (ids.length) {
    const vr = await fetch(`https://www.googleapis.com/youtube/v3/videos?${new URLSearchParams({ part: "contentDetails", id: ids.join(","), key })}`);
    if (vr.ok) {
      const vd = (await vr.json()) as { items?: { id: string; contentDetails: { duration: string } }[] };
      for (const v of vd.items ?? []) durations.set(v.id, isoDuration(v.contentDetails.duration));
    }
  }
  return items.map((i) => ({
    videoId: i.id.videoId!,
    title: decodeEntities(i.snippet.title),
    channel: decodeEntities(i.snippet.channelTitle),
    duration: durations.get(i.id.videoId!) ?? 0,
    thumbnail: `https://i.ytimg.com/vi/${i.id.videoId}/hqdefault.jpg`,
  }));
}

async function searchWithContainer(env: Env, q: string): Promise<YouTubeResult[]> {
  const stub = getContainer(env.PREP!, "prep-search");
  const res = await stub.fetch(new Request(`http://prep/search?q=${encodeURIComponent(q)}`));
  if (!res.ok) throw new HttpError(502, "Song search isn't answering right now. Try again in a moment.");
  return ((await res.json()) as { results: YouTubeResult[] }).results;
}

youtube.get("/search", async (c) => {
  currentUser(c);
  const q = (c.req.query("q") ?? "").trim().slice(0, 200);
  if (!q) throw new HttpError(400, "Type a song or artist to search for");
  let results: YouTubeResult[];
  if (c.env.YOUTUBE_API_KEY) {
    try {
      results = await searchWithApi(q, c.env.YOUTUBE_API_KEY);
    } catch (e) {
      // over the key's daily limit (about 99 searches, shared by everyone), or YouTube not
      // answering: the prep container searches instead, so search keeps working
      if (!c.env.PREP) throw e;
      console.warn("youtube search: using the container instead:", (e as Error).message);
      results = await searchWithContainer(c.env, q);
    }
  } else if (c.env.PREP) results = await searchWithContainer(c.env, q);
  else throw new HttpError(503, "Song search isn't switched on yet: it needs the YouTube key or the cloud song-prep.");
  if (results.length) {
    const marks = await c.env.DB
      .prepare(`SELECT video_id, status, song_id FROM yt_songs WHERE video_id IN (${results.map(() => "?").join(",")})`)
      .bind(...results.map((r) => r.videoId))
      .all<{ video_id: string; status: YouTubeResult["status"]; song_id: string | null }>();
    const byId = new Map(marks.results.map((m) => [m.video_id, m]));
    for (const r of results) {
      const m = byId.get(r.videoId);
      if (m) {
        r.status = m.status;
        if (m.song_id && m.status === "ready") r.songId = m.song_id;
      }
    }
  }
  return c.json({ results });
});

// ---- prepare ----

async function* ndjson(body: ReadableStream<Uint8Array>): AsyncGenerator<unknown> {
  const reader = body.getReader();
  const dec = new TextDecoder();
  let buf = "";
  for (;;) {
    const { done, value } = await reader.read();
    if (value) buf += dec.decode(value, { stream: true });
    let nl: number;
    while ((nl = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, nl).trim();
      buf = buf.slice(nl + 1);
      if (line) yield JSON.parse(line);
    }
    if (done) break;
  }
  if (buf.trim()) yield JSON.parse(buf);
}

/** Spreads songs over a few container instances. */
/** How many prep machines songs are spread over (prep-0, prep-1, ...). */
export const PREP_BUCKETS = 3;

function bucket(videoId: string, n = PREP_BUCKETS): number {
  let h = 0;
  for (const ch of videoId) h = (h * 31 + ch.charCodeAt(0)) >>> 0;
  return h % n;
}

async function toWaitingPc(env: Env, userId: string, videoId: string, why: string, emit: (e: PrepareEvent) => void) {
  await env.DB
    .prepare("UPDATE yt_songs SET status = 'waiting_pc', source = 'pc', claimed_by = NULL, stage = ?, error = ?, updated_at = ? WHERE video_id = ?")
    .bind("Waiting for your PC helper", why, Date.now(), videoId)
    .run();
  const online = await helperOnline(env.DB, userId);
  emit({
    type: "waiting_pc",
    message: online ? "Your PC helper is preparing this song." : "Switch on your PC helper (Settings → Your PC helper) and this song will be prepared there.",
  });
}

/** Waits for a job someone else (another tab, a PC helper) is already running. */
async function follow(env: Env, videoId: string, emit: (e: PrepareEvent) => void) {
  const until = Date.now() + 4 * 60_000;
  let last = "";
  while (Date.now() < until) {
    const row = await getRow(env.DB, videoId);
    if (!row) return emit({ type: "error", message: "This song isn't being prepared." });
    if (row.status === "ready" && row.song_id) return emit({ type: "ready", songId: row.song_id });
    if (row.status === "failed") return emit({ type: "error", message: row.error || "This song couldn't be prepared." });
    if (row.status === "waiting_pc") return emit({ type: "waiting_pc", message: "Waiting for your PC helper to prepare this song." });
    const key = `${row.stage}|${row.progress}`;
    if (key !== last) emit({ type: "progress", stage: row.stage || "Preparing", fraction: row.progress });
    last = key;
    await new Promise((r) => setTimeout(r, 1500));
  }
  emit({ type: "error", message: "This is taking longer than expected. Check again in a minute." });
}

export async function runPrepare(
  env: Env,
  userId: string,
  videoId: string,
  meta: { title: string; channel: string; duration: number; retry: boolean },
  emit: (e: PrepareEvent) => void,
) {
  const db = env.DB;
  const now = Date.now();
  const row = await getRow(db, videoId);
  if (row?.status === "ready" && row.song_id) return emit({ type: "ready", songId: row.song_id });
  const fresh = row && now - row.updated_at < (row.source === "pc" ? STALE_WORKING_MS : 90_000);
  if (row?.status === "working" && fresh) return follow(env, videoId, emit);
  if (row?.status === "waiting_pc" && !meta.retry) {
    return emit({ type: "waiting_pc", message: "Waiting for your PC helper to prepare this song." });
  }

  await db
    .prepare(
      `INSERT INTO yt_songs (video_id, status, title, channel, duration, stage, progress, source, requested_by, attempts, created_at, updated_at)
       VALUES (?1, 'working', ?2, ?3, ?4, 'Starting', 0, 'cloud', ?5, 1, ?6, ?6)
       ON CONFLICT(video_id) DO UPDATE SET status = 'working', source = 'cloud', stage = 'Starting', progress = 0, error = '',
         requested_by = ?5, attempts = attempts + 1, claimed_by = NULL, updated_at = ?6`,
    )
    .bind(videoId, meta.title.slice(0, 300), meta.channel.slice(0, 200), meta.duration, userId, now)
    .run();
  emit({ type: "progress", stage: "Starting", fraction: 0.01 });

  if (!env.PREP) return toWaitingPc(env, userId, videoId, "The cloud song-prep isn't set up.", emit);

  let lastSave = 0;
  const progress = (stage: string, fraction: number) => {
    emit({ type: "progress", stage, fraction });
    const t = Date.now();
    if (t - lastSave > 1500) {
      lastSave = t;
      void setStage(db, videoId, stage, fraction).catch(() => {});
    }
  };

  let analysis: PrepAnalysis | null = null;
  try {
    const stub = getContainer(env.PREP, `prep-${bucket(videoId)}`);
    const res = await stub.fetch(
      new Request("http://prep/prep", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ videoId }) }),
    );
    if (!res.ok || !res.body) throw new Error(`song-prep answered ${res.status}`);
    for await (const raw of ndjson(res.body)) {
      const ev = raw as PrepEvent;
      if (ev.type === "progress") progress(ev.stage, ev.fraction * 0.88);
      else if (ev.type === "result") analysis = ev.analysis;
      else if (ev.type === "error") {
        if (ev.code === "unavailable") {
          await db.prepare("UPDATE yt_songs SET status = 'failed', error = ?, updated_at = ? WHERE video_id = ?").bind(ev.message, Date.now(), videoId).run();
          return emit({ type: "error", message: ev.message });
        }
        return toWaitingPc(env, userId, videoId, ev.message, emit);
      }
    }
  } catch (e) {
    console.warn("cloud prep failed", (e as Error).message);
    return toWaitingPc(env, userId, videoId, "The cloud song-prep couldn't finish.", emit);
  }
  if (!analysis) return toWaitingPc(env, userId, videoId, "The cloud song-prep didn't finish.", emit);
  const songId = await finishPrep(db, env.AI, analysis, progress);
  emit({ type: "ready", songId });
  await queueCajon(env, videoId);
}

youtube.post("/prepare", async (c) => {
  const user = currentUser(c);
  const body = await readJsonObject(c);
  if (!isVideoId(body.videoId)) throw new HttpError(400, "That isn't a YouTube video id");
  const videoId = body.videoId;
  const meta = {
    title: typeof body.title === "string" ? body.title : "",
    channel: typeof body.channel === "string" ? body.channel : "",
    duration: typeof body.duration === "number" && Number.isFinite(body.duration) ? body.duration : 0,
    retry: body.retry === true,
  };
  const { readable, writable } = new TransformStream<Uint8Array, Uint8Array>();
  const writer = writable.getWriter();
  const enc = new TextEncoder();
  const emit = (e: PrepareEvent) => void writer.write(enc.encode(JSON.stringify(e) + "\n")).catch(() => {});
  const task = (async () => {
    try {
      await runPrepare(c.env, user.id, videoId, meta, emit);
    } catch (e) {
      console.error("prepare failed", e);
      emit({ type: "error", message: "Something went wrong preparing this song. Try again." });
    } finally {
      await writer.close().catch(() => {});
    }
  })();
  try {
    c.executionCtx.waitUntil(task);
  } catch {
    /* no execution context (tests) */
  }
  return new Response(readable, { headers: { "Content-Type": "application/x-ndjson", "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff" } });
});

youtube.get("/status/:videoId", async (c) => {
  const user = currentUser(c);
  const videoId = c.req.param("videoId");
  if (!isVideoId(videoId)) throw new HttpError(400, "That isn't a YouTube video id");
  const row = await getRow(c.env.DB, videoId);
  const status: PrepStatus = {
    videoId,
    status: row?.status ?? "none",
    stage: row?.stage ?? "",
    progress: row?.progress ?? 0,
    songId: row?.status === "ready" ? row.song_id : null,
    error: row?.error ?? "",
    helperOnline: await helperOnline(c.env.DB, user.id),
  };
  return c.json(status);
});
