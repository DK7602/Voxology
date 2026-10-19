import { useEffect, useState, type FormEvent } from "react";
import { Link, useNavigate, useSearchParams } from "react-router-dom";
import type { SongSummary } from "../../shared/api";
import type { YouTubeResult } from "../../shared/prep";
import { api } from "../api";
import { IconCheck, IconSearch } from "../components/Icons";
import { errorText } from "../components/ui";
import { SongCard } from "./LibraryPages";

function fmt(sec: number): string {
  if (!sec) return "";
  const m = Math.floor(sec / 60);
  return `${m}:${String(Math.floor(sec % 60)).padStart(2, "0")}`;
}

export function prepareLink(r: YouTubeResult): string {
  if (r.songId) return `/song/${r.songId}`;
  const p = new URLSearchParams({ t: r.title, c: r.channel, d: String(r.duration) });
  return `/yt/${r.videoId}?${p}`;
}

/** Search box used on Home and the search page. */
export function SongSearchBox({ initial = "", big = false }: { initial?: string; big?: boolean }) {
  const navigate = useNavigate();
  const [q, setQ] = useState(initial);
  useEffect(() => setQ(initial), [initial]);
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (q.trim()) navigate(`/search?q=${encodeURIComponent(q.trim())}`);
  };
  return (
    <form className={"song-search" + (big ? " big" : "")} role="search" onSubmit={submit}>
      <IconSearch size={big ? 26 : 22} />
      <input value={q} onChange={(e) => setQ(e.target.value)} placeholder="Search any song or artist…" aria-label="Search any song" autoFocus={big} />
      <button className="btn btn-primary" type="submit">Search</button>
    </form>
  );
}

export function SearchPage() {
  const [params] = useSearchParams();
  const q = params.get("q") ?? "";
  const [mine, setMine] = useState<SongSummary[] | null>(null);
  const [yt, setYt] = useState<YouTubeResult[] | null>(null);
  const [ytError, setYtError] = useState<string | null>(null);

  useEffect(() => {
    if (!q) return;
    let live = true;
    setMine(null);
    setYt(null);
    setYtError(null);
    api.songs("search", q).then((r) => live && setMine(r.songs)).catch(() => live && setMine([]));
    api.ytSearch(q).then((r) => live && setYt(r.results)).catch((e) => live && setYtError(errorText(e)));
    return () => {
      live = false;
    };
  }, [q]);

  return (
    <div className="panel page-panel">
      <div className="page-head" style={{ display: "block" }}>
        <SongSearchBox initial={q} />
      </div>
      {!q && <div className="empty">Type a song or an artist. Pick one, and SongAlong works out the chords and lyrics for you.</div>}

      {q && mine && mine.length > 0 && (
        <>
          <div className="section-title">
            <h2>Ready to play</h2>
          </div>
          <div className="grid-cards">
            {mine.slice(0, 8).map((s) => (
              <SongCard key={s.id} s={s} />
            ))}
          </div>
        </>
      )}

      {q && (
        <>
          <div className="section-title">
            <h2>Songs</h2>
            <span className="muted" style={{ fontSize: 15 }}>Pick one: chords and lyrics are ready in about half a minute</span>
          </div>
          {ytError && <div className="notice error">{ytError}</div>}
          {!yt && !ytError && <div className="empty">Searching…</div>}
          {yt && yt.length === 0 && <div className="empty">No songs found. Try the artist and title.</div>}
          <div className="grid-cards">
            {yt?.map((r) => (
              <Link key={r.videoId} to={prepareLink(r)} className="song-card yt-card">
                <div className="artwork" style={{ borderRadius: 10 }}>
                  <img src={r.thumbnail} alt="" loading="lazy" />
                  {r.duration > 0 && <span className="yt-duration">{fmt(r.duration)}</span>}
                </div>
                {r.status === "ready" && (
                  <div className="card-badges">
                    <span className="badge">
                      <IconCheck size={12} /> Ready
                    </span>
                  </div>
                )}
                <div>
                  <h3>{r.title}</h3>
                  <div className="meta">{r.channel}</div>
                </div>
              </Link>
            ))}
          </div>
        </>
      )}
    </div>
  );
}
