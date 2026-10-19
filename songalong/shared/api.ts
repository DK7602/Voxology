// The HTTP API between the app and the Worker. All routes live under /api, take and return JSON
// (except audio upload / download) and use the `sa_session` HttpOnly cookie for the signed-in user.
// Errors: non-2xx status with body { error: string }.

import type { PracticeSettings, SongDoc, UserSettings } from "./types";

export interface User {
  id: string;
  email: string;
  name: string;
  settings: UserSettings;
  createdAt: number;
  /** false for accounts made with Apple / Google / Facebook that haven't set a password */
  hasPassword: boolean;
}

export interface SongSummary {
  id: string;
  title: string;
  artist: string;
  genre: string;
  key: string;
  bpm: number;
  /** distinct chord names in order of first use */
  chords: string[];
  isPublic: boolean;
  /** built-in example song (id starts with "demo-") */
  isDemo: boolean;
  /** the signed-in user owns it */
  mine: boolean;
  /** the signed-in user saved (hearted) it */
  favorite: boolean;
  /** total plays across all users ("jam sessions") */
  plays: number;
  mediaKind: "none" | "youtube" | "audio";
  youtubeId?: string;
  updatedAt: number;
  /** only in scope=recent */
  lastPlayedAt?: number;
}

export interface SongRecord extends SongSummary {
  doc: SongDoc;
}

export interface PlaylistSummary {
  id: string;
  name: string;
  count: number;
  updatedAt: number;
}

// ---- auth ----
// POST /api/auth/register   RegisterBody            -> { user: User }        (signs in)
// POST /api/auth/login      LoginBody               -> { user: User }
// POST /api/auth/logout                             -> { ok: true }
// GET  /api/auth/me                                 -> { user: User } | 401
// PATCH /api/auth/me        { name?, settings? }    -> { user: User }
// POST /api/auth/forgot     { email }               -> { ok: true }         (always, no account leak)
// POST /api/auth/reset      { token, password }     -> { user: User }        (signs in, ends other sessions)
// POST /api/auth/password   { currentPassword, newPassword } -> { ok: true } (ends other sessions)
// POST /api/auth/delete     { password }            -> { ok: true }         (deletes account + data)
export interface RegisterBody {
  email: string;
  password: string;
  name: string;
}
export interface LoginBody {
  email: string;
  password: string;
  remember?: boolean;
}

// ---- songs ----
// GET    /api/songs?scope=library|mine|discover|search|recent&q=...  -> { songs: SongSummary[] }
//        library  = my songs + songs I saved; mine = my songs; discover = demos + public songs;
//        search   = title / artist / chord match over mine + saved + public + demos (q required)
//        recent   = songs I played, newest first (limit 20), with lastPlayedAt
// GET    /api/songs/:id                -> { song: SongRecord }   (mine, public or demo)
// POST   /api/songs     SongWriteBody  -> { song: SongRecord }
// PUT    /api/songs/:id SongWriteBody  -> { song: SongRecord }   (owner only)
// DELETE /api/songs/:id                -> { ok: true }           (owner only)
// GET    /api/songs/:id/similar        -> { songs: SongSummary[] } (shared chords, up to 6)
// PUT    /api/songs/:id/favorite       -> { favorite: true }
// DELETE /api/songs/:id/favorite       -> { favorite: false }
// GET    /api/songs/:id/progress       -> { settings: PracticeSettings, plays: number }
// PUT    /api/songs/:id/progress  { settings: PracticeSettings, played?: boolean } -> { ok: true }
export interface SongWriteBody {
  doc: SongDoc;
  isPublic?: boolean;
}
export interface ProgressBody {
  settings: PracticeSettings;
  played?: boolean;
}

// ---- playlists ----
// GET    /api/playlists                         -> { playlists: PlaylistSummary[] }
// POST   /api/playlists        { name }         -> { playlist: PlaylistSummary }
// GET    /api/playlists/:id                     -> { playlist: PlaylistSummary, songs: SongSummary[] }
// PATCH  /api/playlists/:id    { name }         -> { playlist: PlaylistSummary }
// DELETE /api/playlists/:id                     -> { ok: true }
// POST   /api/playlists/:id/songs { songId }    -> { ok: true }
// DELETE /api/playlists/:id/songs/:songId       -> { ok: true }
// PUT    /api/playlists/:id/order { songIds }   -> { ok: true }

// ---- audio (your own uploaded recordings, private to you) ----
// POST   /api/audio   raw file body; headers Content-Type: audio/*, X-File-Name: <url-encoded name>
//                     -> { audio: AudioFile }   (max 100 MB)
// GET    /api/audio                              -> { files: AudioFile[] }
// GET    /api/audio/:key   the file, with HTTP Range support (owner only)
// DELETE /api/audio/:key                         -> { ok: true }
export interface AudioFile {
  key: string;
  name: string;
  size: number;
  contentType: string;
  createdAt: number;
}

export type { PracticeSettings, SongDoc, UserSettings };

export const PASSWORD_MIN = 8;
export const PASSWORD_MAX = 200;
