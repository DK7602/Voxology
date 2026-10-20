// Typed client for the Worker API (see shared/api.ts for the contract).
import type {
  AudioFile,
  LoginBody,
  PlaylistSummary,
  PracticeSettings,
  RegisterBody,
  SongDoc,
  SongRecord,
  SongSummary,
  User,
  UserSettings,
} from "../shared/api";
import type { PrepareEvent, PrepStatus, YouTubeResult } from "../shared/prep";

export interface HelperKey {
  id: string;
  name: string;
  createdAt: number;
  lastSeenAt: number | null;
}

export class ApiError extends Error {
  status: number;
  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

async function request<T>(method: string, path: string, body?: unknown): Promise<T> {
  const init: RequestInit = { method, credentials: "same-origin", headers: {} };
  if (body !== undefined) {
    (init.headers as Record<string, string>)["Content-Type"] = "application/json";
    init.body = JSON.stringify(body);
  }
  let res: Response;
  try {
    res = await fetch("/api" + path, init);
  } catch {
    throw new ApiError(0, "Can't reach SongAlong. Check your connection and try again.");
  }
  const text = await res.text();
  let data: unknown = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    /* not JSON */
  }
  if (!res.ok) {
    const msg = (data as { error?: string } | null)?.error ?? `Something went wrong (${res.status}).`;
    throw new ApiError(res.status, msg);
  }
  return data as T;
}

export type SongScope = "library" | "mine" | "discover" | "search" | "recent";

export const api = {
  // auth
  me: () => request<{ user: User }>("GET", "/auth/me"),
  register: (b: RegisterBody) => request<{ user: User }>("POST", "/auth/register", b),
  login: (b: LoginBody) => request<{ user: User }>("POST", "/auth/login", b),
  logout: () => request<{ ok: true }>("POST", "/auth/logout"),
  forgot: (email: string) => request<{ ok: true }>("POST", "/auth/forgot", { email }),
  reset: (token: string, password: string) => request<{ user: User }>("POST", "/auth/reset", { token, password }),
  changePassword: (currentPassword: string, newPassword: string) =>
    request<{ ok: true }>("POST", "/auth/password", { currentPassword, newPassword }),
  updateMe: (b: { name?: string; settings?: UserSettings }) => request<{ user: User }>("PATCH", "/auth/me", b),
  deleteAccount: (password: string, confirm?: string) => request<{ ok: true }>("POST", "/auth/delete", { password, confirm }),

  // songs
  songs: (scope: SongScope, q?: string) =>
    request<{ songs: SongSummary[] }>("GET", `/songs?scope=${scope}${q ? "&q=" + encodeURIComponent(q) : ""}`),
  song: (id: string) => request<{ song: SongRecord }>("GET", `/songs/${encodeURIComponent(id)}`),
  createSong: (doc: SongDoc, isPublic = false) => request<{ song: SongRecord }>("POST", "/songs", { doc, isPublic }),
  updateSong: (id: string, doc: SongDoc, isPublic?: boolean) =>
    request<{ song: SongRecord }>("PUT", `/songs/${encodeURIComponent(id)}`, { doc, isPublic }),
  deleteSong: (id: string) => request<{ ok: true }>("DELETE", `/songs/${encodeURIComponent(id)}`),
  similar: (id: string) => request<{ songs: SongSummary[] }>("GET", `/songs/${encodeURIComponent(id)}/similar`),
  /** take a song off Home's "Keep playing" row or Discover (on = false puts it back) */
  hideSong: (id: string, list: "recent" | "discover", on = true) =>
    request<{ hidden: boolean }>(on ? "PUT" : "DELETE", `/songs/${encodeURIComponent(id)}/hidden/${list}`),
  favorite: (id: string, on: boolean) =>
    request<{ favorite: boolean }>(on ? "PUT" : "DELETE", `/songs/${encodeURIComponent(id)}/favorite`),
  progress: (id: string) =>
    request<{ settings: PracticeSettings; plays: number }>("GET", `/songs/${encodeURIComponent(id)}/progress`),
  saveProgress: (id: string, settings: PracticeSettings, played = false) =>
    request<{ ok: true }>("PUT", `/songs/${encodeURIComponent(id)}/progress`, { settings, played }),

  // playlists
  playlists: () => request<{ playlists: PlaylistSummary[] }>("GET", "/playlists"),
  createPlaylist: (name: string) => request<{ playlist: PlaylistSummary }>("POST", "/playlists", { name }),
  playlist: (id: string) =>
    request<{ playlist: PlaylistSummary; songs: SongSummary[] }>("GET", `/playlists/${encodeURIComponent(id)}`),
  renamePlaylist: (id: string, name: string) =>
    request<{ playlist: PlaylistSummary }>("PATCH", `/playlists/${encodeURIComponent(id)}`, { name }),
  deletePlaylist: (id: string) => request<{ ok: true }>("DELETE", `/playlists/${encodeURIComponent(id)}`),
  addToPlaylist: (id: string, songId: string) =>
    request<{ ok: true }>("POST", `/playlists/${encodeURIComponent(id)}/songs`, { songId }),
  removeFromPlaylist: (id: string, songId: string) =>
    request<{ ok: true }>("DELETE", `/playlists/${encodeURIComponent(id)}/songs/${encodeURIComponent(songId)}`),
  reorderPlaylist: (id: string, songIds: string[]) =>
    request<{ ok: true }>("PUT", `/playlists/${encodeURIComponent(id)}/order`, { songIds }),

  // search any song / prepare it
  ytSearch: (q: string) => request<{ results: YouTubeResult[] }>("GET", `/youtube/search?q=${encodeURIComponent(q)}`),
  prepStatus: (videoId: string) => request<PrepStatus>("GET", `/youtube/status/${encodeURIComponent(videoId)}`),
  helpers: () => request<{ helpers: HelperKey[] }>("GET", "/helper/tokens"),
  createHelper: (name: string) => request<{ token: string; id: string; name: string }>("POST", "/helper/tokens", { name }),
  deleteHelper: (id: string) => request<{ ok: true }>("DELETE", `/helper/tokens/${encodeURIComponent(id)}`),

  // audio
  audioFiles: () => request<{ files: AudioFile[] }>("GET", "/audio"),
  deleteAudio: (key: string) => request<{ ok: true }>("DELETE", `/audio/${encodeURIComponent(key)}`),
};

export function audioUrl(key: string): string {
  return `/api/audio/${encodeURIComponent(key)}`;
}

/** Uploads an audio file with progress (fetch can't report upload progress, XHR can). */
export function uploadAudio(file: File, onProgress?: (fraction: number) => void): Promise<AudioFile> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/audio");
    xhr.withCredentials = true;
    xhr.setRequestHeader("Content-Type", file.type || "application/octet-stream");
    xhr.setRequestHeader("X-File-Name", encodeURIComponent(file.name));
    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) onProgress?.(e.loaded / e.total);
    };
    xhr.onload = () => {
      let data: { audio?: AudioFile; error?: string } = {};
      try {
        data = JSON.parse(xhr.responseText);
      } catch {
        /* ignore */
      }
      if (xhr.status >= 200 && xhr.status < 300 && data.audio) resolve(data.audio);
      else reject(new ApiError(xhr.status, data.error ?? `Upload failed (${xhr.status}).`));
    };
    xhr.onerror = () => reject(new ApiError(0, "Upload failed. Check your connection."));
    xhr.send(file);
  });
}

/** Starts preparing a YouTube song and calls `onEvent` for each progress line. Resolves with the
 *  last event (ready / waiting_pc / error), or null if the connection dropped before the end. */
export async function prepareSong(
  body: { videoId: string; title?: string; channel?: string; duration?: number; retry?: boolean },
  onEvent: (e: PrepareEvent) => void,
  signal?: AbortSignal,
): Promise<PrepareEvent | null> {
  const res = await fetch("/api/youtube/prepare", {
    method: "POST",
    credentials: "same-origin",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
    signal,
  });
  if (!res.ok || !res.body) {
    let msg = `Couldn't start (${res.status}).`;
    try {
      msg = ((await res.json()) as { error?: string }).error ?? msg;
    } catch {
      /* not JSON */
    }
    throw new ApiError(res.status, msg);
  }
  const reader = res.body.getReader();
  const dec = new TextDecoder();
  let buf = "";
  let last: PrepareEvent | null = null;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (value) buf += dec.decode(value, { stream: true });
      let nl: number;
      while ((nl = buf.indexOf("\n")) >= 0) {
        const line = buf.slice(0, nl).trim();
        buf = buf.slice(nl + 1);
        if (!line) continue;
        const e = JSON.parse(line) as PrepareEvent;
        onEvent(e);
        if (e.type !== "progress") last = e;
      }
      if (done) break;
    }
  } catch (e) {
    if ((e as Error).name === "AbortError") throw e;
    return last; // connection dropped: the caller falls back to polling the status
  }
  return last;
}
