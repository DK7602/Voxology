import { useEffect, useMemo, useRef, useState } from "react";
import { Link, useNavigate, useParams, useSearchParams } from "react-router-dom";
import type { AudioFile, SongRecord } from "../../shared/api";
import { emptySong, type SongDoc } from "../../shared/types";
import { exportChordPro, importChordSheet } from "../../shared/chordpro";
import { exportLead, importLead } from "../../shared/lead";
import { parseKey, prettyAccidentals } from "../../shared/music/notes";
import { keyFromChords, weightedChords } from "../../shared/music/songKey";
import { indexAt } from "../../shared/timeline";
import { api, audioUrl, uploadAudio } from "../api";
import { analyseAudioFile } from "../analysis";
import { IconMusicFile, IconNote, IconUpload, IconYoutube } from "../components/Icons";
import { Toggle, errorText, useToast } from "../components/ui";
import { PlayerEngine } from "../player/engine";
import { usePlayerState } from "../player/hooks";
import { MediaPanel } from "../song/MediaPanel";
import "../styles/editor.css";

const EXAMPLE = `{title: My Song}
{key: G}

[Verse 1]
G             D
Morning comes in slow and gold
Em             C
Coffee warm against the cold

{comment: Chorus}
[C]Sing it [G]out, [D]sing it [Em]loud`;

export function youtubeId(input: string): string | null {
  const s = input.trim();
  if (/^[A-Za-z0-9_-]{11}$/.test(s)) return s;
  try {
    const u = new URL(s);
    if (u.hostname.endsWith("youtu.be")) return u.pathname.slice(1, 12) || null;
    if (u.hostname.includes("youtube.com")) {
      const v = u.searchParams.get("v");
      if (v) return v.slice(0, 11);
      const m = /\/(embed|shorts|live)\/([A-Za-z0-9_-]{11})/.exec(u.pathname);
      if (m) return m[2];
    }
  } catch {
    /* not a URL */
  }
  return null;
}

export function SongEditorPage() {
  const { id } = useParams();
  const [record, setRecord] = useState<SongRecord | null>(null);
  const [error, setError] = useState<string | null>(null);
  useEffect(() => {
    if (!id) return;
    api.song(id).then((r) => setRecord(r.song)).catch((e) => setError(errorText(e)));
  }, [id]);
  if (id && error) return <div className="panel page-panel empty">{error}</div>;
  if (id && !record) return <div className="panel page-panel empty">Loading…</div>;
  if (record && !record.mine) return <div className="panel page-panel empty">Only the person who added this song can edit it. Use “Save an editable copy” on the song page.</div>;
  return <Editor key={id ?? "new"} record={record} />;
}

function Editor({ record }: { record: SongRecord | null }) {
  const toast = useToast();
  const navigate = useNavigate();
  const [params] = useSearchParams();
  const [doc, setDoc] = useState<SongDoc>(() => record?.doc ?? emptySong());
  const [isPublic, setIsPublic] = useState(record?.isPublic ?? false);
  const [sheet, setSheet] = useState(() => (record ? exportChordPro(record.doc) : ""));
  const [leadText, setLeadText] = useState(() => (record?.doc.lead ? exportLead(record.doc.lead, record.doc.beatsPerBar) : ""));
  const [source, setSource] = useState<"none" | "youtube" | "audio">(
    record?.doc.media.kind ?? ((params.get("from") as "youtube" | "audio" | null) === "youtube" ? "youtube" : params.get("from") === "audio" ? "audio" : "none"),
  );
  const [ytInput, setYtInput] = useState(record?.doc.media.kind === "youtube" ? `https://youtu.be/${record.doc.media.videoId}` : "");
  const [uploads, setUploads] = useState<AudioFile[]>([]);
  const [upload, setUpload] = useState<{ fraction: number; name: string } | null>(null);
  const [detect, setDetect] = useState<{ fraction: number; stage: string } | null>(null);
  const [saving, setSaving] = useState(false);
  const localFile = useRef<File | null>(null);
  const sheetRef = useRef<HTMLTextAreaElement>(null);

  useEffect(() => {
    if (source === "audio") api.audioFiles().then((r) => setUploads(r.files)).catch(() => {});
  }, [source]);
  useEffect(() => {
    if (params.get("from") === "sheet") sheetRef.current?.focus();
  }, [params]);

  const update = (patch: Partial<SongDoc>) => setDoc((d) => ({ ...d, ...patch }));

  // ---- music source ----
  const applyYouTube = () => {
    const vid = youtubeId(ytInput);
    if (!vid) return toast("That doesn't look like a YouTube link.", true);
    update({ media: { kind: "youtube", videoId: vid } });
    toast("Video set. Use Timing below to line the chords up with it.");
  };

  const onFile = async (file: File | undefined) => {
    if (!file) return;
    if (!/^audio\//.test(file.type) && !/\.(mp3|wav|m4a|aac|ogg|flac)$/i.test(file.name)) return toast("Choose an audio file (MP3, WAV, M4A…).", true);
    localFile.current = file;
    setUpload({ fraction: 0, name: file.name });
    try {
      const a = await uploadAudio(file, (f) => setUpload({ fraction: f, name: file.name }));
      update({ media: { kind: "audio", key: a.key, name: a.name } });
      setUploads((u) => [a, ...u]);
      if (!doc.title) update({ title: file.name.replace(/\.[^.]+$/, "").replace(/[_-]+/g, " ") });
      toast("Uploaded. Finding the chords and beat…");
      void runDetection(file);
    } catch (e) {
      toast(errorText(e), true);
    } finally {
      setUpload(null);
    }
  };

  const runDetection = async (file?: Blob) => {
    try {
      let blob = file ?? localFile.current;
      if (!blob) {
        if (doc.media.kind !== "audio") return;
        setDetect({ fraction: 0, stage: "Downloading" });
        const res = await fetch(audioUrl(doc.media.key), { credentials: "same-origin" });
        if (!res.ok) throw new Error("Couldn't load the recording.");
        blob = await res.blob();
      }
      setDetect({ fraction: 0, stage: "Decoding" });
      const r = await analyseAudioFile(blob, (fraction, stage) => setDetect({ fraction, stage }));
      setDoc((d) => {
        const hasLyrics = d.lines.some((l) => l.text.trim());
        return {
          ...d,
          bpm: r.bpm,
          beatsPerBar: r.beatsPerBar,
          beatTimes: r.beatTimes,
          offset: r.beatTimes[0] ?? 0,
          key: r.key,
          chords: r.chords,
          length: r.length,
          // keep lyrics; spread them over the song until they are tap-synced
          lines: hasLyrics ? spreadLines(d.lines, r.chords[0]?.beat ?? 0, r.length) : [],
        };
      });
      toast(`Found ${new Set(r.chords.map((c) => c.name)).size} chords · ${prettyAccidentals(r.key)} · ${Math.round(r.bpm)} BPM`);
    } catch (e) {
      toast(errorText(e), true);
    } finally {
      setDetect(null);
    }
  };

  // ---- chord sheet ----
  const applySheet = () => {
    const { doc: imported, warnings } = importChordSheet(sheet, { beatsPerBar: doc.beatsPerBar });
    warnings.forEach((w) => toast(w, true));
    setDoc((d) => {
      const next: SongDoc = { ...d };
      if (imported.title && !d.title) next.title = imported.title;
      if (imported.artist && !d.artist) next.artist = imported.artist;
      if (/\{key:/i.test(sheet)) next.key = imported.key;
      // no key line: the key the chords are in (not whatever the song said before)
      else if (imported.chords.length) next.key = keyFromChords(weightedChords(imported.chords, imported.length))?.name ?? d.key;
      if (/\{(tempo|bpm):/i.test(sheet)) next.bpm = imported.bpm;
      if (/\{capo:/i.test(sheet)) next.capo = imported.capo;
      const sheetHasChords = imported.chords.length > 0;
      if (sheetHasChords) {
        // keep earlier timing when the chords are the same sequence (edits to words or names)
        const sameShape = d.chords.length === imported.chords.length && d.lines.length === imported.lines.length;
        next.chords = sameShape ? imported.chords.map((c, i) => ({ ...c, beat: d.chords[i].beat })) : imported.chords;
        next.lines = sameShape ? imported.lines.map((l, i) => ({ ...l, beat: d.lines[i].beat })) : imported.lines;
        next.length = sameShape ? d.length : imported.length;
        if (!sameShape && d.beatTimes && d.media.kind !== "audio") delete next.beatTimes;
      } else {
        // lyrics only: keep the chords (e.g. found in the recording) and spread the lines
        next.lines = spreadLines(imported.lines, d.chords[0]?.beat ?? 0, d.length || imported.length);
        if (!d.chords.length) next.length = imported.length;
      }
      return next;
    });
    toast(imported.chords.length ? "Sheet applied. Check the timing below." : "Lyrics added. Tap-sync them below.");
  };

  const applyLead = () => {
    if (!leadText.trim()) {
      update({ lead: undefined });
      return toast("Lead part removed.");
    }
    const { events, warnings } = importLead(leadText, doc.beatsPerBar);
    warnings.slice(0, 3).forEach((w) => toast(w, true));
    update({ lead: events, length: Math.max(doc.length, events.at(-1)?.beat ?? 0) });
    toast(`Lead part: ${events.filter((e) => e.name !== "N.C.").length} notes / riffs`);
  };

  // ---- save ----
  const save = async () => {
    if (!doc.title.trim()) return toast("Give the song a title.", true);
    if (!doc.chords.length) return toast("Add chords: paste a chord sheet or find them in a recording.", true);
    const finalDoc: SongDoc = { ...doc, media: source === "none" ? { kind: "none" } : doc.media };
    if (source === "youtube" && finalDoc.media.kind !== "youtube") return toast("Add the YouTube link (or choose Backing track).", true);
    if (source === "audio" && finalDoc.media.kind !== "audio") return toast("Upload the recording (or choose Backing track).", true);
    setSaving(true);
    try {
      const r = record ? await api.updateSong(record.id, finalDoc, isPublic) : await api.createSong(finalDoc, isPublic);
      toast("Saved");
      navigate(`/song/${r.song.id}`);
    } catch (e) {
      toast(errorText(e), true);
    } finally {
      setSaving(false);
    }
  };

  const keyOk = !!parseKey(doc.key);

  return (
    <div className="panel page-panel editor">
      <div className="page-head">
        <h1>{record ? "Edit song" : "Add a song"}</h1>
        <div className="tabs">
          {record && <Link className="btn" to={`/song/${record.id}`}>Cancel</Link>}
          <button className="btn btn-primary" onClick={save} disabled={saving}>
            {saving ? "Saving…" : "Save song"}
          </button>
        </div>
      </div>

      <section className="ed-section">
        <h2>1 · Details</h2>
        <div className="form-grid">
          <label className="field">
            <span>Title</span>
            <input className="input" value={doc.title} onChange={(e) => update({ title: e.target.value })} maxLength={200} />
          </label>
          <label className="field">
            <span>Artist</span>
            <input className="input" value={doc.artist} onChange={(e) => update({ artist: e.target.value })} maxLength={200} />
          </label>
          <label className="field">
            <span>Genre</span>
            <input className="input" value={doc.genre} onChange={(e) => update({ genre: e.target.value })} maxLength={60} list="genres" />
            <datalist id="genres">
              {["Country", "Pop", "Rock", "Folk", "Jazz", "Blues", "R&B", "Hip-Hop", "Worship", "Reggae", "Soul", "Indie"].map((g) => (
                <option key={g} value={g} />
              ))}
            </datalist>
          </label>
          <label className="field">
            <span>Key {keyOk ? "" : "(e.g. G, Ebm)"}</span>
            <input className="input" value={doc.key} onChange={(e) => update({ key: e.target.value })} maxLength={12} style={keyOk ? undefined : { borderColor: "var(--danger)" }} />
          </label>
          <label className="field">
            <span>BPM</span>
            <input className="input" type="number" min={20} max={400} step="0.1" value={doc.bpm} onChange={(e) => update({ bpm: parseFloat(e.target.value) || 0, beatTimes: undefined })} />
          </label>
          <label className="field">
            <span>Beats per bar</span>
            <select className="select" value={doc.beatsPerBar} onChange={(e) => update({ beatsPerBar: parseInt(e.target.value, 10) })}>
              {[2, 3, 4, 5, 6, 7, 12].map((n) => (
                <option key={n} value={n}>{n}/4{n === 6 ? " (or 6/8)" : n === 12 ? " (12/8)" : ""}</option>
              ))}
            </select>
          </label>
          <label className="field">
            <span>Capo the song uses</span>
            <select className="select" value={doc.capo} onChange={(e) => update({ capo: parseInt(e.target.value, 10) })}>
              {Array.from({ length: 10 }, (_, n) => (
                <option key={n} value={n}>{n === 0 ? "None" : n}</option>
              ))}
            </select>
          </label>
          <div className="field">
            <span>Public (shows in Discover)</span>
            <Toggle checked={isPublic} onChange={setIsPublic} label="Public" />
          </div>
        </div>
      </section>

      <section className="ed-section">
        <h2>2 · Music to play along with</h2>
        <div className="source-pick">
          <button className={"source" + (source === "none" ? " on" : "")} onClick={() => setSource("none")}>
            <IconNote size={28} />
            <b>Backing track</b>
            <span>SongAlong plays the chords for you</span>
          </button>
          <button className={"source" + (source === "audio" ? " on" : "")} onClick={() => setSource("audio")}>
            <IconMusicFile size={28} />
            <b>My recording</b>
            <span>Upload an MP3 / WAV: chords & beat found automatically</span>
          </button>
          <button className={"source" + (source === "youtube" ? " on" : "")} onClick={() => setSource("youtube")}>
            <IconYoutube size={28} />
            <b>YouTube video</b>
            <span>Play along with a video (add chords from a sheet)</span>
          </button>
        </div>

        {source === "youtube" && (
          <div style={{ display: "flex", gap: 8, marginTop: 12, flexWrap: "wrap" }}>
            <input className="input" style={{ flex: 1, minWidth: 240 }} placeholder="https://www.youtube.com/watch?v=…" value={ytInput} onChange={(e) => setYtInput(e.target.value)} />
            <button className="btn" onClick={applyYouTube}>Use this video</button>
          </div>
        )}

        {source === "audio" && (
          <div style={{ display: "grid", gap: 10, marginTop: 12 }}>
            <label className="dropzone">
              <IconUpload size={28} />
              <span>
                {doc.media.kind === "audio" ? <>Using <b>{doc.media.name}</b>. Choose another file to replace it.</> : "Choose an audio file (up to 100 MB). Only you can play it."}
              </span>
              <input type="file" accept="audio/*,.mp3,.wav,.m4a,.aac,.ogg,.flac" onChange={(e) => onFile(e.target.files?.[0])} />
            </label>
            {upload && <Progress label={`Uploading ${upload.name}`} fraction={upload.fraction} />}
            {detect && <Progress label={detect.stage} fraction={detect.fraction} />}
            {doc.media.kind === "audio" && !detect && !upload && (
              <button className="btn" onClick={() => runDetection()}>Find chords & beat again</button>
            )}
            {uploads.length > 0 && doc.media.kind !== "audio" && (
              <div>
                <div className="muted" style={{ marginBottom: 4 }}>Or use one you uploaded before:</div>
                {uploads.slice(0, 6).map((u) => (
                  <button key={u.key} className="menu-item" onClick={() => { localFile.current = null; update({ media: { kind: "audio", key: u.key, name: u.name } }); }}>
                    {u.name} <span className="faint" style={{ marginLeft: "auto" }}>{(u.size / 1e6).toFixed(1)} MB</span>
                  </button>
                ))}
              </div>
            )}
          </div>
        )}
      </section>

      <section className="ed-section">
        <h2>3 · Chords & lyrics</h2>
        <p className="muted">
          Paste a chord sheet: chords on the line above the words, or ChordPro (<code>[G]words</code>). Use single notes as{" "}
          <code>note:E</code>, riffs as <code>notes:E G A</code>, power chords as <code>E5</code>. Lyrics only? Paste them
          and keep the chords found in your recording.
        </p>
        <textarea ref={sheetRef} className="textarea" value={sheet} placeholder={EXAMPLE} onChange={(e) => setSheet(e.target.value)} spellCheck={false} />
        <div style={{ display: "flex", gap: 8, marginTop: 8, flexWrap: "wrap" }}>
          <button className="btn btn-primary" onClick={applySheet} disabled={!sheet.trim()}>Apply sheet</button>
          <button className="btn" onClick={() => setSheet(exportChordPro(doc))} disabled={!doc.chords.length}>Show current song as text</button>
          <span className="muted" style={{ alignSelf: "center" }}>
            {doc.chords.length} chord changes · {doc.lines.length} lines · {Math.ceil((doc.length || 0) / (doc.beatsPerBar || 4))} bars
          </span>
        </div>
      </section>

      <section className="ed-section">
        <h2>4 · Lead part (optional)</h2>
        <p className="muted">
          The melody, riffs or fills, for the <b>Lead</b> view. Write notes with their octave and length in beats:{" "}
          <code>bar 1: E4 G4 A4:2 | B4:0.5 A4:0.5 G4 r | [E5]:4 | E4-G4-A4:2</code>. <code>r</code> = rest,{" "}
          <code>[E5]</code> = a chord, <code>E4-G4-A4</code> = a quick run in one slot, <code>bar 9:</code> jumps to bar 9.
          Middle C is C4.
        </p>
        <textarea className="textarea" style={{ minHeight: 120 }} value={leadText} onChange={(e) => setLeadText(e.target.value)} spellCheck={false}
          placeholder={"bar 1: E4 G4 A4:2 | B4 A4 G4:2\nbar 5: E4:4"} />
        <div style={{ display: "flex", gap: 8, marginTop: 8, flexWrap: "wrap" }}>
          <button className="btn btn-primary" onClick={applyLead}>{leadText.trim() ? "Apply lead part" : "Remove lead part"}</button>
          <span className="muted" style={{ alignSelf: "center" }}>
            {doc.lead?.length ? `${doc.lead.filter((e) => e.name !== "N.C.").length} lead notes / riffs` : "No lead part"}
          </span>
        </div>
      </section>

      <section className="ed-section">
        <h2>5 · Timing</h2>
        <TimingTools doc={doc} setDoc={setDoc} mediaOn={source !== "none"} />
      </section>

      <div style={{ display: "flex", justifyContent: "flex-end", gap: 8 }}>
        <button className="btn btn-primary" onClick={save} disabled={saving}>
          {saving ? "Saving…" : "Save song"}
        </button>
      </div>
    </div>
  );
}

function Progress({ label, fraction }: { label: string; fraction: number }) {
  return (
    <div className="progress-row" role="progressbar" aria-valuenow={Math.round(fraction * 100)} aria-valuemin={0} aria-valuemax={100}>
      <span>{label}… {Math.round(fraction * 100)}%</span>
      <div className="progress-track">
        <div style={{ width: `${Math.round(fraction * 100)}%` }} />
      </div>
    </div>
  );
}

/** Spreads lyric lines evenly between `from` and `to` beats (whole bars), keeping sections. */
function spreadLines(lines: SongDoc["lines"], from: number, to: number): SongDoc["lines"] {
  if (!lines.length) return lines;
  const span = Math.max(lines.length, to - from);
  const step = span / lines.length;
  return lines.map((l, i) => ({ ...l, beat: Math.round(from + i * step), end: undefined }));
}

/** Tap along to set when each line / chord starts, set beat 1, tap the tempo. */
function TimingTools({ doc, setDoc, mediaOn }: { doc: SongDoc; setDoc: (fn: (d: SongDoc) => SongDoc) => void; mediaOn: boolean }) {
  const engine = useMemo(() => new PlayerEngine(doc), []); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => () => engine.destroy(), [engine]);
  useEffect(() => engine.setDoc(doc), [engine, doc]);
  const st = usePlayerState(engine);
  const [target, setTarget] = useState<"lines" | "chords">("lines");
  const [next, setNext] = useState(0);
  const taps = useRef<number[]>([]);

  const items = target === "lines" ? doc.lines : doc.chords;
  const snap = target === "lines" ? 1 : 0.5;

  const tap = () => {
    if (next >= items.length) return;
    const beat = Math.round(engine.timeline.timeToBeat(engine.heardTime() - 0.08 * st.speed) / snap) * snap; // allow for reaction time
    setDoc((d) => {
      const list = target === "lines" ? [...d.lines] : [...d.chords];
      const prev = next > 0 ? list[next - 1].beat : -Infinity;
      const b = Math.max(beat, prev + snap);
      const shift = b - list[next].beat;
      // move this item and everything after it, so later items keep their spacing
      for (let i = next; i < list.length; i++) list[i] = { ...list[i], beat: list[i].beat + shift };
      return target === "lines"
        ? { ...d, lines: list as SongDoc["lines"], length: Math.max(d.length, list.at(-1)!.beat + d.beatsPerBar) }
        : { ...d, chords: list as SongDoc["chords"], length: Math.max(d.length, list.at(-1)!.beat + d.beatsPerBar) };
    });
    setNext((n) => n + 1);
  };

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.target as HTMLElement).closest("input, textarea, select")) return;
      if (e.key === "t" || e.key === "T" || e.key === "Enter") {
        e.preventDefault();
        tap();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  });

  const startSync = () => {
    setNext(0);
    engine.setCountIn(0);
    engine.seekTime(0);
    void engine.play();
  };

  const setBeatOne = () => {
    const t = engine.heardTime() - 0.08;
    setDoc((d) => {
      if (d.beatTimes?.length) {
        const shift = t - d.beatTimes[0];
        return { ...d, beatTimes: d.beatTimes.map((x) => x + shift), offset: t };
      }
      return { ...d, offset: Math.max(0, t) };
    });
  };

  const tapTempo = () => {
    const now = performance.now();
    taps.current = [...taps.current.filter((x) => now - x < 3000), now].slice(-8);
    if (taps.current.length >= 3) {
      const d = (taps.current.at(-1)! - taps.current[0]) / (taps.current.length - 1);
      const bpm = Math.round((60000 / d) * 10) / 10;
      setDoc((x) => ({ ...x, bpm, beatTimes: undefined }));
    }
  };

  const curItem = indexAt<{ beat: number }>(items, engine.beat);
  return (
    <div className="timing">
      <div className="timing-grid">
        <MediaPanel engine={engine} doc={doc} title={doc.title || "New song"} />
        <div style={{ display: "grid", gap: 12, alignContent: "start" }}>
          <p className="muted" style={{ margin: 0 }}>
            {mediaOn
              ? "Play the music and tap (button, T or Enter) at the start of each line or chord change. Everything after a tap moves with it."
              : "With the backing track, timing comes from the BPM. Tap Tempo to set it, or paste the sheet with one chord per bar."}
          </p>
          <div style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
            <div className="seg" role="group" aria-label="What to tap">
              <button aria-pressed={target === "lines"} onClick={() => { setTarget("lines"); setNext(0); }}>Lyric lines</button>
              <button aria-pressed={target === "chords"} onClick={() => { setTarget("chords"); setNext(0); }}>Chord changes</button>
            </div>
            <button className="btn" onClick={startSync} disabled={!items.length}>Start from the top</button>
            <button className="btn" onClick={() => engine.toggle()}>{st.playing ? "Pause" : "Play"}</button>
          </div>
          <button className="btn btn-primary tap-btn" onClick={tap} disabled={next >= items.length || !st.playing}>
            {next < items.length
              ? `TAP: ${target === "lines" ? doc.lines[next]?.text || "(instrumental line)" : prettyAccidentals(doc.chords[next]?.name ?? "")}`
              : "All set ✓"}
          </button>
          <div style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
            <button className="btn btn-sm" onClick={setBeatOne} title="Press exactly on the first beat of bar 1">Beat 1 is now</button>
            <button className="btn btn-sm" onClick={tapTempo}>Tap tempo ({Math.round(doc.bpm)} BPM)</button>
            <button className="btn btn-sm" onClick={() => setNext((n) => Math.max(0, n - 1))} disabled={next === 0}>Undo last tap</button>
          </div>
          <div className="faint" style={{ fontSize: 14 }}>
            Now at bar {Math.floor(Math.max(0, engine.beat) / (doc.beatsPerBar || 4)) + 1}
            {curItem >= 0 && target === "lines" && doc.lines[curItem] ? ` · “${doc.lines[curItem].text.slice(0, 40)}”` : ""}
          </div>
        </div>
      </div>
    </div>
  );
}
