// /api/playlists/*: the user's own playlists (private, owner only).

import { Hono } from "hono";
import type { PlaylistSummary, SongSummary } from "../shared/api";
import { randomId } from "./crypto";
import type { AppEnv } from "./env";
import { HttpError, readJsonObject, stringField } from "./http";
import { currentUser, requireAuth } from "./session";
import { isVisible, summariesByIds } from "./songData";

const MAX_NAME = 100;
const MAX_SONGS = 1000;

export const playlists = new Hono<AppEnv>();
playlists.use("*", requireAuth);

const notFound = () => new HttpError(404, "Playlist not found");

// ?1 = user id. Counts only entries the user can still open (demo ids, own or public songs).
const SUMMARY_SQL = `SELECT p.id, p.name, p.updated_at,
  (SELECT COUNT(*) FROM playlist_songs ps LEFT JOIN songs s ON s.id = ps.song_id
   WHERE ps.playlist_id = p.id AND (ps.song_id LIKE 'demo-%' OR s.owner_id = ?1 OR s.is_public = 1)) AS count
  FROM playlists p WHERE p.user_id = ?1`;

interface PlaylistRow {
  id: string;
  name: string;
  updated_at: number;
  count: number;
}

const toSummary = (r: PlaylistRow): PlaylistSummary => ({ id: r.id, name: r.name, count: r.count, updatedAt: r.updated_at });

function checkName(body: Record<string, unknown>): string {
  const name = stringField(body, "name", "A playlist name").replace(/\s+/g, " ").trim();
  if (!name) throw new HttpError(400, "Give the playlist a name");
  if (name.length > MAX_NAME) throw new HttpError(400, `Playlist names can be at most ${MAX_NAME} characters`);
  return name;
}

async function ownPlaylist(db: D1Database, userId: string, id: string): Promise<PlaylistSummary> {
  const row = await db.prepare(`${SUMMARY_SQL} AND p.id = ?2`).bind(userId, id).first<PlaylistRow>();
  if (!row) throw notFound();
  return toSummary(row);
}

function touch(db: D1Database, id: string) {
  return db.prepare("UPDATE playlists SET updated_at = ? WHERE id = ?").bind(Date.now(), id);
}

playlists.get("/", async (c) => {
  const user = currentUser(c);
  const { results } = await c.env.DB.prepare(`${SUMMARY_SQL} ORDER BY p.updated_at DESC`).bind(user.id).all<PlaylistRow>();
  return c.json({ playlists: results.map(toSummary) });
});

playlists.post("/", async (c) => {
  const user = currentUser(c);
  const name = checkName(await readJsonObject(c));
  const id = randomId();
  const now = Date.now();
  await c.env.DB.prepare("INSERT INTO playlists (id, user_id, name, created_at, updated_at) VALUES (?, ?, ?, ?, ?)")
    .bind(id, user.id, name, now, now)
    .run();
  return c.json({ playlist: { id, name, count: 0, updatedAt: now } satisfies PlaylistSummary });
});

playlists.get("/:id", async (c) => {
  const user = currentUser(c);
  const db = c.env.DB;
  const playlist = await ownPlaylist(db, user.id, c.req.param("id"));
  const { results } = await db
    .prepare("SELECT song_id FROM playlist_songs WHERE playlist_id = ? ORDER BY position, added_at")
    .bind(playlist.id)
    .all<{ song_id: string }>();
  const summaries = await summariesByIds(db, user.id, results.map((r) => r.song_id));
  const songs = results.map((r) => summaries.get(r.song_id)).filter((s): s is SongSummary => !!s);
  return c.json({ playlist, songs });
});

playlists.patch("/:id", async (c) => {
  const user = currentUser(c);
  const db = c.env.DB;
  const playlist = await ownPlaylist(db, user.id, c.req.param("id"));
  const name = checkName(await readJsonObject(c));
  const now = Date.now();
  await db.prepare("UPDATE playlists SET name = ?, updated_at = ? WHERE id = ?").bind(name, now, playlist.id).run();
  return c.json({ playlist: { ...playlist, name, updatedAt: now } });
});

playlists.delete("/:id", async (c) => {
  const user = currentUser(c);
  const playlist = await ownPlaylist(c.env.DB, user.id, c.req.param("id"));
  await c.env.DB.prepare("DELETE FROM playlists WHERE id = ? AND user_id = ?").bind(playlist.id, user.id).run();
  return c.json({ ok: true });
});

playlists.post("/:id/songs", async (c) => {
  const user = currentUser(c);
  const db = c.env.DB;
  const playlist = await ownPlaylist(db, user.id, c.req.param("id"));
  const songId = stringField(await readJsonObject(c), "songId", "songId");
  if (!(await isVisible(db, user.id, songId))) throw new HttpError(404, "Song not found");
  const total = await db
    .prepare("SELECT COUNT(*) AS n FROM playlist_songs WHERE playlist_id = ?")
    .bind(playlist.id)
    .first<number>("n");
  if ((total ?? 0) >= MAX_SONGS) throw new HttpError(400, `A playlist can hold at most ${MAX_SONGS} songs`);
  // Appends at the end; adding a song that is already there is a no-op.
  await db.batch([
    db
      .prepare(
        `INSERT OR IGNORE INTO playlist_songs (playlist_id, song_id, position, added_at)
         SELECT ?1, ?2, COALESCE(MAX(position), -1) + 1, ?3 FROM playlist_songs WHERE playlist_id = ?1`,
      )
      .bind(playlist.id, songId, Date.now()),
    touch(db, playlist.id),
  ]);
  return c.json({ ok: true });
});

playlists.delete("/:id/songs/:songId", async (c) => {
  const user = currentUser(c);
  const db = c.env.DB;
  const playlist = await ownPlaylist(db, user.id, c.req.param("id"));
  await db.batch([
    db.prepare("DELETE FROM playlist_songs WHERE playlist_id = ? AND song_id = ?").bind(playlist.id, c.req.param("songId")),
    touch(db, playlist.id),
  ]);
  return c.json({ ok: true });
});

playlists.put("/:id/order", async (c) => {
  const user = currentUser(c);
  const db = c.env.DB;
  const playlist = await ownPlaylist(db, user.id, c.req.param("id"));
  const body = await readJsonObject(c);
  const ids = body.songIds;
  if (!Array.isArray(ids) || ids.length > MAX_SONGS || !ids.every((x) => typeof x === "string")) {
    throw new HttpError(400, "songIds must be a list of song ids");
  }
  const { results } = await db
    .prepare("SELECT song_id FROM playlist_songs WHERE playlist_id = ? ORDER BY position, added_at")
    .bind(playlist.id)
    .all<{ song_id: string }>();
  const current = results.map((r) => r.song_id);
  const present = new Set(current);
  // The listed songs first, in the given order; any not listed keep their relative order after.
  const listed = [...new Set(ids as string[])].filter((id) => present.has(id));
  const listedSet = new Set(listed);
  const order = [...listed, ...current.filter((id) => !listedSet.has(id))];
  await db.batch([
    ...order.map((songId, position) =>
      db.prepare("UPDATE playlist_songs SET position = ? WHERE playlist_id = ? AND song_id = ?").bind(position, playlist.id, songId),
    ),
    touch(db, playlist.id),
  ]);
  return c.json({ ok: true });
});
