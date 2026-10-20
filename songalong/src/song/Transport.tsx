import { prettyAccidentals, parseKey, transposeKey } from "../../shared/music/notes";
import type { PlayerEngine } from "../player/engine";
import { usePlayerState, type ChordView, type ViewSettings } from "../player/hooks";
import {
  IconCapo,
  IconChevronDown,
  IconDownload,
  IconMetronome,
  IconPause,
  IconPiano,
  IconPlay,
  IconPrev,
  IconPrinter,
  IconTimer,
  IconTranspose,
} from "../components/Icons";
import { Popover, Toggle } from "../components/ui";
import { indexAt, lineEnd } from "../../shared/timeline";
import { syncLabel } from "../player/sync";

interface Props {
  hasLead?: boolean;
  engine: PlayerEngine;
  view: ChordView;
  settings: ViewSettings;
  songKey: string;
  bestCapo: number | null;
  onSettings: (patch: Partial<ViewSettings>) => void;
  onMidi: () => void;
  onExport: (kind: "pdf" | "chordpro" | "text" | "midi") => void;
  onPrint: () => void;
}

const SPEEDS = [0.5, 0.6, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1, 1.05, 1.1, 1.25, 1.5];

export function Transport({ hasLead, engine, view, settings, songKey, bestCapo, onSettings, onMidi, onExport, onPrint }: Props) {
  const st = usePlayerState(engine);
  const doc = engine.timeline.doc;
  const bpm = Math.round(doc.bpm * st.speed);
  const rates = engine.media.rates();
  const speeds = rates ? SPEEDS.filter((s) => rates.some((r) => Math.abs(r - s) < 1e-6)) : SPEEDS;
  const key = parseKey(songKey);
  const keyAt = (t: number) => (key ? prettyAccidentals(transposeKey(key, t).name) : "");
  const playing = st.playing || st.countIn !== null;

  const loopRange = (kind: "song" | "bar" | "line" | "section") => {
    const b = engine.beat;
    const bpb = doc.beatsPerBar || 4;
    const length = engine.timeline.lengthBeats;
    if (kind === "song") return engine.setLoop({ on: true });
    if (kind === "bar") {
      const start = Math.floor(b / bpb) * bpb;
      return engine.setLoop({ on: true, from: start, to: start + bpb });
    }
    const li = Math.max(0, indexAt(doc.lines, b));
    if (kind === "line" && doc.lines.length) {
      return engine.setLoop({ on: true, from: doc.lines[li].beat, to: lineEnd(doc.lines, li, length) });
    }
    if (kind === "section" && doc.lines.length) {
      let s = li;
      while (s > 0 && !doc.lines[s].section) s--;
      let e = li + 1;
      while (e < doc.lines.length && !doc.lines[e].section) e++;
      return engine.setLoop({ on: true, from: doc.lines[s].beat, to: e < doc.lines.length ? doc.lines[e].beat : length });
    }
  };
  const loopLabel = !st.loop.on
    ? "Off"
    : st.loop.from === undefined
      ? "Song"
      : `Bars ${Math.floor(st.loop.from / (doc.beatsPerBar || 4)) + 1}–${Math.ceil((st.loop.to ?? 0) / (doc.beatsPerBar || 4))}`;

  return (
    <div className="transport inset" role="toolbar" aria-label="Playback">
      <div className="t-group">
        <button className="t-prev" onClick={() => engine.skipBars(-1)} aria-label="Back one bar (or to the start of this bar)">
          <IconPrev size={28} />
        </button>
        <button className="play-btn" onClick={() => engine.toggle()} aria-label={playing ? "Pause" : "Play"}>
          {playing ? <IconPause /> : <IconPlay />}
        </button>
        <Popover
          width={270}
          trigger={(p) => (
            <button className={"countin" + (st.countInBars ? "" : " off")} {...p} ref={p.ref} title="Count-in and sync">
              <IconTimer size={24} />
              <span className="countin-text">{st.countInBars ? (doc.beatsPerBar === 3 ? "3, 2, 1..." : "4, 3, 2, 1...") : "No count-in"}</span>
            </button>
          )}
        >
          {(close) => (
            <>
              <div className="menu-label">Count-in</div>
              {[0, 1, 2].map((n) => (
                <button key={n} className={"menu-item" + (st.countInBars === n ? " selected" : "")} onClick={() => { engine.setCountIn(n); close(); }}>
                  {n === 0 ? "Off" : `${n} bar${n > 1 ? "s" : ""} of clicks`}
                </button>
              ))}
              <div className="menu-sep" />
              <div className="menu-label">Sync words &amp; chords to the sound</div>
              <div className="menu-item sync-row" style={{ cursor: "default" }}>
                <button className="btn btn-sm" onClick={() => engine.setSync(st.sync - 0.05)} aria-label="Show words and chords earlier">−</button>
                <span className="sync-val" aria-live="polite">{syncLabel(st.sync)}</span>
                <button className="btn btn-sm" onClick={() => engine.setSync(st.sync + 0.05)} aria-label="Show words and chords later">+</button>
              </div>
              {Math.abs(st.sync) >= 0.005 && (
                <button className="menu-item" onClick={() => engine.setSync(0)}>Reset</button>
              )}
              <div className="menu-item faint" style={{ cursor: "default", fontSize: 14 }}>
                Words ahead of the singing? Press + until they match. Bluetooth speakers and headphones add a delay. Kept on this device.
              </div>
            </>
          )}
        </Popover>
      </div>

      <div className="t-group">
        <div className="t-loop">
          <span>Loop</span>
          <Toggle checked={st.loop.on} onChange={(on) => engine.setLoop({ ...st.loop, on })} label="Loop" />
        </div>
        <Popover
          width={250}
          trigger={(p) => (
            <button className="loop-more" {...p} ref={p.ref} aria-label={"Loop range: " + loopLabel} title={"Loop: " + loopLabel}>
              <IconChevronDown size={18} />
            </button>
          )}
        >
          {(close) => (
            <>
              <div className="menu-label">Loop · now: {loopLabel}</div>
              {([["song", "Whole song"], ["section", "This section"], ["line", "This line"], ["bar", "This bar"]] as const).map(([k, label]) => (
                <button key={k} className="menu-item" onClick={() => { loopRange(k); close(); }}>{label}</button>
              ))}
              <div className="menu-sep" />
              <div className="menu-item faint" style={{ cursor: "default", fontSize: 14 }}>Tip: shift-click two bars in the chord strip to loop them.</div>
              {st.loop.on && (
                <button className="menu-item" onClick={() => { engine.setLoop({ on: false }); close(); }}>Turn loop off</button>
              )}
            </>
          )}
        </Popover>
      </div>

      <div className="t-group">
        <Popover
          width={190}
          trigger={(p) => (
            <button className="t-drop" {...p} ref={p.ref} aria-label="Speed">
              <span className="t-label">
                Speed
                <span className="val">{Math.round(st.speed * 100)}% <IconChevronDown size={16} /></span>
              </span>
            </button>
          )}
        >
          {(close) => (
            <>
              <div className="menu-label">Playback speed</div>
              {speeds.map((s) => (
                <button key={s} className={"menu-item" + (Math.abs(st.speed - s) < 1e-6 ? " selected" : "")} onClick={() => { engine.setSpeed(s); close(); }}>
                  {Math.round(s * 100)}% <span className="faint" style={{ marginLeft: "auto" }}>{Math.round(doc.bpm * s)} BPM</span>
                </button>
              ))}
            </>
          )}
        </Popover>
      </div>

      <div className="t-group">
        <Popover
          width={240}
          trigger={(p) => (
            <button className="t-drop" {...p} ref={p.ref} aria-label="Tempo and metronome">
              <IconMetronome size={30} />
              <span className="t-label">
                BPM
                <span className="val">{bpm} <IconChevronDown size={16} /></span>
              </span>
            </button>
          )}
        >
          {() => (
            <>
              <div className="menu-label">Tempo</div>
              <div className="menu-item" style={{ cursor: "default" }}>
                Song {Math.round(doc.bpm)} BPM · playing at {bpm}
              </div>
              <div className="menu-item" style={{ justifyContent: "space-between" }}>
                Metronome click
                <Toggle checked={st.metronome} onChange={(v) => engine.setMetronome(v)} label="Metronome" />
              </div>
              <div className="menu-item" style={{ justifyContent: "space-between" }}>
                Play the chords (rhythm)
                <Toggle checked={st.backing} onChange={(v) => engine.setBacking(v)} label="Backing chords" />
              </div>
              {hasLead && (
                <div className="menu-item" style={{ justifyContent: "space-between" }}>
                  Play the lead (guide)
                  <Toggle checked={st.leadGuide} onChange={(v) => engine.setLeadGuide(v)} label="Lead guide" />
                </div>
              )}
              <div className="menu-item faint" style={{ cursor: "default", fontSize: 14 }}>Change the tempo with Speed.</div>
            </>
          )}
        </Popover>
      </div>

      <div className="t-group">
        <Popover
          width={300}
          trigger={(p) => (
            <button className="t-drop" {...p} ref={p.ref} aria-label="Capo">
              <IconCapo size={30} />
              <span className="t-label">
                Capo
                <span className="val">{settings.capo || "None"} <IconChevronDown size={16} /></span>
              </span>
            </button>
          )}
        >
          {(close) => (
            <>
              <div className="menu-label">Capo (guitar, ukulele, mandolin)</div>
              {bestCapo !== null && (
                <button className="menu-item" onClick={() => { onSettings({ capo: bestCapo }); close(); }}>
                  ★ Easiest shapes: {bestCapo ? `capo ${bestCapo}` : "no capo"}
                </button>
              )}
              {doc.capo > 0 && (
                <button className="menu-item" onClick={() => { onSettings({ capo: doc.capo }); close(); }}>
                  Song's capo: {doc.capo}
                </button>
              )}
              <div className="menu-sep" />
              <div style={{ display: "grid", gridTemplateColumns: "repeat(5, 1fr)", gap: 4, padding: 4 }}>
                {Array.from({ length: 10 }, (_, n) => (
                  <button key={n} className={"btn btn-sm" + (settings.capo === n ? " active" : "")} onClick={() => { onSettings({ capo: n }); close(); }}>
                    {n}
                  </button>
                ))}
              </div>
              <div className="menu-item" style={{ justifyContent: "space-between" }}>
                Show shapes (not sounding chords)
                <Toggle checked={settings.capoShapes} onChange={(v) => onSettings({ capoShapes: v })} label="Show capo shapes" />
              </div>
              {view.shapeKeyName && (
                <div className="menu-item faint" style={{ cursor: "default", fontSize: 14 }}>
                  Sounds in {prettyAccidentals(view.keyName)}; you play {prettyAccidentals(view.shapeKeyName)} shapes.
                </div>
              )}
            </>
          )}
        </Popover>
      </div>

      <div className="t-group">
        <Popover
          width={230}
          trigger={(p) => (
            <button className="t-drop" {...p} ref={p.ref} aria-label="Transpose">
              <IconTranspose size={28} />
              <span className="t-label">
                Transpose
                <span className="val">
                  {settings.transpose > 0 ? "+" : ""}
                  {settings.transpose} ({prettyAccidentals(view.keyName)}) <IconChevronDown size={16} />
                </span>
              </span>
            </button>
          )}
        >
          {(close) => (
            <>
              <div className="menu-label">Transpose (semitones)</div>
              {Array.from({ length: 13 }, (_, i) => 6 - i).map((t) => (
                <button key={t} className={"menu-item" + (settings.transpose === t ? " selected" : "")} onClick={() => { onSettings({ transpose: t }); close(); }}>
                  {t > 0 ? "+" : ""}
                  {t} <span className="faint" style={{ marginLeft: "auto" }}>{keyAt(t)}</span>
                </button>
              ))}
              {doc.media.kind !== "none" && (
                <div className="menu-item faint" style={{ cursor: "default", fontSize: 14 }}>The recording stays in its key; the chords move.</div>
              )}
            </>
          )}
        </Popover>
      </div>

      <div className="t-group">
        <button className="t-tool" onClick={onMidi} title="Download a MIDI file of the chords">
          <IconPiano />
          <span>MIDI</span>
        </button>
        <Popover
          align="end"
          width={230}
          trigger={(p) => (
            <button className="t-tool" {...p} ref={p.ref} title="Export">
              <IconDownload />
              <span>Export</span>
            </button>
          )}
        >
          {(close) => (
            <>
              <div className="menu-label">Export with current settings</div>
              <button className="menu-item" onClick={() => { close(); onExport("pdf"); }}>PDF chord sheet</button>
              <button className="menu-item" onClick={() => { close(); onExport("chordpro"); }}>ChordPro (.cho)</button>
              <button className="menu-item" onClick={() => { close(); onExport("text"); }}>Plain text (.txt)</button>
              <button className="menu-item" onClick={() => { close(); onExport("midi"); }}>MIDI (.mid)</button>
            </>
          )}
        </Popover>
        <button className="t-tool" onClick={onPrint} title="Print the chord sheet">
          <IconPrinter />
          <span>Print</span>
        </button>
      </div>
    </div>
  );
}
