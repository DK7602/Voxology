// Downloads: MIDI, ChordPro and plain text, each with the player's current transpose / capo /
// simplify settings applied.
import type { SongDoc } from "../../shared/types";
import { chordOverLyricRows, exportChordPro } from "../../shared/chordpro";
import { songToMidi } from "../../shared/midi";
import { chordsInLine, gapAfterLine } from "../../shared/timeline";
import type { ChordView, ViewSettings } from "../player/hooks";
import { downloadBlob, safeFileName } from "../components/ui";

/** The song as it sounds with these settings (chords transposed / simplified). */
export function soundingDoc(doc: SongDoc, view: ChordView): SongDoc {
  return { ...doc, key: view.keyName, chords: doc.chords.map((c) => ({ ...c, name: view.sounding(c.name) })) };
}

export function downloadMidi(doc: SongDoc, view: ChordView) {
  const bytes = songToMidi(soundingDoc(doc, view), 0);
  downloadBlob(bytes, `${safeFileName(doc.title)}.mid`, "audio/midi");
}

export function downloadChordPro(doc: SongDoc, view: ChordView, s: ViewSettings) {
  const header = { ...doc, key: view.keyName, capo: view.usesCapo ? s.capo : 0 };
  const text = exportChordPro(header, view.shown);
  downloadBlob(text, `${safeFileName(doc.title)}.cho`, "text/plain;charset=utf-8");
}

export function chordSheetText(doc: SongDoc, view: ChordView, s: ViewSettings): string {
  const out: string[] = [];
  out.push(doc.title);
  if (doc.artist) out.push("by " + doc.artist);
  const meta = [`Key: ${view.keyName}`, `BPM: ${Math.round(doc.bpm)}`, `Time: ${doc.beatsPerBar}/4`];
  if (view.usesCapo) meta.push(`Capo: ${s.capo}${view.shapeKeyName ? ` (play ${view.shapeKeyName} shapes)` : ""}`);
  out.push(meta.join("   "), "");
  const firstLine = doc.lines[0]?.beat ?? Infinity;
  const intro = doc.chords.filter((c) => c.beat < firstLine);
  if (intro.length) out.push("[Intro]", intro.map((c) => view.shown(c.name)).join("  "), "");
  doc.lines.forEach((line, i) => {
    if (line.section) out.push((i ? "\n" : "") + `[${line.section}]`);
    const placed = chordsInLine(doc, i)
      .filter((p) => !p.carried || !line.text)
      .map((p) => ({ name: view.shown(p.chord.name), col: line.text ? p.col : 0 }));
    const gap = gapAfterLine(doc, i);
    const gapRow = gap ? gap.chords.filter((p) => !p.carried).map((p) => view.shown(p.chord.name)).join("  ") : "";
    if (!line.text) {
      out.push(placed.map((p) => p.name).join("  "));
      if (gapRow) out.push(gapRow);
      return;
    }
    const { chordRow, lyric } = chordOverLyricRows(line.text, placed);
    let row = "";
    for (const c of chordRow) row = row.padEnd(c.col, " ") + c.name;
    if (row.trim()) out.push(row);
    out.push(lyric.trimEnd());
    if (gapRow) out.push(gapRow);
  });
  return out.join("\n") + "\n";
}

export function downloadText(doc: SongDoc, view: ChordView, s: ViewSettings) {
  downloadBlob(chordSheetText(doc, view, s), `${safeFileName(doc.title)}.txt`, "text/plain;charset=utf-8");
}

export function printUrl(id: string, s: ViewSettings, auto = false): string {
  const p = new URLSearchParams({
    t: String(s.transpose),
    c: String(s.capo),
    i: s.instrument,
    s: s.simplify ? "1" : "0",
    sh: s.capoShapes ? "1" : "0",
  });
  if (auto) p.set("print", "1");
  return `/song/${encodeURIComponent(id)}/print?${p}`;
}
