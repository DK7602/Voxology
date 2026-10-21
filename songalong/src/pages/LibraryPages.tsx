import { useEffect, useRef, useState, type KeyboardEvent, type PointerEvent } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";
import type { PlaylistSummary, SongSummary } from "../../shared/api";
import { namedForList } from "../../shared/music/songKey";
import { mergeOrder } from "../../shared/order";
import { api, type SongScope } from "../api";
import { useUser } from "../auth";
import { Artwork } from "../components/Artwork";
import {
  IconClose,
  IconCompass,
  IconGrip,
  IconHeart,
  IconMusicFile,
  IconPiano,
  IconPlus,
  IconTrash,
  IconTuner,
  IconYoutube,
} from "../components/Icons";
import { InstallButton } from "../components/InstallButton";
import { useGridReorder } from "../components/gridReorder";
import { useLongPress } from "../components/longPress";
import { SongMenu, type SongChange, type SongList } from "../components/SongMenu";
import { errorText, useToast } from "../components/ui";
import { SongSearchBox } from "./SearchPage";

/** A song in a grid. With `onMenu`, a long-press (or right-click) opens the song's menu; with
 *  `reorder`, the grid's pick-up-and-drag handlers take over (they open the menu themselves). */
export function SongCard({
  s,
  onMenu,
  reorder,
}: {
  s: SongSummary;
  onMenu?: () => void;
  reorder?: { handlers: Record<string, unknown>; ref: (el: HTMLElement | null) => void; style: React.CSSProperties; lifted: boolean };
}) {
  const named = namedForList(s.key, s.chords);
  const press = useLongPress(() => onMenu?.());
  const extra = reorder
    ? { ...reorder.handlers, ref: reorder.ref, style: reorder.style, draggable: false }
    : onMenu
      ? press
      : {};
  return (
    <Link to={`/song/${s.id}`} className={"song-card" + (reorder?.lifted ? " lifted" : "")} {...extra}>
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
          {s.key && ` · ${named.key}`}
          {s.bpm ? ` · ${Math.round(s.bpm)} BPM` : ""}
        </div>
      </div>
      <div className="chords-line">{named.chords.slice(0, 8).join("  ")}</div>
    </Link>
  );
}

function useSongs(scope: SongScope, q?: string) {
  const [songs, setSongs] = useState<SongSummary[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [version, setVersion] = useState(0);
  useEffect(() => {
    let live = true;
    if (!version) setSongs(null);
    api
      .songs(scope, q)
      .then((r) => live && setSongs(r.songs))
      .catch((e) => live && setError(errorText(e)));
    return () => {
      live = false;
    };
  }, [scope, q, version]);
  /** after a long-press menu change: drop the song, mark it saved, or (Undo) load the list again */
  const change = (id: string, c: SongChange) => {
    if (c.removed === false) return setVersion((v) => v + 1);
    setSongs((list) =>
      list && (c.removed ? list.filter((s) => s.id !== id) : list.map((s) => (s.id === id && c.favorite !== undefined ? { ...s, favorite: c.favorite } : s))),
    );
  };
  /** put these songs (all, or a filtered view of them) in this order, the rest staying put */
  const arrange = (ids: string[]) =>
    setSongs((list) => {
      if (!list) return list;
      const byId = new Map(list.map((s) => [s.id, s]));
      return mergeOrder(list.map((s) => s.id), ids).map((id) => byId.get(id)!);
    });
  return { songs, error, change, arrange };
}

function SongGrid({
  songs,
  error,
  empty,
  list,
  onChange,
  onReorder,
}: {
  songs: SongSummary[] | null;
  error: string | null;
  empty: React.ReactNode;
  /** which list the cards are in, for their long-press menu */
  list: SongList;
  onChange: (id: string, c: SongChange) => void;
  /** cards can be picked up and moved (My Library) */
  onReorder?: (from: number, to: number) => void;
}) {
  const [menu, setMenu] = useState<SongSummary | null>(null);
  const grid = useGridReorder(
    songs?.length ?? 0,
    (from, to) => onReorder?.(from, to),
    (i) => songs && setMenu(songs[i]),
  );
  if (error) return <div className="notice error">{error}</div>;
  if (!songs) return <div className="empty">Loading…</div>;
  if (!songs.length) return <div className="empty">{empty}</div>;
  return (
    <div className="grid-cards">
      {songs.map((s, i) => (
        <SongCard
          key={s.id}
          s={s}
          onMenu={() => setMenu(s)}
          reorder={onReorder ? { handlers: grid.handlers(i), ref: grid.ref(i), style: grid.style(i), lifted: grid.lifted === i } : undefined}
        />
      ))}
      {menu && <SongMenu song={menu} list={list} onClose={() => setMenu(null)} onChange={(c) => onChange(menu.id, c)} />}
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
          <p>Search any song, tap play, and the chords and lyrics scroll in time: guitar, bass, ukulele, piano or mandolin, any key, any capo.</p>
          <SongSearchBox big />
          <div style={{ display: "flex", gap: 10, flexWrap: "wrap", marginTop: 14 }}>
            <Link className="btn btn-primary" to="/new">
              <IconPlus size={20} /> Add a song
            </Link>
            <Link className="btn" to="/discover">
              <IconCompass size={20} /> Try an example
            </Link>
            <InstallButton />
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
            <span>Guitar, bass, ukulele, mandolin</span>
          </button>
        </div>
      </section>

      {recent.songs && recent.songs.length > 0 && (
        <>
          <div className="section-title">
            <h2>Keep playing</h2>
          </div>
          <SongGrid songs={recent.songs.slice(0, 8)} error={recent.error} empty="" list="recent" onChange={recent.change} />
        </>
      )}
      <div className="section-title">
        <h2>Discover</h2>
        <Link to="/discover">See all</Link>
      </div>
      <SongGrid songs={discover.songs?.slice(0, 8) ?? null} error={discover.error} empty="Nothing here yet." list="discover" onChange={discover.change} />
    </div>
  );
}

export function DiscoverPage() {
  const { songs, error, change } = useSongs("discover");
  return (
    <div className="panel page-panel">
      <div className="page-head">
        <h1>Discover</h1>
        <span className="muted">Example songs and songs other players made public</span>
      </div>
      <SongGrid songs={songs} error={error} empty="No public songs yet." list="discover" onChange={change} />
    </div>
  );
}

export function LibraryPage() {
  const [filter, setFilter] = useState<"library" | "mine">("library");
  const { songs, error, change, arrange } = useSongs(filter);
  const [onlySaved, setOnlySaved] = useState(false);
  const toast = useToast();
  const shown = songs && onlySaved ? songs.filter((s) => s.favorite) : songs;
  const reorder = (from: number, to: number) => {
    if (!shown) return;
    const ids = shown.map((s) => s.id);
    ids.splice(to, 0, ...ids.splice(from, 1));
    arrange(ids);
    api.reorderLibrary(ids).catch((e) => toast(errorText(e), true));
  };
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
        list="library"
        onChange={change}
        onReorder={reorder}
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
          <Link key={l.id} to={`/playlists/${l.id}`} className="song-card" aria-label={l.name}>
            <Artwork title={l.name} label={l.name} rounded={10} />
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
  const reorder = useDragReorder(data?.songs.length ?? 0, (from, to) => {
    if (!data || from === to) return;
    const songs = data.songs.slice();
    songs.splice(to, 0, ...songs.splice(from, 1));
    setData({ ...data, songs });
    api.reorderPlaylist(id, songs.map((s) => s.id)).catch((e) => toast(errorText(e), true));
  });
  if (!data) return <div className="panel page-panel empty">Loading…</div>;
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
      <div className="reorder-list" style={{ display: "grid", gap: ROW_GAP }}>
        {data.songs.map((s, i) => (
          <div
            key={s.id}
            ref={reorder.rowRef(i)}
            className={"song-row" + (reorder.dragging === i ? " dragging" : "")}
            style={{ gridTemplateColumns: "34px 20px 64px 1fr auto", ...reorder.rowStyle(i) }}
          >
            <button
              className="drag-handle"
              aria-label={`Move ${s.title}: drag, or use the up and down arrow keys`}
              onPointerDown={(e) => reorder.start(i, e)}
              onKeyDown={(e) => reorder.key(i, e)}
            >
              <IconGrip size={22} />
            </button>
            <span className="faint">{i + 1}</span>
            <Artwork title={s.title} youtubeId={s.youtubeId} rounded={8} />
            <Link to={`/song/${s.id}`} style={{ color: "inherit", textDecoration: "none", minWidth: 0 }}>
              <div className="title">{s.title}</div>
              <div className="sub">
                {s.artist} · {namedForList(s.key, s.chords).chords.slice(0, 6).join(" ")}
              </div>
            </Link>
            <button
              className="btn btn-sm"
              aria-label={`Remove ${s.title} from the playlist`}
              onClick={async () => {
                await api.removeFromPlaylist(id, s.id).catch((e) => toast(errorText(e), true));
                void load();
              }}
            >
              <IconClose size={18} />
            </button>
          </div>
        ))}
      </div>
    </div>
  );
}

// ---------- drag to reorder ----------

const ROW_GAP = 4;
/** how close to the top / bottom of the screen (px) a drag scrolls the page; the bottom allows for
 *  the phone tab bar */
const EDGE_TOP = 90;
const EDGE_BOTTOM = 150;

/**
 * Reordering rows by dragging their handle (mouse or touch), with the page scrolling while the row
 * is held near the top or bottom of the screen, so a long list can be crossed in one drag. The
 * arrow keys move the focused row one place.
 */
function useDragReorder(count: number, onMove: (from: number, to: number) => void) {
  const rows = useRef<(HTMLElement | null)[]>([]);
  const [drag, setDrag] = useState<{ index: number; offset: number; target: number } | null>(null);
  const g = useRef({ index: 0, target: 0, startPageY: 0, clientY: 0, mids: [] as number[], height: 0, raf: 0 });

  const update = () => {
    const s = g.current;
    const offset = s.clientY + window.scrollY - s.startPageY;
    const centre = s.mids[s.index] + offset;
    // its new place: after every other row whose middle it has passed
    let target = 0;
    s.mids.forEach((m, j) => {
      if (j !== s.index && m < centre) target++;
    });
    s.target = target;
    setDrag({ index: s.index, offset, target });
  };

  const start = (index: number, e: PointerEvent) => {
    if (e.button !== 0) return;
    e.preventDefault();
    const handle = e.currentTarget as HTMLElement;
    handle.setPointerCapture(e.pointerId);
    const rects = rows.current.slice(0, count).map((r) => r!.getBoundingClientRect());
    g.current = {
      index,
      target: index,
      startPageY: e.clientY + window.scrollY,
      clientY: e.clientY,
      mids: rects.map((r) => r.top + window.scrollY + r.height / 2),
      height: rects[index].height + ROW_GAP,
      raf: 0,
    };
    setDrag({ index, offset: 0, target: index });
    const scroll = () => {
      const y = g.current.clientY;
      const step = y < EDGE_TOP ? -Math.ceil((EDGE_TOP - y) / 6) : y > innerHeight - EDGE_BOTTOM ? Math.ceil((y - innerHeight + EDGE_BOTTOM) / 6) : 0;
      if (step) {
        window.scrollBy(0, step);
        update();
      }
      g.current.raf = requestAnimationFrame(scroll);
    };
    g.current.raf = requestAnimationFrame(scroll);
    const move = (ev: globalThis.PointerEvent) => {
      // the button is up but no release arrived (let go outside the window): drop it here
      if (ev.pointerType === "mouse" && !(ev.buttons & 1)) return end();
      g.current.clientY = ev.clientY;
      update();
    };
    const end = () => {
      cancelAnimationFrame(g.current.raf);
      handle.removeEventListener("pointermove", move);
      handle.removeEventListener("pointerup", end);
      handle.removeEventListener("pointercancel", end);
      setDrag(null);
      onMove(g.current.index, g.current.target);
    };
    handle.addEventListener("pointermove", move);
    handle.addEventListener("pointerup", end);
    handle.addEventListener("pointercancel", end);
  };

  const key = (index: number, e: KeyboardEvent) => {
    const to = e.key === "ArrowUp" ? index - 1 : e.key === "ArrowDown" ? index + 1 : null;
    if (to === null || to < 0 || to >= count) return;
    e.preventDefault();
    onMove(index, to);
    // keep the focus on the moved row's handle
    requestAnimationFrame(() => rows.current[to]?.querySelector<HTMLElement>(".drag-handle")?.focus());
  };

  /** the held row follows the pointer; the rows it passes slide over to make room */
  const rowStyle = (i: number): React.CSSProperties => {
    if (!drag) return {};
    if (i === drag.index) return { transform: `translateY(${drag.offset}px)`, zIndex: 2, position: "relative" };
    const h = g.current.height;
    const shift = i > drag.index && i <= drag.target ? -h : i < drag.index && i >= drag.target ? h : 0;
    return { transform: `translateY(${shift}px)`, transition: "transform 0.15s" };
  };

  return { start, key, rowStyle, dragging: drag?.index ?? null, rowRef: (i: number) => (el: HTMLElement | null) => void (rows.current[i] = el) };
}
