import { useEffect, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";
import type { PlaylistSummary, SongSummary } from "../../shared/api";
import { prettyAccidentals } from "../../shared/music/notes";
import { api, type SongScope } from "../api";
import { useUser } from "../auth";
import { Artwork } from "../components/Artwork";
import {
  IconCompass,
  IconHeart,
  IconMusicFile,
  IconPiano,
  IconPlus,
  IconTrash,
  IconTuner,
  IconYoutube,
} from "../components/Icons";
import { errorText, useToast } from "../components/ui";
import { SongSearchBox } from "./SearchPage";

export function SongCard({ s }: { s: SongSummary }) {
  return (
    <Link to={`/song/${s.id}`} className="song-card">
      <Artwork title={s.title} youtubeId={s.youtubeId} rounded={10} />
      <div className="card-badges">
        {s.isDemo && <span className="badge">Example</span>}
        {s.favorite && (
          <span className="badge">
            <IconHeart filled size={12} /> Saved
          </span>
        )}
        {s.mine && !s.isPublic && <span className="badge">Private</span>}
      </div>
      <div>
        <h3>{s.title || "Untitled"}</h3>
        <div className="meta">
          {s.artist || "Unknown artist"}
          {s.key && ` · ${prettyAccidentals(s.key)}`}
          {s.bpm ? ` · ${Math.round(s.bpm)} BPM` : ""}
        </div>
      </div>
      <div className="chords-line">{s.chords.slice(0, 8).map(prettyAccidentals).join("  ")}</div>
    </Link>
  );
}

function useSongs(scope: SongScope, q?: string) {
  const [songs, setSongs] = useState<SongSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    let live = true;
    setSongs(null);
    api
      .songs(scope, q)
      .then((r) => live && setSongs(r.songs))
      .catch((e) => live && setError(errorText(e)));
    return () => {
      live = false;
    };
  }, [scope, q]);
  return { songs, error };
}

function SongGrid({ songs, error, empty }: { songs: SongSummary[] | null; error: string | null; empty: React.ReactNode }) {
  if (error) return <div className="notice error">{error}</div>;
  if (!songs) return <div className="empty">Loading…</div>;
  if (!songs.length) return <div className="empty">{empty}</div>;
  return (
    <div className="grid-cards">
      {songs.map((s) => (
        <SongCard key={s.id} s={s} />
      ))}
    </div>
  );
}

export function HomePage() {
  const user = useUser();
  const recent = useSongs("recent");
  const discover = useSongs("discover");
  const navigate = useNavigate();
  const first = user.name.split(" ")[0];
  return (
    <div className="panel page-panel">
      <section className="hero">
        <div>
          <h1>{first ? `Welcome back, ${first}` : "Welcome to SongAlong"}</h1>
          <p>Search any song, tap play, and the chords and lyrics scroll in time: guitar, ukulele, piano or mandolin, any key, any capo.</p>
          <SongSearchBox big />
          <div style={{ display: "flex", gap: 10, flexWrap: "wrap", marginTop: 14 }}>
            <Link className="btn btn-primary" to="/new">
              <IconPlus size={20} /> Add a song
            </Link>
            <Link className="btn" to="/discover">
              <IconCompass size={20} /> Try an example
            </Link>
          </div>
        </div>
        <div className="quick-actions">
          <button className="quick-action" onClick={() => navigate("/new?from=audio")}>
            <IconMusicFile size={30} />
            <b>From a recording</b>
            <span>Upload an MP3: chords & beat found for you</span>
          </button>
          <button className="quick-action" onClick={() => navigate("/new?from=youtube")}>
            <IconYoutube size={30} />
            <b>From YouTube</b>
            <span>Play along with a video</span>
          </button>
          <button className="quick-action" onClick={() => navigate("/new?from=sheet")}>
            <IconPiano size={30} />
            <b>Paste a chord sheet</b>
            <span>Chords over lyrics or ChordPro</span>
          </button>
          <button className="quick-action" onClick={() => navigate("/tools/tuner")}>
            <IconTuner size={30} />
            <b>Tune up</b>
            <span>Guitar, ukulele, mandolin</span>
          </button>
        </div>
      </section>

      {recent.songs && recent.songs.length > 0 && (
        <>
          <div className="section-title">
            <h2>Keep playing</h2>
          </div>
          <SongGrid songs={recent.songs.slice(0, 8)} error={recent.error} empty="" />
        </>
      )}
      <div className="section-title">
        <h2>Discover</h2>
        <Link to="/discover">See all</Link>
      </div>
      <SongGrid songs={discover.songs?.slice(0, 8) ?? null} error={discover.error} empty="Nothing here yet." />
    </div>
  );
}

export function DiscoverPage() {
  const { songs, error } = useSongs("discover");
  return (
    <div className="panel page-panel">
      <div className="page-head">
        <h1>Discover</h1>
        <span className="muted">Example songs and songs other players made public</span>
      </div>
      <SongGrid songs={songs} error={error} empty="No public songs yet." />
    </div>
  );
}

export function LibraryPage() {
  const [filter, setFilter] = useState<"library" | "mine">("library");
  const { songs, error } = useSongs(filter);
  const [onlySaved, setOnlySaved] = useState(false);
  const shown = songs && onlySaved ? songs.filter((s) => s.favorite) : songs;
  return (
    <div className="panel page-panel">
      <div className="page-head">
        <h1>My Library</h1>
        <div className="tabs">
          <div className="seg" role="group" aria-label="Show">
            <button aria-pressed={filter === "library" && !onlySaved} onClick={() => { setFilter("library"); setOnlySaved(false); }}>All</button>
            <button aria-pressed={filter === "library" && onlySaved} onClick={() => { setFilter("library"); setOnlySaved(true); }}>Saved</button>
            <button aria-pressed={filter === "mine"} onClick={() => { setFilter("mine"); setOnlySaved(false); }}>Made by me</button>
          </div>
          <Link className="btn btn-primary" to="/new">
            <IconPlus size={20} /> Add a song
          </Link>
        </div>
      </div>
      <SongGrid
        songs={shown}
        error={error}
        empty={
          <>
            Songs you add or save (♥) show up here. <Link to="/discover">Find something to play</Link> or{" "}
            <Link to="/new">add your own</Link>.
          </>
        }
      />
    </div>
  );
}

export function PlaylistsPage() {
  const toast = useToast();
  const [lists, setLists] = useState<PlaylistSummary[] | null>(null);
  const [name, setName] = useState("");
  const load = () => api.playlists().then((r) => setLists(r.playlists)).catch((e) => toast(errorText(e), true));
  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  const create = async () => {
    if (!name.trim()) return;
    try {
      await api.createPlaylist(name.trim());
      setName("");
      void load();
    } catch (e) {
      toast(errorText(e), true);
    }
  };
  return (
    <div className="panel page-panel">
      <div className="page-head">
        <h1>Playlists</h1>
        <form
          style={{ display: "flex", gap: 8 }}
          onSubmit={(e) => {
            e.preventDefault();
            void create();
          }}
        >
          <input className="input" placeholder="New playlist" value={name} onChange={(e) => setName(e.target.value)} maxLength={80} />
          <button className="btn btn-primary">Create</button>
        </form>
      </div>
      {!lists && <div className="empty">Loading…</div>}
      {lists?.length === 0 && <div className="empty">Group songs into setlists for practice or a gig. Create your first playlist above.</div>}
      <div className="grid-cards">
        {lists?.map((l) => (
          <Link key={l.id} to={`/playlists/${l.id}`} className="song-card">
            <Artwork title={l.name} rounded={10} />
            <h3>{l.name}</h3>
            <div className="meta">
              {l.count} song{l.count === 1 ? "" : "s"}
            </div>
          </Link>
        ))}
      </div>
    </div>
  );
}

export function PlaylistPage() {
  const { id = "" } = useParams();
  const toast = useToast();
  const navigate = useNavigate();
  const [data, setData] = useState<{ playlist: PlaylistSummary; songs: SongSummary[] } | null>(null);
  const load = () => api.playlist(id).then(setData).catch((e) => toast(errorText(e), true));
  useEffect(() => {
    void load();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [id]);
  if (!data) return <div className="panel page-panel empty">Loading…</div>;
  const move = async (i: number, dir: number) => {
    const ids = data.songs.map((s) => s.id);
    const j = i + dir;
    if (j < 0 || j >= ids.length) return;
    [ids[i], ids[j]] = [ids[j], ids[i]];
    setData({ ...data, songs: ids.map((x) => data.songs.find((s) => s.id === x)!) });
    await api.reorderPlaylist(id, ids).catch((e) => toast(errorText(e), true));
  };
  const rename = async () => {
    const name = prompt("Playlist name", data.playlist.name);
    if (!name?.trim()) return;
    const r = await api.renamePlaylist(id, name.trim()).catch((e) => toast(errorText(e), true));
    if (r) setData({ ...data, playlist: r.playlist });
  };
  const remove = async () => {
    if (!confirm(`Delete the playlist "${data.playlist.name}"? The songs stay in your library.`)) return;
    await api.deletePlaylist(id);
    navigate("/playlists");
  };
  return (
    <div className="panel page-panel">
      <div className="page-head">
        <h1>{data.playlist.name}</h1>
        <div className="tabs">
          <button className="btn btn-sm" onClick={rename}>Rename</button>
          <button className="btn btn-sm btn-danger" onClick={remove}>
            <IconTrash size={18} /> Delete
          </button>
        </div>
      </div>
      {data.songs.length === 0 && <div className="empty">Add songs from a song's ⋮ menu → Add to playlist.</div>}
      <div style={{ display: "grid", gap: 4 }}>
        {data.songs.map((s, i) => (
          <div key={s.id} className="song-row" style={{ gridTemplateColumns: "28px 64px 1fr auto" }}>
            <span className="faint">{i + 1}</span>
            <Artwork title={s.title} youtubeId={s.youtubeId} rounded={8} />
            <Link to={`/song/${s.id}`} style={{ color: "inherit", textDecoration: "none", minWidth: 0 }}>
              <div className="title">{s.title}</div>
              <div className="sub">
                {s.artist} · {s.chords.slice(0, 6).map(prettyAccidentals).join(" ")}
              </div>
            </Link>
            <div style={{ display: "flex", gap: 4 }}>
              <button className="btn btn-sm" onClick={() => move(i, -1)} aria-label="Move up" disabled={i === 0}>↑</button>
              <button className="btn btn-sm" onClick={() => move(i, 1)} aria-label="Move down" disabled={i === data.songs.length - 1}>↓</button>
              <button
                className="btn btn-sm"
                aria-label="Remove from playlist"
                onClick={async () => {
                  await api.removeFromPlaylist(id, s.id).catch((e) => toast(errorText(e), true));
                  void load();
                }}
              >
                ✕
              </button>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
