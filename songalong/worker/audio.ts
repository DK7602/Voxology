// /api/audio/*: the user's uploaded recordings in R2, private to them.

import { Hono } from "hono";
import type { AudioFile } from "../shared/api";
import { randomId } from "./crypto";
import type { AppEnv, Env } from "./env";
import { HttpError } from "./http";
import { currentUser, requireAuth } from "./session";

export const MAX_AUDIO_BYTES = 100 * 1024 * 1024;

const EXTENSION_TYPES: Record<string, string> = {
  mp3: "audio/mpeg",
  m4a: "audio/mp4",
  mp4: "audio/mp4",
  aac: "audio/aac",
  wav: "audio/wav",
  ogg: "audio/ogg",
  oga: "audio/ogg",
  opus: "audio/ogg",
  flac: "audio/flac",
  webm: "audio/webm",
  weba: "audio/webm",
  aif: "audio/aiff",
  aiff: "audio/aiff",
};

interface AudioRow {
  key: string;
  owner_id: string;
  name: string;
  content_type: string;
  size: number;
  created_at: number;
}

const toAudioFile = (r: AudioRow): AudioFile => ({
  key: r.key,
  name: r.name,
  size: r.size,
  contentType: r.content_type,
  createdAt: r.created_at,
});

function fileName(header: string | undefined): string {
  let name = header ?? "";
  try {
    name = decodeURIComponent(name);
  } catch {
    // keep it as sent
  }
  name = name.replace(/[\u0000-\u001f\u007f/\\]+/g, " ").trim().slice(0, 200);
  return name || "Recording";
}

/** The type to store: audio/* as sent, or one guessed from the extension for octet-stream. */
function audioContentType(header: string | undefined, name: string): string {
  const type = (header ?? "").split(";")[0].trim().toLowerCase();
  if (/^audio\/[a-z0-9.+-]+$/.test(type)) return type;
  if (type === "application/octet-stream" || type === "") {
    const ext = /\.([a-z0-9]+)$/i.exec(name)?.[1]?.toLowerCase();
    if (ext && EXTENSION_TYPES[ext]) return EXTENSION_TYPES[ext];
  }
  throw new HttpError(415, "Only audio files can be uploaded");
}

export type ByteRange = { offset: number; length: number };

/** Parses a single `Range: bytes=...` header. null = serve the whole file (absent, malformed
 *  or multi-range, all of which RFC 9110 lets us ignore). */
export function parseRange(header: string | undefined, size: number): ByteRange | "unsatisfiable" | null {
  if (!header) return null;
  const m = /^bytes=(\d*)-(\d*)$/.exec(header.trim());
  if (!m || (m[1] === "" && m[2] === "")) return null;
  if (m[1] === "") {
    const suffix = Number(m[2]);
    if (suffix === 0 || size === 0) return "unsatisfiable";
    const length = Math.min(suffix, size);
    return { offset: size - length, length };
  }
  const start = Number(m[1]);
  if (start >= size) return "unsatisfiable";
  const end = m[2] === "" ? size - 1 : Math.min(Number(m[2]), size - 1);
  if (end < start) return null;
  return { offset: start, length: end - start + 1 };
}

export async function ownsAudio(db: D1Database, userId: string, key: string): Promise<boolean> {
  const row = await db.prepare("SELECT 1 AS ok FROM audio_files WHERE key = ? AND owner_id = ?").bind(key, userId).first();
  return row !== null;
}

async function ownAudioRow(db: D1Database, userId: string, key: string): Promise<AudioRow> {
  const row = await db
    .prepare("SELECT key, owner_id, name, content_type, size, created_at FROM audio_files WHERE key = ? AND owner_id = ?")
    .bind(key, userId)
    .first<AudioRow>();
  if (!row) throw new HttpError(404, "Audio file not found");
  return row;
}

/** Removes every R2 object of a user (recorded ones and any strays under their prefix). */
export async function deleteAllAudio(env: Env, userId: string): Promise<void> {
  const { results } = await env.DB.prepare("SELECT key FROM audio_files WHERE owner_id = ?").bind(userId).all<{ key: string }>();
  const keys = new Set(results.map((r) => r.key));
  let cursor: string | undefined;
  do {
    const page = await env.AUDIO.list({ prefix: `u/${userId}/`, cursor });
    for (const obj of page.objects) keys.add(obj.key);
    cursor = page.truncated ? page.cursor : undefined;
  } while (cursor);
  const all = [...keys];
  for (let i = 0; i < all.length; i += 1000) await env.AUDIO.delete(all.slice(i, i + 1000));
}

export const audio = new Hono<AppEnv>();
audio.use("*", requireAuth);

audio.post("/", async (c) => {
  const user = currentUser(c);
  const declared = c.req.header("Content-Length");
  if (declared === undefined) throw new HttpError(411, "The upload needs a Content-Length header");
  const size = Number(declared);
  if (!Number.isInteger(size) || size < 0) throw new HttpError(400, "Invalid Content-Length");
  if (size > MAX_AUDIO_BYTES) throw new HttpError(413, "Audio files can be at most 100 MB");
  const body = c.req.raw.body;
  if (size === 0 || !body) throw new HttpError(400, "The file is empty");
  const name = fileName(c.req.header("X-File-Name"));
  const contentType = audioContentType(c.req.header("Content-Type"), name);

  const key = `u/${user.id}/${randomId()}`;
  // Streams straight into R2; the known Content-Length makes this a fixed-length upload.
  const object = await c.env.AUDIO.put(key, body, { httpMetadata: { contentType } });
  const row: AudioRow = { key, owner_id: user.id, name, content_type: contentType, size: object?.size ?? size, created_at: Date.now() };
  try {
    await c.env.DB.prepare("INSERT INTO audio_files (key, owner_id, name, content_type, size, created_at) VALUES (?, ?, ?, ?, ?, ?)")
      .bind(row.key, row.owner_id, row.name, row.content_type, row.size, row.created_at)
      .run();
  } catch (err) {
    await c.env.AUDIO.delete(key).catch(() => {});
    throw err;
  }
  return c.json({ audio: toAudioFile(row) });
});

audio.get("/", async (c) => {
  const user = currentUser(c);
  const { results } = await c.env.DB.prepare(
    "SELECT key, owner_id, name, content_type, size, created_at FROM audio_files WHERE owner_id = ? ORDER BY created_at DESC",
  )
    .bind(user.id)
    .all<AudioRow>();
  return c.json({ files: results.map(toAudioFile) });
});

// The key contains slashes; it may arrive url-encoded (one segment) or raw (several segments).
audio.get("/:key{.+}", async (c) => {
  const user = currentUser(c);
  const row = await ownAudioRow(c.env.DB, user.id, c.req.param("key"));
  const headers = new Headers({
    "Content-Type": row.content_type,
    "Accept-Ranges": "bytes",
    "Cache-Control": "private, max-age=3600",
    "Content-Disposition": `inline; filename*=UTF-8''${encodeURIComponent(row.name)}`,
  });
  const range = parseRange(c.req.header("Range"), row.size);
  if (range === "unsatisfiable") {
    headers.set("Content-Range", `bytes */${row.size}`);
    return new Response(null, { status: 416, headers });
  }
  const object = await c.env.AUDIO.get(row.key, range ? { range } : undefined);
  if (!object) throw new HttpError(404, "Audio file not found");
  headers.set("ETag", object.httpEtag);
  if (range) {
    headers.set("Content-Range", `bytes ${range.offset}-${range.offset + range.length - 1}/${row.size}`);
    headers.set("Content-Length", String(range.length));
    return new Response(object.body, { status: 206, headers });
  }
  headers.set("Content-Length", String(row.size));
  return new Response(object.body, { status: 200, headers });
});

audio.delete("/:key{.+}", async (c) => {
  const user = currentUser(c);
  const row = await ownAudioRow(c.env.DB, user.id, c.req.param("key"));
  await c.env.AUDIO.delete(row.key);
  await c.env.DB.prepare("DELETE FROM audio_files WHERE key = ? AND owner_id = ?").bind(row.key, user.id).run();
  return c.json({ ok: true });
});
