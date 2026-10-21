// Cajon parts in the background: once a YouTube song is ready, the cajon container (4 cores)
// pulls its drums out and finds the kick, snare and hi-hat hits; they are kept with the song.
// Nobody waits: jobs wait in D1 (yt_songs.cajon) and the container's Durable Object works through
// them one at a time (worker/cajonContainer.ts). When YouTube refuses the cloud, the job tries
// again later.

import { getContainer } from "@cloudflare/containers";
import { drumHits } from "../shared/music/cajon";
import { CAJON_ENGINE, type CajonEvent } from "../shared/prep";
import type { SongDoc } from "../shared/types";
import { validateSongDoc } from "../shared/validate";
import type { Env } from "./env";

export type CajonStatus = "queued" | "working" | "waiting" | "ready" | "failed";

/** A job that started this long ago and never finished (the machine went away) is due again. */
const STUCK_MS = 20 * 60_000;
/** How many times YouTube may refuse before a song is given up on (spread over about 4 days). */
const MAX_BLOCKED = 8;
const MAX_FAILED = 3;

/** Waits before trying again: 15 min, 30 min, 1 h ... up to 12 h. */
export function retryDelay(tries: number): number {
  return Math.min(12 * 60, 15 * 2 ** Math.max(0, tries - 1)) * 60_000;
}

/** Asks for a song's cajon part; the work starts in the background. */
export async function queueCajon(env: Env, videoId: string): Promise<void> {
  if (!env.CAJON) return;
  await env.DB
    .prepare("UPDATE yt_songs SET cajon = 'queued', cajon_at = ?, cajon_tries = 0, cajon_error = '' WHERE video_id = ?")
    .bind(Date.now(), videoId)
    .run();
  await kickCajon(env);
}

/** Makes sure the cajon container is working through its queue. */
export async function kickCajon(env: Env): Promise<void> {
  if (!env.CAJON) return;
  try {
    await getContainer(env.CAJON, "cajon").kick();
  } catch (e) {
    console.warn("cajon: couldn't start the queue", (e as Error).message);
  }
}

/** A song's cajon state, asking for its part if no one has yet (songs made before cajon). A
 *  part still to come nudges the queue, so it never sits still for long. */
export async function cajonStatus(env: Env, videoId: string, songId: string): Promise<CajonStatus | null> {
  const row = await env.DB
    .prepare("SELECT status, song_id, cajon FROM yt_songs WHERE video_id = ?")
    .bind(videoId)
    .first<{ status: string; song_id: string | null; cajon: CajonStatus | null }>();
  if (!row || row.status !== "ready" || row.song_id !== songId) return null;
  if (row.cajon === null && env.CAJON) {
    await queueCajon(env, videoId);
    return "queued";
  }
  if (row.cajon === "queued" || row.cajon === "waiting" || row.cajon === "working") await kickCajon(env);
  return row.cajon;
}

interface Job {
  videoId: string;
  songId: string;
  tries: number;
}

/** The job to do now, if any: the longest-waiting one that is due. */
export async function nextCajonJob(db: D1Database, now = Date.now()): Promise<Job | null> {
  const row = await db
    .prepare(
      `SELECT video_id, song_id, cajon_tries FROM yt_songs
       WHERE status = 'ready' AND song_id IS NOT NULL
         AND ((cajon IN ('queued', 'waiting') AND cajon_at <= ?1) OR (cajon = 'working' AND cajon_at <= ?2))
       ORDER BY cajon_at LIMIT 1`,
    )
    .bind(now, now - STUCK_MS)
    .first<{ video_id: string; song_id: string; cajon_tries: number }>();
  return row ? { videoId: row.video_id, songId: row.song_id, tries: row.cajon_tries } : null;
}

/** When the next job falls due (ms), or null with nothing left to do. */
export async function nextCajonTime(db: D1Database): Promise<number | null> {
  const row = await db
    .prepare(
      `SELECT MIN(CASE WHEN cajon = 'working' THEN cajon_at + ?1 ELSE cajon_at END) AS due FROM yt_songs
       WHERE status = 'ready' AND song_id IS NOT NULL AND cajon IN ('queued', 'waiting', 'working')`,
    )
    .bind(STUCK_MS)
    .first<{ due: number | null }>();
  return row?.due ?? null;
}

async function* lines(body: ReadableStream<Uint8Array>): AsyncGenerator<CajonEvent> {
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
      if (line) yield JSON.parse(line) as CajonEvent;
    }
    if (done) break;
  }
  if (buf.trim()) yield JSON.parse(buf) as CajonEvent;
}

function validOnsets(o: unknown): o is { kick: number[]; snare: number[]; hat: number[] } {
  if (!o || typeof o !== "object") return false;
  const list = (v: unknown) => Array.isArray(v) && v.length < 50_000 && v.every((t) => typeof t === "number" && Number.isFinite(t));
  const r = o as Record<string, unknown>;
  return list(r.kick) && list(r.snare) && list(r.hat);
}

/**
 * Does one job: asks the container for the song's drums (`call` sends the request) and keeps
 * them with the song. A refusal by YouTube, or a failure, is tried again later.
 */
export async function runCajonJob(db: D1Database, job: Job, call: (videoId: string) => Promise<Response>, now = () => Date.now()): Promise<CajonStatus> {
  await db.prepare("UPDATE yt_songs SET cajon = 'working', cajon_at = ? WHERE video_id = ?").bind(now(), job.videoId).run();
  let result: CajonEvent | null = null;
  try {
    const res = await call(job.videoId);
    if (!res.ok || !res.body) throw new Error(`the cajon service answered ${res.status}`);
    for await (const e of lines(res.body)) if (e.type !== "progress") result = e;
  } catch (e) {
    result = { type: "error", code: "failed", message: (e as Error).message.slice(0, 300) };
  }

  if (result?.type === "result" && validOnsets(result.onsets)) {
    // the song as it is now (it may have been prepared again meanwhile: same audio, new grid)
    const row = await db.prepare("SELECT data FROM songs WHERE id = ?").bind(job.songId).first<{ data: string }>();
    if (!row) return settle(db, job.videoId, "failed", "The song is gone", 0);
    const doc = JSON.parse(row.data) as SongDoc;
    if (!doc.beatTimes?.length) return settle(db, job.videoId, "failed", "The song has no beat grid", 0);
    doc.drums = { v: 1, hits: drumHits(result.onsets, doc.beatTimes) };
    await db.batch([
      db.prepare("UPDATE songs SET data = ? WHERE id = ?").bind(JSON.stringify(validateSongDoc(doc)), job.songId),
      db
        .prepare("UPDATE yt_songs SET cajon = 'ready', cajon_error = '', cajon_engine = ?, cajon_at = ? WHERE video_id = ?")
        .bind(CAJON_ENGINE, now(), job.videoId),
    ]);
    return "ready";
  }

  const error = result?.type === "error" ? result : { code: "failed" as const, message: "The cajon service didn't finish" };
  const tries = job.tries + 1;
  const again = error.code === "blocked" ? tries < MAX_BLOCKED : error.code === "failed" ? tries < MAX_FAILED : false;
  if (again) return settle(db, job.videoId, "waiting", error.message, tries, now() + retryDelay(tries));
  return settle(db, job.videoId, "failed", error.message, tries);
}

async function settle(db: D1Database, videoId: string, status: CajonStatus, error: string, tries: number, at = Date.now()): Promise<CajonStatus> {
  await db
    .prepare("UPDATE yt_songs SET cajon = ?, cajon_error = ?, cajon_tries = ?, cajon_at = ? WHERE video_id = ?")
    .bind(status, error.slice(0, 300), tries, at, videoId)
    .run();
  return status;
}
