import { useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { transposeKey, type Key } from "../../shared/music/notes";
import { simplifySymbol, transposeSymbol } from "../../shared/music/chords";
import { shapeFor } from "../../shared/music/capo";
import { songKey, weightedChords } from "../../shared/music/songKey";
import type { Instrument, SongDoc } from "../../shared/types";
import type { PlayerEngine, PlayerState } from "./engine";

export function usePlayerState(engine: PlayerEngine): PlayerState {
  return useSyncExternalStore(engine.subscribe, engine.getState);
}

/** Re-renders only when `select(beat)` changes (e.g. the current chord index). */
export function useBeatSelect<T>(engine: PlayerEngine, select: (beat: number) => T): T {
  const selectRef = useRef(select);
  selectRef.current = select;
  const [value, setValue] = useState(() => select(engine.beat));
  useEffect(() => {
    let last = selectRef.current(engine.beat);
    setValue(last);
    return engine.onTick((_t, beat) => {
      const v = selectRef.current(beat);
      if (v !== last) {
        last = v;
        setValue(v);
      }
    });
  }, [engine]);
  return value;
}

/** Calls `fn` every frame with the current beat (for smooth progress bars via refs). */
export function useTick(engine: PlayerEngine, fn: (time: number, beat: number) => void) {
  const ref = useRef(fn);
  ref.current = fn;
  useEffect(() => engine.onTick((t, b) => ref.current(t, b)), [engine]);
}

export interface ViewSettings {
  transpose: number;
  capo: number;
  instrument: Instrument;
  simplify: boolean;
  /** with a capo, show the shapes you finger (true) or the sounding chords (false) */
  capoShapes: boolean;
}

export interface ChordView {
  /** what the chord sounds as, after simplify + transpose */
  sounding: (symbol: string) => string;
  /** what to show / finger: the capo shape on fretted instruments */
  shown: (symbol: string) => string;
  /** the song's key before transposing (what its chords are named for); null when unknown */
  songKey: Key | null;
  /** the key the song sounds in, after transposing */
  soundKey: Key | null;
  /** the key of the shapes you play with the capo (E for F# with capo 2), else null */
  shapeKey: Key | null;
  /** the key the song sounds in */
  keyName: string;
  /** shapes key with the capo (e.g. "E" for F# with capo 2) */
  shapeKeyName: string | null;
  fretted: boolean;
  usesCapo: boolean;
}

export function useChordView(doc: SongDoc, s: ViewSettings): ChordView {
  // the key the chords are named for: the song's own, unless its chords say otherwise
  const key = useMemo(() => songKey(doc.key, weightedChords(doc.chords, doc.length)), [doc.key, doc.chords, doc.length]);
  return useMemo(() => {
    const fretted = s.instrument !== "piano";
    // bass players don't use a capo: bass always shows the notes as they sound
    const usesCapo = fretted && s.instrument !== "bass" && s.capo > 0;
    const soundKey = key && transposeKey(key, s.transpose);
    const shapeKey = soundKey && usesCapo ? transposeKey(soundKey, -s.capo) : null;
    // every chord is named for the key it sounds in (no key: the usual guitar names)
    const sounding = (sym: string) => transposeSymbol(s.simplify ? simplifySymbol(sym) : sym, s.transpose, soundKey ?? "common");
    const shown = (sym: string) => (usesCapo && s.capoShapes ? shapeFor(sounding(sym), s.capo, shapeKey) : sounding(sym));
    const keyName = soundKey ? soundKey.name : doc.key;
    return { sounding, shown, songKey: key, soundKey, shapeKey, keyName, shapeKeyName: shapeKey?.name ?? null, fretted, usesCapo };
  }, [key, doc.key, s.transpose, s.capo, s.instrument, s.simplify, s.capoShapes]);
}
