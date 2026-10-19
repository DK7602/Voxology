// /api/helper/*: the user's PC helper prepares songs the cloud couldn't fetch.
//   Signed-in user (cookie):  GET/POST /tokens, DELETE /tokens/:id   (helper keys)
//   PC helper (Bearer key):   GET /next, POST /jobs/:videoId/progress|result|fail

import { Hono, type MiddlewareHandler } from "hono";
import { isVideoId, type PrepAnalysis } from "../shared/prep";
import { finishPrep } from "./catalog";
import { randomToken, sha256Hex } from "./crypto";
import type { AppEnv } from "./env";
import { HttpError, readJsonObject, type Ctx } from "./http";
import { currentUser } from "./session";
import { setStage } from "./youtube";

export const helper = new Hono<AppEnv>();

const STALE_CLAIM_MS = 6 * 60_000;
const RESULT_LIMIT = 40 * 1024 * 1024;

// ---- keys (managed from Settings) ----

helper.get("/tokens", async (c) => {
  const user = currentUser(c);
  const rows = await c.env.DB
    .prepare("SELECT token_hash, name, created_at, last_seen_at FROM helper_tokens WHERE user_id = ? ORDER BY created_at DESC")
    .bind(user.id)
    .all<{ token_hash: string; name: string; created_at: number; last_seen_at: number | null }>();
  return c.json({
    helpers: rows.results.map((r) => ({ id: r.token_hash.slice(0, 12), name: r.name, createdAt: r.created_at, lastSeenAt: r.last_seen_at })),
  });
});

helper.post("/tokens", async (c) => {
  const user = currentUser(c);
  const body = await readJsonObject(c);
  const name = typeof body.name === "string" && body.name.trim() ? body.name.trim().slice(0, 60) : "My PC";
  const token = "sah_" + randomToken();
  const hash = await sha256Hex(token);
  await c.env.DB.prepare("INSERT INTO helper_tokens (token_hash, user_id, name, created_at) VALUES (?, ?, ?, ?)")
    .bind(hash, user.id, name, Date.now())
    .run();
  return c.json({ token, id: hash.slice(0, 12), name });
});

helper.delete("/tokens/:id", async (c) => {
  const user = currentUser(c);
  const id = c.req.param("id");
  if (!/^[0-9a-f]{12}$/.test(id)) throw new HttpError(400, "Bad helper id");
  await c.env.DB.prepare("DELETE FROM helper_tokens WHERE user_id = ? AND substr(token_hash, 1, 12) = ?").bind(user.id, id).run();
  return c.json({ ok: true });
});

// ---- the helper itself ----

const helperAuth: MiddlewareHandler<AppEnv> = async (c, next) => {
  const m = /^Bearer (sah_[A-Za-z0-9_-]{20,})$/.exec(c.req.header("Authorization") ?? "");
  if (!m) throw new HttpError(401, "Missing helper key");
  const hash = await sha256Hex(m[1]);
  const row = await c.env.DB.prepare("SELECT user_id FROM helper_tokens WHERE token_hash = ?").bind(hash).first<{ user_id: string }>();
  if (!row) throw new HttpError(401, "Unknown helper key");
  await c.env.DB.prepare("UPDATE helper_tokens SET last_seen_at = ? WHERE token_hash = ?").bind(Date.now(), hash).run();
  c.set("helperUserId", row.user_id);
  c.set("helperTokenHash", hash);
  await next();
};

helper.get("/next", helperAuth, async (c) => {
  const db = c.env.DB;
  const now = Date.now();
  // jobs a helper dropped (PC switched off mid-song) go back in the queue
  await db
    .prepare("UPDATE yt_songs SET status = 'waiting_pc', claimed_by = NULL WHERE status = 'working' AND source = 'pc' AND claimed_at < ?")
    .bind(now - STALE_CLAIM_MS)
    .run();
  const job = await db
    .prepare(
      `UPDATE yt_songs SET status = 'working', source = 'pc', claimed_by = ?1, claimed_at = ?2, stage = 'Your PC is getting the song', progress = 0.02, updated_at = ?2
       WHERE video_id = (SELECT video_id FROM yt_songs WHERE status = 'waiting_pc' AND requested_by = ?3 ORDER BY updated_at LIMIT 1)
       RETURNING video_id, title, channel, duration`,
    )
    .bind(c.get("helperTokenHash")!, now, c.get("helperUserId")!)
    .first<{ video_id: string; title: string; channel: string; duration: number }>();
  return c.json({ job: job ? { videoId: job.video_id, title: job.title, channel: job.channel, duration: job.duration } : null });
});

async function claimed(c: Ctx, videoId: string) {
  if (!isVideoId(videoId)) throw new HttpError(400, "Bad video id");
  const row = await c.env.DB
    .prepare("SELECT 1 AS mine FROM yt_songs WHERE video_id = ? AND status = 'working' AND source = 'pc' AND claimed_by = ?")
    .bind(videoId, c.get("helperTokenHash")!)
    .first();
  if (!row) throw new HttpError(409, "This helper isn't working on that song");
}

helper.post("/jobs/:videoId/progress", helperAuth, async (c) => {
  const videoId = c.req.param("videoId");
  await claimed(c, videoId);
  const body = await readJsonObject(c);
  const stage = typeof body.stage === "string" ? body.stage.slice(0, 80) : "Preparing";
  const fraction = typeof body.fraction === "number" ? Math.max(0, Math.min(1, body.fraction)) : 0;
  await setStage(c.env.DB, videoId, stage, fraction * 0.88);
  return c.json({ ok: true });
});

helper.post("/jobs/:videoId/fail", helperAuth, async (c) => {
  const videoId = c.req.param("videoId");
  await claimed(c, videoId);
  const body = await readJsonObject(c);
  const message = typeof body.message === "string" ? body.message.slice(0, 300) : "Your PC couldn't prepare this song.";
  await c.env.DB.prepare("UPDATE yt_songs SET status = 'failed', error = ?, claimed_by = NULL, updated_at = ? WHERE video_id = ?")
    .bind(message, Date.now(), videoId)
    .run();
  return c.json({ ok: true });
});

/** Light shape check of what a helper sends (it is our own code, but on someone's PC). */
export function checkAnalysis(input: unknown, videoId: string): PrepAnalysis {
  const a = input as PrepAnalysis;
  const ok =
    a && typeof a === "object" && a.videoId === videoId &&
    a.meta && typeof a.meta.title === "string" &&
    a.detect && Array.isArray(a.detect.beatTimes) && a.detect.beatTimes.length < 20000 && a.detect.beatTimes.every((t) => typeof t === "number" && Number.isFinite(t)) &&
    Array.isArray(a.detect.chords) && a.detect.chords.length < 5000 &&
    a.detect.chords.every((ch) => typeof ch.beat === "number" && typeof ch.name === "string" && ch.name.length < 40) &&
    typeof a.detect.bpm === "number" && typeof a.detect.key === "string" &&
    Array.isArray(a.chunks) && a.chunks.length <= 40 && a.chunks.every((ch) => typeof ch.start === "number" && typeof ch.mp3 === "string" && ch.mp3.length < 1_500_000) &&
    (a.engine === undefined || (Number.isInteger(a.engine) && a.engine >= 1 && a.engine < 1000));
  if (!ok) throw new HttpError(400, "The helper sent something unexpected");
  return a;
}

helper.post("/jobs/:videoId/result", helperAuth, async (c) => {
  const videoId = c.req.param("videoId");
  await claimed(c, videoId);
  const body = await readJsonObject(c, RESULT_LIMIT);
  const analysis = checkAnalysis(body.analysis, videoId);
  const songId = await finishPrep(c.env.DB, c.env.AI, analysis, (stage, fraction) => void setStage(c.env.DB, videoId, stage, fraction).catch(() => {}));
  return c.json({ songId });
});
