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
  deleteAccount: (password: string) => request<{ ok: true }>("POST", "/auth/delete", { password }),

  // songs
  songs: (scope: SongScope, q?: string) =>
    request<{ songs: SongSummary[] }>("GET", `/songs?scope=${scope}${q ? "&q=" + encodeURIComponent(q) : ""}`),
  song: (id: string) => request<{ song: SongRecord }>("GET", `/songs/${encodeURIComponent(id)}`),
  createSong: (doc: SongDoc, isPublic = false) => request<{ song: SongRecord }>("POST", "/songs", { doc, isPublic }),
  updateSong: (id: string, doc: SongDoc, isPublic?: boolean) =>
    request<{ song: SongRecord }>("PUT", `/songs/${encodeURIComponent(id)}`, { doc, isPublic }),
  deleteSong: (id: string) => request<{ ok: true }>("DELETE", `/songs/${encodeURIComponent(id)}`),
  similar: (id: string) => request<{ songs: SongSummary[] }>("GET", `/songs/${encodeURIComponent(id)}/similar`),
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
