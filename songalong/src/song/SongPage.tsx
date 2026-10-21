import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { Link, useNavigate, useParams } from "react-router-dom";
import type { SongRecord, SongSummary } from "../../shared/api";
import type { Instrument, PracticeSettings } from "../../shared/types";
import { parseChord } from "../../shared/music/chords";
import { bestCapo as findBestCapo } from "../../shared/music/capo";
import { isFretted } from "../../shared/music/voicings";
import { cajonStrokes, DEFAULT_LEVEL, setBarFix, setFix, type CajonLevel, type Stroke } from "../../shared/music/cajon";
import { prettyAccidentals } from "../../shared/music/notes";
import { api } from "../api";
import { useUser } from "../auth";
import { Artwork } from "../components/Artwork";
import {
  IconEdit,
  IconGuitar,
  IconHeart,
  IconList,
  IconMandolin,
  IconBass,
  IconCajon,
  IconMore,
  IconPiano,
  IconPlayCircle,
  IconShare,
  IconTrash,
  IconUkulele,
} from "../components/Icons";
import { Modal, Popover, errorText, useToast } from "../components/ui";
import { AddToPlaylist } from "../components/AddToPlaylist";
import { PlayerEngine } from "../player/engine";
import { useChordView, type ViewSettings } from "../player/hooks";
import { warmUp } from "../player/synth";
import { Tuner } from "../tools/Tuner";
import { CapoFinder } from "./CapoFinder";
import { ChordStrip } from "./ChordStrip";
import { Diagrams } from "./Diagrams";
import { downloadChordPro, downloadMidi, downloadText, printUrl } from "./exports";
import { LyricsLane } from "./LyricsLane";
import { MediaPanel } from "./MediaPanel";
import { SimilarSongs, SongDetails, ToolsPanel } from "./SidePanels";
import { SummaryView } from "./SummaryView";
import { Transport } from "./Transport";
import { voicingFor } from "./voicing";
import { LeadNeck } from "./LeadNeck";
import { BarFixer, CajonPanel, GrooveCards, GrooveStrip, useCajonPart } from "./Cajon";
import { noteMidi } from "../../shared/music/chords";
import type { Part } from "../player/engine";

export function SongPage() {
  const { id = "" } = useParams();
  const [record, setRecord] = useState<SongRecord | null>(null);
  const [progress, setProgress] = useState<PracticeSettings | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let live = true;
    setRecord(null);
    setError(null);
    Promise.all([api.song(id), api.progress(id).catch(() => ({ settings: {} as PracticeSettings, plays: 0 }))])
      .then(([s, p]) => {
        if (!live) return;
        setProgress(p.settings);
        setRecord(s.song);
      })
      .catch((e) => live && setError(errorText(e)));
    return () => {
      live = false;
    };
  }, [id]);

  if (error)
    return (
      <div className="panel page-panel empty">
        <h2>Song not available</h2>
        <p>{error}</p>
        <Link className="btn" to="/library">Back to your library</Link>
      </div>
    );
  if (!record || !progress) return <div className="panel page-panel empty">Loading song…</div>;
  return <SongPlayer key={record.id} record={record} saved={progress} onChanged={setRecord} />;
}

const INSTRUMENTS: { id: Instrument; label: string; icon: typeof IconGuitar }[] = [
  { id: "guitar", label: "Guitar", icon: IconGuitar },
  { id: "ukulele", label: "Ukulele", icon: IconUkulele },
  { id: "piano", label: "Piano", icon: IconPiano },
  { id: "mandolin", label: "Mandolin", icon: IconMandolin },
  { id: "bass", label: "Bass", icon: IconBass },
  { id: "cajon", label: "Cajon", icon: IconCajon },
];

/** While a song's cajon part is being worked out, the page asks again this often. */
const CAJON_POLL_MS = 45_000;

function SongPlayer({ record, saved, onChanged }: { record: SongRecord; saved: PracticeSettings; onChanged: (r: SongRecord) => void }) {
  const user = useUser();
  const toast = useToast();
  const navigate = useNavigate();
  const doc = record.doc;

  const engine = useMemo(() => new PlayerEngine(doc), [record.id]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => () => engine.destroy(), [engine]);
  const hasLead = !!doc.lead?.length;
  const [part, setPart] = useState<Part>(saved.part === "lead" && hasLead ? "lead" : "rhythm");
  // what is shown: the rhythm part, or the lead part in place of the chords
  const shownDoc = useMemo(() => (part === "lead" && hasLead ? { ...doc, chords: doc.lead! } : doc), [doc, part, hasLead]);
  // set before the children render, so they read the right part
  useMemo(() => {
    engine.setParts(doc.chords, doc.lead ?? []);
    engine.setDoc(shownDoc);
  }, [engine, doc, shownDoc]);

  const [settings, setSettings] = useState<ViewSettings>(() => ({
    transpose: saved.transpose ?? 0,
    capo: saved.capo ?? doc.capo ?? 0,
    instrument: saved.instrument ?? user.settings.instrument ?? "guitar",
    simplify: saved.simplify ?? false,
    capoShapes: user.settings.capoShapes ?? true,
  }));
  const isCajon = settings.instrument === "cajon";
  // the cajon part's level: Easy until the player picks one (only a pick is remembered)
  const [cajonPick, setCajonPick] = useState<CajonLevel | undefined>(saved.cajonLevel);
  const cajonLevel = cajonPick ?? DEFAULT_LEVEL;
  // the player's fixes to the grooves, by level
  const [cajonFixes, setCajonFixes] = useState<NonNullable<PracticeSettings["cajonFixes"]>>(() => saved.cajonFixes ?? {});
  const fixGroove = useCallback(
    (found: readonly Stroke[], bar: readonly Stroke[]) => setCajonFixes((f) => ({ ...f, [cajonLevel]: setFix(f[cajonLevel], found, bar) })),
    [cajonLevel],
  );
  // and to single bars
  const [cajonBarFixes, setCajonBarFixes] = useState<NonNullable<PracticeSettings["cajonBarFixes"]>>(() => saved.cajonBarFixes ?? {});
  const fixBar = useCallback(
    (bar: number, found: readonly Stroke[], base: readonly Stroke[], next: readonly Stroke[]) =>
      setCajonBarFixes((f) => ({ ...f, [cajonLevel]: setBarFix(f[cajonLevel], bar, found, base, next) })),
    [cajonLevel],
  );
  const cajon = useCajonPart(doc, cajonLevel, cajonFixes[cajonLevel], cajonBarFixes[cajonLevel]);
  // the bar being fixed (the song waits there meanwhile)
  const [fixingBar, setFixingBar] = useState<number | null>(null);
  const openBarFixer = useCallback(
    (bar: number) => {
      engine.pause();
      engine.seekBeat(bar * cajon.beatsPerBar);
      setFixingBar(bar);
    },
    [engine, cajon.beatsPerBar],
  );
  useEffect(() => engine.setCajon(cajonStrokes(cajon)), [engine, cajon]);

  // a song whose cajon part is still being worked out: look again now and then
  useEffect(() => {
    if (!isCajon || !(record.cajon === "queued" || record.cajon === "working" || record.cajon === "waiting")) return;
    const timer = window.setInterval(() => {
      api
        .song(record.id)
        .then((r) => {
          if (r.song.cajon !== record.cajon || !!r.song.doc.drums !== !!doc.drums) onChanged(r.song);
        })
        .catch(() => {});
    }, CAJON_POLL_MS);
    return () => window.clearInterval(timer);
  }, [isCajon, record, doc.drums, onChanged]);

  const [choices, setChoices] = useState<Record<string, number>>({});
  const [mode, setMode] = useState<"animated" | "summary">("animated");
  const [favorite, setFavorite] = useState(record.favorite);
  const [modal, setModal] = useState<null | "capo" | "tuner" | "transpose" | "playlist">(null);
  const [similar, setSimilar] = useState<SongSummary[]>([]);
  const view = useChordView(doc, settings);

  useEffect(() => {
    api.similar(record.id).then((r) => setSimilar(r.songs)).catch(() => {});
  }, [record.id]);

  // engine settings from the saved practice state
  useEffect(() => {
    if (saved.speed) engine.setSpeed(saved.speed);
    if (saved.loop?.on) engine.setLoop({ on: true, from: saved.loop.from, to: saved.loop.to });
    engine.setCountIn(user.settings.countIn ?? 1);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [engine]);

  // the engine plays the same shapes the diagrams show; lead notes at their written octave
  useEffect(() => {
    engine.instrument = settings.instrument;
    engine.voicer = (p, i) => {
      if (p === "lead") return leadNotes(doc.lead?.[i]?.name ?? "", view, settings);
      const v = voicingFor(doc.chords[i].name, view, settings, choices[doc.chords[i].name] ?? 0);
      return v ? { midis: v.midis, riff: v.riff } : null;
    };
    const all = doc.chords.flatMap((c) => voicingFor(c.name, view, settings, choices[c.name] ?? 0)?.midis ?? []);
    warmUp([...new Set(all)], settings.instrument);
  }, [engine, doc, view, settings, choices]);

  // remember settings per song (debounced), count a "jam session" on first play
  const counted = useRef(false);
  const saveTimer = useRef(0);
  const persist = useCallback(
    (played = false) => {
      window.clearTimeout(saveTimer.current);
      saveTimer.current = window.setTimeout(() => {
        const st = engine.getState();
        api
          .saveProgress(
            record.id,
            {
              part,
              cajonLevel: cajonPick,
              cajonFixes,
              cajonBarFixes,
              transpose: settings.transpose,
              capo: settings.capo,
              instrument: settings.instrument,
              simplify: settings.simplify,
              speed: st.speed,
              loop: st.loop,
            },
            played,
          )
          .catch(() => {});
      }, played ? 0 : 1200);
    },
    [engine, record.id, settings, part, cajonPick, cajonFixes, cajonBarFixes],
  );
  useEffect(() => persist(), [settings, part, cajonPick, cajonFixes, cajonBarFixes, persist]);
  useEffect(
    () =>
      engine.subscribe(() => {
        const st = engine.getState();
        if (st.playing && !counted.current) {
          counted.current = true;
          persist(true);
        }
      }),
    [engine, persist],
  );

  // keyboard: space = play / pause, arrows = bars, [ ] = speed
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement;
      if (t.closest("input, textarea, select, [contenteditable]")) return;
      if (e.code === "Space") {
        e.preventDefault();
        engine.toggle();
      } else if (e.key === "ArrowLeft") engine.skipBars(-1);
      else if (e.key === "ArrowRight") engine.skipBars(1);
      else if (e.key === "Home") engine.restart();
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [engine]);

  const patch = (p: Partial<ViewSettings>) => setSettings((s) => ({ ...s, ...p }));

  const chordsShown = useMemo(() => {
    const out: string[] = [];
    for (const c of doc.chords) {
      const p = parseChord(c.name);
      if (!p || p.category === "none") continue;
      const n = view.shown(c.name);
      if (!out.includes(n)) out.push(n);
    }
    return out;
  }, [doc.chords, view]);

  const best = useMemo(() => {
    if (!isFretted(settings.instrument)) return null;
    const sounding = doc.chords.map((c) => ({ ...c, name: view.sounding(c.name) }));
    return findBestCapo(sounding, doc.length, settings.instrument).capo;
  }, [doc, view, settings.instrument]);

  const toggleFavorite = async () => {
    const next = !favorite;
    setFavorite(next);
    try {
      await api.favorite(record.id, next);
      toast(next ? "Saved to My Library" : "Removed from My Library");
    } catch (e) {
      setFavorite(!next);
      toast(errorText(e), true);
    }
  };

  const share = async () => {
    const url = location.origin + "/song/" + record.id;
    const note = !record.isPublic && !record.isDemo ? " (only you can open it until you make it public)" : "";
    try {
      if (navigator.share) await navigator.share({ title: doc.title, url });
      else {
        await navigator.clipboard.writeText(url);
        toast("Link copied" + note);
      }
    } catch {
      /* cancelled */
    }
  };

  const print = () => window.open(printUrl(record.id, settings, true), "_blank", "noopener");
  const onExport = (kind: "pdf" | "chordpro" | "text" | "midi") => {
    if (kind === "pdf") window.open(printUrl(record.id, settings, true), "_blank", "noopener");
    if (kind === "chordpro") downloadChordPro(doc, view, settings);
    if (kind === "text") downloadText(doc, view, settings);
    if (kind === "midi") downloadMidi(doc, view);
  };

  const togglePublic = async () => {
    try {
      const r = await api.updateSong(record.id, doc, !record.isPublic);
      onChanged(r.song);
      toast(r.song.isPublic ? "Now public: it shows in Discover" : "Now private");
    } catch (e) {
      toast(errorText(e), true);
    }
  };
  const remove = async () => {
    if (!confirm(`Delete "${doc.title}"? This can't be undone.`)) return;
    try {
      await api.deleteSong(record.id);
      toast("Song deleted");
      navigate("/library");
    } catch (e) {
      toast(errorText(e), true);
    }
  };
  const duplicate = async () => {
    try {
      const r = await api.createSong({ ...doc, title: doc.title + (record.mine ? " (copy)" : "") });
      toast("Copy saved to My Library");
      navigate(`/song/${r.song.id}/edit`);
    } catch (e) {
      toast(errorText(e), true);
    }
  };

  return (
    <div className="song-page song-layout">
      <section className={"panel song-main" + (part === "lead" && !isCajon ? " lead-mode" : "") + (isCajon ? " cajon-mode" : "")} aria-label="Play along">
        <header className="song-head">
          <Artwork title={doc.title} youtubeId={record.youtubeId} />
          <div style={{ minWidth: 0 }}>
            {doc.genre && <span className="chip">{doc.genre}</span>}
            <h1>{doc.title || "Untitled"}</h1>
            {doc.artist && (
              <div className="by">
                by <b>{doc.artist}</b>
              </div>
            )}
            {doc.credits && <div className="credits">{doc.credits}{!record.mine && " · Spot a wrong chord? ⋮ → Save an editable copy"}</div>}
          </div>
          <div className="head-actions">
            <button className={"icon-btn" + (favorite ? " fav" : "")} onClick={toggleFavorite} aria-pressed={favorite} aria-label={favorite ? "Remove from My Library" : "Save to My Library"}>
              <IconHeart filled={favorite} size={26} />
            </button>
            <button className="icon-btn" onClick={share} aria-label="Share">
              <IconShare size={24} />
            </button>
            <Popover
              align="end"
              width={250}
              trigger={(p) => (
                <button className="icon-btn" {...p} ref={p.ref} aria-label="More">
                  <IconMore size={24} />
                </button>
              )}
            >
              {(close) => (
                <>
                  {record.mine && (
                    <Link className="menu-item" to={`/song/${record.id}/edit`} onClick={close}>
                      <IconEdit size={20} /> Edit chords, lyrics & timing
                    </Link>
                  )}
                  <button className="menu-item" onClick={() => { close(); setModal("playlist"); }}>
                    <IconList size={20} /> Add to playlist
                  </button>
                  <button className="menu-item" onClick={() => { close(); void duplicate(); }}>
                    <IconPlayCircle size={20} /> {record.mine ? "Duplicate" : "Save an editable copy"}
                  </button>
                  {record.mine && (
                    <button className="menu-item" onClick={() => { close(); void togglePublic(); }}>
                      <IconShare size={20} /> {record.isPublic ? "Make private" : "Make public (Discover)"}
                    </button>
                  )}
                  {record.mine && (
                    <button className="menu-item btn-danger" onClick={() => { close(); void remove(); }}>
                      <IconTrash size={20} /> Delete song
                    </button>
                  )}
                </>
              )}
            </Popover>
          </div>
        </header>

        <Transport
          hasLead={hasLead}
          cajon={isCajon}
          engine={engine}
          view={view}
          settings={settings}
          bestCapo={best}
          onSettings={patch}
          onMidi={() => downloadMidi(doc, view)}
          onExport={onExport}
          onPrint={print}
        />

        {mode === "animated" ? (
          <>
            {isCajon ? <GrooveStrip engine={engine} part={cajon} onFixBar={openBarFixer} /> : <ChordStrip key={part} engine={engine} view={view} lead={part === "lead"} />}
            <LyricsLane engine={engine} />
          </>
        ) : (
          <SummaryView key={part} engine={engine} view={view} />
        )}

        <div className="inst-row">
          {isCajon ? (
            <div className="seg part-seg" role="group" aria-label="Cajon level">
              <button aria-pressed={cajonLevel === "easy"} onClick={() => setCajonPick("easy")} title="Bass and Slap on the beats">Easy</button>
              <button aria-pressed={cajonLevel === "groove"} onClick={() => setCajonPick("groove")} title="The song's groove on 8th notes, with light taps">Groove</button>
            </div>
          ) : hasLead && (
            <div className="seg part-seg" role="group" aria-label="Part">
              <button aria-pressed={part === "rhythm"} onClick={() => setPart("rhythm")} title="Chords and strumming">Rhythm</button>
              <button aria-pressed={part === "lead"} onClick={() => setPart("lead")} title="Melody, riffs and fills">Lead</button>
            </div>
          )}
          {INSTRUMENTS.map(({ id, label, icon: Icon }) => (
            <button key={id} className={"btn" + (settings.instrument === id ? " active" : "")} onClick={() => patch({ instrument: id })} aria-pressed={settings.instrument === id}>
              <Icon /> {label}
            </button>
          ))}
          <span style={{ flex: 1 }} />
          <div className="view-btns" style={{ display: "flex", gap: 10 }}>
            <button className={"btn" + (mode === "animated" ? " active" : "")} onClick={() => setMode("animated")} aria-pressed={mode === "animated"}>
              <IconPlayCircle /> Animated
            </button>
            <button className={"btn" + (mode === "summary" ? " active" : "")} onClick={() => setMode("summary")} aria-pressed={mode === "summary"}>
              <IconList /> Summary
            </button>
          </div>
        </div>

        {isCajon ? (
          <>
            <CajonPanel engine={engine} part={cajon} status={record.cajon} onFixBar={openBarFixer} />
            <GrooveCards engine={engine} part={cajon} bpm={doc.bpm} onFix={fixGroove} />
          </>
        ) : part === "lead" ? (
          <LeadNeck engine={engine} view={view} settings={settings} leftHanded={!!user.settings.leftHanded} />
        ) : (
          <Diagrams
            engine={engine}
            view={view}
            settings={settings}
            leftHanded={!!user.settings.leftHanded}
            choices={choices}
            onChoose={(sym, i) => setChoices((c) => ({ ...c, [sym]: i }))}
          />
        )}
      </section>

      <aside className="song-side">
        <MediaPanel engine={engine} doc={doc} title={doc.title} />
        <SongDetails doc={doc} view={view} settings={settings} chords={chordsShown} />
        <ToolsPanel
          simplify={settings.simplify}
          onTranspose={() => setModal("transpose")}
          onCapo={() =>
            !isFretted(settings.instrument) || settings.instrument === "bass" ? toast("Capo is for guitar, ukulele and mandolin.") : setModal("capo")
          }
          onTuner={() => setModal("tuner")}
          onMidi={() => downloadMidi(doc, view)}
          onPrint={print}
          onSimplify={() => patch({ simplify: !settings.simplify })}
        />
        <SimilarSongs songs={similar} />
      </aside>

      {modal === "capo" && isFretted(settings.instrument) && settings.instrument !== "bass" && (
        <CapoFinder
          doc={doc}
          view={view}
          instrument={settings.instrument}
          current={settings.capo}
          onPick={(capo) => {
            patch({ capo });
            setModal(null);
          }}
          onClose={() => setModal(null)}
        />
      )}
      {modal === "tuner" && (
        <Modal title="Tuner" onClose={() => setModal(null)}>
          <Tuner instrument={settings.instrument} />
        </Modal>
      )}
      {modal === "transpose" && (
        <Modal title="Transpose" onClose={() => setModal(null)}>
          <p className="muted" style={{ marginTop: 0 }}>Move every chord up or down to suit your voice. Now in {prettyAccidentals(view.keyName)}.</p>
          <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fill, minmax(70px, 1fr))", gap: 8 }}>
            {Array.from({ length: 13 }, (_, i) => i - 6).map((t) => (
              <button key={t} className={"btn" + (settings.transpose === t ? " active" : "")} onClick={() => patch({ transpose: t })}>
                {t > 0 ? "+" : ""}
                {t}
              </button>
            ))}
          </div>
        </Modal>
      )}
      {modal === "playlist" && <AddToPlaylist songId={record.id} onClose={() => setModal(null)} />}
      {isCajon && fixingBar !== null && fixingBar < cajon.barGroove.length && (
        <BarFixer
          part={cajon}
          bar={fixingBar}
          bpm={doc.bpm}
          onFixGroove={fixGroove}
          onFixBar={fixBar}
          onBar={openBarFixer}
          onClose={() => setFixingBar(null)}
        />
      )}
    </div>
  );
}

/** Sounding notes for a lead event (guide playback). */
function leadNotes(name: string, view: ReturnType<typeof useChordView>, settings: ViewSettings) {
  const sym = view.sounding(name);
  const c = parseChord(sym);
  if (!c || c.category === "none") return null;
  if (c.category === "note" || c.category === "notes") {
    const seq = c.category === "notes" ? c.sequence ?? [] : [c.root];
    return { midis: seq.map((pc, i) => noteMidi(c, i) ?? 60 + pc), riff: true };
  }
  const v = voicingFor(name, view, settings, 0);
  return v ? { midis: v.midis, riff: false } : null;
}
