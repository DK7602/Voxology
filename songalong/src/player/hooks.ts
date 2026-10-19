import { useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { parseKey, transposeKey } from "../../shared/music/notes";
import { simplifySymbol, spellingForKey, transposeSymbol } from "../../shared/music/chords";
import { shapeFor } from "../../shared/music/capo";
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
  /** the key the song sounds in */
  keyName: string;
  /** shapes key with the capo (e.g. "E" for Gb with capo 2) */
  shapeKeyName: string | null;
  fretted: boolean;
  usesCapo: boolean;
}

export function useChordView(doc: SongDoc, s: ViewSettings): ChordView {
  return useMemo(() => {
    const spelling = spellingForKey(doc.key, s.transpose);
    const fretted = s.instrument !== "piano";
    const usesCapo = fretted && s.capo > 0;
    const sounding = (sym: string) => transposeSymbol(s.simplify ? simplifySymbol(sym) : sym, s.transpose, spelling);
    const shown = (sym: string) => (usesCapo && s.capoShapes ? shapeFor(sounding(sym), s.capo) : sounding(sym));
    const key = parseKey(doc.key);
    const keyT = key ? transposeKey(key, s.transpose) : null;
    const keyName = keyT ? (s.transpose ? keyT.name : key!.name) : doc.key;
    const shapeKey = keyT && usesCapo ? transposeKey(keyT, -s.capo).name : null;
    return { sounding, shown, keyName, shapeKeyName: shapeKey, fretted, usesCapo };
  }, [doc.key, s.transpose, s.capo, s.instrument, s.simplify, s.capoShapes]);
}
