// /api/songs/*: song documents, favorites, practice progress, search and "similar".

import { Hono } from "hono";
import type { SongSummary } from "../shared/api";
import { DEMO_SONGS, findDemo } from "../shared/demoSongs";
import { enharmonicSpellings } from "../shared/music/chords";
import type { SongDoc } from "../shared/types";
import { distinctChords, validatePracticeSettings, validateSongDoc } from "../shared/validate";
import { ownsAudio } from "./audio";
import type { AppEnv } from "./env";
import { HttpError, readJsonObject, type Ctx } from "./http";
import { currentUser, requireAuth } from "./session";
import { jaccard, relativeChordSet } from "./similar";
import {
  decodeChords,
  demoSummaries,
  encodeChords,
  isDemoId,
  isVisible,
  listSongs,
  newSongId,
  summariesByIds,
  VISIBLE,
  visibleSongRecord,
} from "./songData";

const MAX_SONG_BYTES = 1024 * 1024;
const LIST_LIMIT = 500;
const DISCOVER_LIMIT = 200;
const SEARCH_LIMIT = 50;
const RECENT_LIMIT = 20;
const SIMILAR_LIMIT = 6;
const SIMILAR_CANDIDATES = 1000;

export const songs = new Hono<AppEnv>();
songs.use("*", requireAuth);

const notFound = () => new HttpError(404, "Song not found");

function likeEscape(s: string): string {
  return s.replace(/[\\%_]/g, (ch) => `\\${ch}`);
}

async function scopeLibrary(db: D1Database, userId: string): Promise<SongSummary[]> {
  const stored = await listSongs(
    db,
    userId,
    `s.owner_id = ?1 OR (s.is_public = 1 AND EXISTS (SELECT 1 FROM favorites f WHERE f.user_id = ?1 AND f.song_id = s.id))`,
    `ORDER BY s.updated_at DESC LIMIT ${LIST_LIMIT}`,
  );
  const demos = (await demoSummaries(db, userId)).filter((s) => s.favorite);
  return [...stored, ...demos];
}

async function scopeSearch(db: D1Database, userId: string, rawQuery: string): Promise<SongSummary[]> {
  const q = rawQuery.trim().slice(0, 100);
  if (!q) throw new HttpError(400, "Enter something to search for");
  const tokens = q.split(/\s+/).slice(0, 8);
  // each chord however a song spells it: "Bb" also finds "A#"
  const spellings = tokens.map(enharmonicSpellings);
  // Whole-token match: " G C D " contains " C " but not " Cm ".
  let param = 3;
  const chordMatch = spellings
    .map((alts) => "(" + alts.map(() => `(' ' || s.chords || ' ') LIKE ?${param++} ESCAPE '\\'`).join(" OR ") + ")")
    .join(" AND ");
  const stored = await listSongs(
    db,
    userId,
    `${VISIBLE} AND (s.title LIKE ?2 ESCAPE '\\' OR s.artist LIKE ?2 ESCAPE '\\' OR (${chordMatch}))`,
    `ORDER BY (s.owner_id = ?1) DESC, s.updated_at DESC LIMIT ${SEARCH_LIMIT}`,
    `%${likeEscape(q)}%`,
    ...spellings.flat().map((t) => `% ${likeEscape(t)} %`),
  );

  const needle = q.toLowerCase();
  const lowerSpellings = spellings.map((alts) => alts.map((t) => t.toLowerCase()));
  const demos = DEMO_SONGS.filter(({ doc }) => {
    if (doc.title.toLowerCase().includes(needle) || doc.artist.toLowerCase().includes(needle)) return true;
    const chords = new Set(encodeChords(distinctChords(doc)).toLowerCase().split(" "));
    return lowerSpellings.every((alts) => alts.some((t) => chords.has(t)));
  });
  return [...stored, ...(await demoSummaries(db, userId, demos))].slice(0, SEARCH_LIMIT);
}

async function scopeRecent(db: D1Database, userId: string): Promise<SongSummary[]> {
  const { results } = await db
    .prepare(
      `SELECT song_id, last_played_at FROM song_progress
       WHERE user_id = ? AND play_count > 0 ORDER BY last_played_at DESC LIMIT 100`,
    )
    .bind(userId)
    .all<{ song_id: string; last_played_at: number }>();
  const summaries = await summariesByIds(db, userId, results.map((r) => r.song_id));
  const out: SongSummary[] = [];
  for (const r of results) {
    const s = summaries.get(r.song_id);
    if (s) out.push({ ...s, lastPlayedAt: r.last_played_at });
    if (out.length === RECENT_LIMIT) break;
  }
  return out;
}

songs.get("/", async (c) => {
  const user = currentUser(c);
  const db = c.env.DB;
  const scope = c.req.query("scope") ?? "library";
  let list: SongSummary[];
  switch (scope) {
    case "library":
      list = await scopeLibrary(db, user.id);
      break;
    case "mine":
      list = await listSongs(db, user.id, "s.owner_id = ?1", `ORDER BY s.updated_at DESC LIMIT ${LIST_LIMIT}`);
      break;
    case "discover":
      list = [
        ...(await demoSummaries(db, user.id)),
        ...(await listSongs(db, user.id, "s.is_public = 1", `ORDER BY s.updated_at DESC LIMIT ${DISCOVER_LIMIT}`)),
      ];
      break;
    case "search":
      list = await scopeSearch(db, user.id, c.req.query("q") ?? "");
      break;
    case "recent":
      list = await scopeRecent(db, user.id);
      break;
    default:
      throw new HttpError(400, "Unknown song list");
  }
  return c.json({ songs: list });
});

/** Parses and checks a SongWriteBody, including that an audio file belongs to the user. */
async function readSongWrite(c: Ctx, userId: string): Promise<{ doc: SongDoc; isPublic?: boolean }> {
  const body = await readJsonObject(c, MAX_SONG_BYTES);
  if (body.isPublic !== undefined && typeof body.isPublic !== "boolean") {
    throw new HttpError(400, "isPublic must be true or false");
  }
  const doc = validateSongDoc(body.doc);
  if (doc.media.kind === "audio" && !(await ownsAudio(c.env.DB, userId, doc.media.key))) {
    throw new HttpError(400, "That audio file isn't in your uploads");
  }
  return { doc, isPublic: body.isPublic as boolean | undefined };
}

/** Throws unless the user owns the stored song `id` (404 if they can't see it at all). */
async function requireOwnSong(db: D1Database, userId: string, id: string): Promise<void> {
  if (isDemoId(id)) throw new HttpError(403, "Built-in songs can't be changed");
  const row = await db
    .prepare("SELECT owner_id, is_public FROM songs WHERE id = ?")
    .bind(id)
    .first<{ owner_id: string; is_public: number }>();
  if (!row || (row.owner_id !== userId && row.is_public !== 1)) throw notFound();
  if (row.owner_id !== userId) throw new HttpError(403, "You can only change your own songs");
}

async function respondWithSong(c: Ctx, userId: string, id: string) {
  const song = await visibleSongRecord(c.env.DB, userId, id);
  if (!song) throw notFound();
  return c.json({ song });
}

songs.post("/", async (c) => {
  const user = currentUser(c);
  const { doc, isPublic } = await readSongWrite(c, user.id);
  const id = newSongId();
  const now = Date.now();
  await c.env.DB.prepare(
    `INSERT INTO songs (id, owner_id, title, artist, genre, song_key, bpm, chords, is_public, data, created_at, updated_at)
     VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
  )
    .bind(id, user.id, doc.title, doc.artist, doc.genre, doc.key, doc.bpm, encodeChords(distinctChords(doc)), isPublic ? 1 : 0, JSON.stringify(doc), now, now)
    .run();
  return respondWithSong(c, user.id, id);
});

songs.get("/:id", async (c) => respondWithSong(c, currentUser(c).id, c.req.param("id")));

songs.put("/:id", async (c) => {
  const user = currentUser(c);
  const id = c.req.param("id");
  const db = c.env.DB;
  await requireOwnSong(db, user.id, id);
  const { doc, isPublic } = await readSongWrite(c, user.id);
  await db
    .prepare(
      `UPDATE songs SET title = ?, artist = ?, genre = ?, song_key = ?, bpm = ?, chords = ?,
         is_public = COALESCE(?, is_public), data = ?, updated_at = ?
       WHERE id = ? AND owner_id = ?`,
    )
    .bind(
      doc.title,
      doc.artist,
      doc.genre,
      doc.key,
      doc.bpm,
      encodeChords(distinctChords(doc)),
      isPublic === undefined ? null : isPublic ? 1 : 0,
      JSON.stringify(doc),
      Date.now(),
      id,
      user.id,
    )
    .run();
  return respondWithSong(c, user.id, id);
});

songs.delete("/:id", async (c) => {
  const user = currentUser(c);
  const id = c.req.param("id");
  const db = c.env.DB;
  await requireOwnSong(db, user.id, id);
  // favorites / playlist entries / progress hold song ids without a foreign key (they may be demos).
  await db.batch([
    db.prepare("DELETE FROM favorites WHERE song_id = ?").bind(id),
    db.prepare("DELETE FROM playlist_songs WHERE song_id = ?").bind(id),
    db.prepare("DELETE FROM song_progress WHERE song_id = ?").bind(id),
    db.prepare("DELETE FROM songs WHERE id = ? AND owner_id = ?").bind(id, user.id),
  ]);
  return c.json({ ok: true });
});

songs.get("/:id/similar", async (c) => {
  const user = currentUser(c);
  const id = c.req.param("id");
  const db = c.env.DB;

  let target: { key: string; chords: string[] } | null = null;
  const demo = findDemo(id);
  if (demo) {
    target = { key: demo.doc.key, chords: distinctChords(demo.doc) };
  } else if (!isDemoId(id)) {
    const row = await db
      .prepare(`SELECT s.song_key, s.chords FROM songs s WHERE s.id = ?2 AND ${VISIBLE}`)
      .bind(user.id, id)
      .first<{ song_key: string; chords: string }>();
    if (row) target = { key: row.song_key, chords: decodeChords(row.chords) };
  }
  if (!target) throw notFound();

  const targetSet = relativeChordSet(target.key, target.chords);
  const { results } = await db
    .prepare(
      `SELECT s.id, s.song_key, s.chords FROM songs s WHERE ${VISIBLE} AND s.id != ?2
       ORDER BY s.updated_at DESC LIMIT ${SIMILAR_CANDIDATES}`,
    )
    .bind(user.id, id)
    .all<{ id: string; song_key: string; chords: string }>();
  const candidates = [
    ...results.map((r) => ({ id: r.id, key: r.song_key, chords: decodeChords(r.chords) })),
    ...DEMO_SONGS.filter((d) => d.id !== id).map((d) => ({ id: d.id, key: d.doc.key, chords: distinctChords(d.doc) })),
  ];
  const ranked = candidates
    .map((cand) => ({ id: cand.id, score: jaccard(targetSet, relativeChordSet(cand.key, cand.chords)) }))
    .filter((r) => r.score > 0)
    .sort((a, b) => b.score - a.score)
    .slice(0, SIMILAR_LIMIT);
  const summaries = await summariesByIds(db, user.id, ranked.map((r) => r.id));
  return c.json({ songs: ranked.map((r) => summaries.get(r.id)).filter((s): s is SongSummary => !!s) });
});

songs.put("/:id/favorite", async (c) => {
  const user = currentUser(c);
  const id = c.req.param("id");
  if (!(await isVisible(c.env.DB, user.id, id))) throw notFound();
  await c.env.DB.prepare("INSERT OR IGNORE INTO favorites (user_id, song_id, created_at) VALUES (?, ?, ?)")
    .bind(user.id, id, Date.now())
    .run();
  return c.json({ favorite: true });
});

songs.delete("/:id/favorite", async (c) => {
  const user = currentUser(c);
  await c.env.DB.prepare("DELETE FROM favorites WHERE user_id = ? AND song_id = ?").bind(user.id, c.req.param("id")).run();
  return c.json({ favorite: false });
});

songs.get("/:id/progress", async (c) => {
  const user = currentUser(c);
  const id = c.req.param("id");
  if (!(await isVisible(c.env.DB, user.id, id))) throw notFound();
  const row = await c.env.DB.prepare("SELECT settings, play_count FROM song_progress WHERE user_id = ? AND song_id = ?")
    .bind(user.id, id)
    .first<{ settings: string; play_count: number }>();
  let settings = {};
  try {
    settings = validatePracticeSettings(row ? JSON.parse(row.settings) : {});
  } catch {
    settings = {};
  }
  return c.json({ settings, plays: row?.play_count ?? 0 });
});

songs.put("/:id/progress", async (c) => {
  const user = currentUser(c);
  const id = c.req.param("id");
  const body = await readJsonObject(c);
  const hasSettings = body.settings !== undefined;
  const settings = validatePracticeSettings(body.settings);
  if (body.played !== undefined && typeof body.played !== "boolean") {
    throw new HttpError(400, "played must be true or false");
  }
  const played = body.played === true;
  if (!(await isVisible(c.env.DB, user.id, id))) throw notFound();
  const now = Date.now();
  // Without `settings` only the play count changes; last_played_at stays 0 until a first play.
  await c.env.DB.prepare(
    `INSERT INTO song_progress (user_id, song_id, settings, play_count, last_played_at) VALUES (?1, ?2, ?3, ?4, ?5)
     ON CONFLICT (user_id, song_id) DO UPDATE SET
       settings = CASE WHEN ?6 = 1 THEN excluded.settings ELSE song_progress.settings END,
       play_count = song_progress.play_count + excluded.play_count,
       last_played_at = MAX(song_progress.last_played_at, excluded.last_played_at)`,
  )
    .bind(user.id, id, JSON.stringify(settings), played ? 1 : 0, played ? now : 0, hasSettings ? 1 : 0)
    .run();
  return c.json({ ok: true });
});
