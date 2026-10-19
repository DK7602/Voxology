// Reading songs as SongSummary / SongRecord, for stored songs and the built-in demos alike.

import type { SongRecord, SongSummary } from "../shared/api";
import { DEMO_SONGS, findDemo, type DemoSong } from "../shared/demoSongs";
import type { SongDoc } from "../shared/types";
import { distinctChords } from "../shared/validate";
import { randomId } from "./crypto";

export const isDemoId = (id: string) => id.startsWith("demo-");

export function newSongId(): string {
  for (;;) {
    const id = randomId(9);
    if (!id.toLowerCase().startsWith("demo-")) return id;
  }
}

/** A stored song is visible to the viewer bound as ?1 when they own it or it is public. */
export const VISIBLE = "(s.owner_id = ?1 OR s.is_public = 1)";

/** Summary columns of `songs s` for the viewer bound as ?1. */
export const SUMMARY_COLUMNS = `s.id, s.owner_id, s.title, s.artist, s.genre, s.song_key, s.bpm, s.chords,
  s.is_public, s.updated_at,
  json_extract(s.data, '$.media.kind') AS media_kind,
  json_extract(s.data, '$.media.videoId') AS youtube_id,
  (SELECT COALESCE(SUM(p.play_count), 0) FROM song_progress p WHERE p.song_id = s.id) AS plays,
  EXISTS (SELECT 1 FROM favorites f WHERE f.user_id = ?1 AND f.song_id = s.id) AS favorite`;

export interface SongRow {
  id: string;
  owner_id: string;
  title: string;
  artist: string;
  genre: string;
  song_key: string;
  bpm: number;
  chords: string;
  is_public: number;
  updated_at: number;
  media_kind: string | null;
  youtube_id: string | null;
  plays: number;
  favorite: number;
}

// The chords column is space separated, but names like "notes:E G A" contain spaces; inside a
// name they are stored as "_" (which chord symbols don't use).
export function encodeChords(names: string[]): string {
  return names.map((n) => n.replace(/ /g, "_")).join(" ");
}

export function decodeChords(column: string): string[] {
  return column ? column.split(" ").map((n) => n.replace(/_/g, " ")) : [];
}

function mediaKind(kind: string | null | undefined): SongSummary["mediaKind"] {
  return kind === "youtube" || kind === "audio" ? kind : "none";
}

export function rowToSummary(row: SongRow, viewerId: string): SongSummary {
  const summary: SongSummary = {
    id: row.id,
    title: row.title,
    artist: row.artist,
    genre: row.genre,
    key: row.song_key,
    bpm: row.bpm,
    chords: decodeChords(row.chords),
    isPublic: row.is_public === 1,
    isDemo: false,
    mine: row.owner_id === viewerId,
    favorite: !!row.favorite,
    plays: Number(row.plays) || 0,
    mediaKind: mediaKind(row.media_kind),
    updatedAt: row.updated_at,
  };
  if (summary.mediaKind === "youtube" && row.youtube_id) summary.youtubeId = row.youtube_id;
  return summary;
}

interface DemoStats {
  plays: Map<string, number>;
  favorites: Set<string>;
}

async function demoStats(db: D1Database, userId: string): Promise<DemoStats> {
  const [plays, favorites] = await db.batch([
    db.prepare(
      "SELECT song_id, SUM(play_count) AS plays FROM song_progress WHERE song_id LIKE 'demo-%' GROUP BY song_id",
    ),
    db.prepare("SELECT song_id FROM favorites WHERE user_id = ? AND song_id LIKE 'demo-%'").bind(userId),
  ]);
  return {
    plays: new Map((plays.results as { song_id: string; plays: number }[]).map((r) => [r.song_id, Number(r.plays)])),
    favorites: new Set((favorites.results as { song_id: string }[]).map((r) => r.song_id)),
  };
}

export function demoSummary(demo: DemoSong, stats: DemoStats): SongSummary {
  const { doc } = demo;
  const summary: SongSummary = {
    id: demo.id,
    title: doc.title,
    artist: doc.artist,
    genre: doc.genre,
    key: doc.key,
    bpm: doc.bpm,
    chords: distinctChords(doc),
    isPublic: true,
    isDemo: true,
    mine: false,
    favorite: stats.favorites.has(demo.id),
    plays: stats.plays.get(demo.id) ?? 0,
    mediaKind: doc.media.kind,
    updatedAt: 0,
  };
  if (doc.media.kind === "youtube") summary.youtubeId = doc.media.videoId;
  return summary;
}

export async function demoSummaries(db: D1Database, userId: string, demos: DemoSong[] = DEMO_SONGS): Promise<SongSummary[]> {
  if (demos.length === 0) return [];
  const stats = await demoStats(db, userId);
  return demos.map((d) => demoSummary(d, stats));
}

export async function listSongs(db: D1Database, userId: string, where: string, tail: string, ...params: unknown[]) {
  const { results } = await db
    .prepare(`SELECT ${SUMMARY_COLUMNS} FROM songs s WHERE ${where} ${tail}`)
    .bind(userId, ...params)
    .all<SongRow>();
  return results.map((r) => rowToSummary(r, userId));
}

// D1 allows at most 100 bound parameters per statement.
const ID_CHUNK = 90;

/** Summaries of the given ids that the user can see (demos included), keyed by id. */
export async function summariesByIds(db: D1Database, userId: string, ids: string[]): Promise<Map<string, SongSummary>> {
  const out = new Map<string, SongSummary>();
  const unique = [...new Set(ids)];
  const demos = unique.filter(isDemoId).map(findDemo).filter((d): d is DemoSong => !!d);
  for (const s of await demoSummaries(db, userId, demos)) out.set(s.id, s);
  const stored = unique.filter((id) => !isDemoId(id));
  for (let i = 0; i < stored.length; i += ID_CHUNK) {
    const chunk = stored.slice(i, i + ID_CHUNK);
    const placeholders = chunk.map((_, j) => `?${j + 2}`).join(", ");
    for (const s of await listSongs(db, userId, `${VISIBLE} AND s.id IN (${placeholders})`, "", ...chunk)) {
      out.set(s.id, s);
    }
  }
  return out;
}

export async function visibleSongRecord(db: D1Database, userId: string, id: string): Promise<SongRecord | null> {
  if (isDemoId(id)) {
    const demo = findDemo(id);
    if (!demo) return null;
    const [summary] = await demoSummaries(db, userId, [demo]);
    return { ...summary, doc: demo.doc };
  }
  const row = await db
    .prepare(`SELECT ${SUMMARY_COLUMNS}, s.data FROM songs s WHERE s.id = ?2 AND ${VISIBLE}`)
    .bind(userId, id)
    .first<SongRow & { data: string }>();
  if (!row) return null;
  return { ...rowToSummary(row, userId), doc: JSON.parse(row.data) as SongDoc };
}

export async function isVisible(db: D1Database, userId: string, id: string): Promise<boolean> {
  if (isDemoId(id)) return !!findDemo(id);
  const row = await db.prepare(`SELECT 1 AS ok FROM songs s WHERE s.id = ?2 AND ${VISIBLE}`).bind(userId, id).first();
  return row !== null;
}
