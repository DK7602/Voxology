// Putting lyrics on the video's clock. LRCLIB's timing belongs to the record; the video may have
// an intro or a different edit, so a few pieces of the song are run through speech recognition
// and matched to the lyric lines to find the offset. When there is no timing at all, the plain
// lyrics are matched line by line, and with no lyrics the recognised words become the lyrics.

import type { PrepChunk } from "../shared/prep";
import type { TimedLine } from "./lyrics";

export interface Segment {
  start: number;
  end: number;
  text: string;
}

/** What we need from the Workers AI binding (kept narrow so tests can fake it). */
export interface SpeechAi {
  run(model: string, input: Record<string, unknown>): Promise<unknown>;
}

const MODEL = "@cf/openai/whisper-large-v3-turbo";

export async function transcribe(ai: SpeechAi, chunk: PrepChunk): Promise<Segment[]> {
  try {
    const res = (await ai.run(MODEL, {
      audio: chunk.mp3,
      task: "transcribe",
      vad_filter: true,
      condition_on_previous_text: false,
    })) as { segments?: { start: number; end: number; text: string }[]; text?: string };
    const segs = res.segments ?? [];
    return segs
      .filter((s) => typeof s.text === "string" && s.text.trim())
      .map((s) => ({ start: chunk.start + s.start, end: chunk.start + s.end, text: s.text.trim() }));
  } catch (e) {
    console.warn("speech recognition failed for a chunk", (e as Error).message);
    return [];
  }
}

export function words(text: string): string[] {
  return text
    .toLowerCase()
    .normalize("NFKD")
    .replace(/[̀-ͯ]/g, "")
    .replace(/[^a-z0-9' ]+/g, " ")
    .split(/\s+/)
    .map((w) => w.replace(/^'+|'+$/g, ""))
    .filter((w) => w.length > 0);
}

/** Word-overlap similarity (Dice), 0..1. */
export function similarity(a: string, b: string): number {
  const wa = words(a);
  const wb = words(b);
  if (!wa.length || !wb.length) return 0;
  const counts = new Map<string, number>();
  for (const w of wa) counts.set(w, (counts.get(w) ?? 0) + 1);
  let common = 0;
  for (const w of wb) {
    const n = counts.get(w) ?? 0;
    if (n > 0) {
      common++;
      counts.set(w, n - 1);
    }
  }
  return (2 * common) / (wa.length + wb.length);
}

/** Offset (video time - lyric time) that the most line / segment matches agree on. */
export function estimateOffset(lines: TimedLine[], segs: Segment[]): { offset: number; matches: number } {
  const candidates: number[] = [];
  for (const line of lines) {
    if (words(line.text).length < 2) continue;
    for (const s of segs) {
      if (similarity(line.text, s.text) >= 0.5) candidates.push(s.start - line.time);
    }
  }
  if (!candidates.length) return { offset: 0, matches: 0 };
  let best: number[] = [];
  for (const c of candidates) {
    const cluster = candidates.filter((x) => Math.abs(x - c) <= 1.25);
    if (cluster.length > best.length || (cluster.length === best.length && Math.abs(median(cluster)) < Math.abs(median(best)))) best = cluster;
  }
  return { offset: median(best), matches: best.length };
}

function median(xs: number[]): number {
  if (!xs.length) return 0;
  const s = [...xs].sort((a, b) => a - b);
  const m = s.length >> 1;
  return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
}

/** Runs speech recognition on the chunks at these indexes, a few at a time. */
async function transcribeSome(ai: SpeechAi, chunks: PrepChunk[], idx: number[]): Promise<Segment[]> {
  const out: Segment[] = [];
  for (let i = 0; i < idx.length; i += 3) {
    const batch = idx.slice(i, i + 3).filter((k) => k >= 0 && k < chunks.length);
    const results = await Promise.all(batch.map((k) => transcribe(ai, chunks[k])));
    out.push(...results.flat());
  }
  return out.sort((a, b) => a.start - b.start);
}

export type LyricsTiming = "synced" | "aligned" | "transcribed" | "none";

export interface AlignedLyrics {
  lines: TimedLine[]; // video time
  timing: LyricsTiming;
}

/** Timed (LRC) lyrics -> video time. Listens to the pieces where the singing should start, and a
 *  few more if needed, until enough lines are heard to agree on the offset. */
export async function alignSynced(lines: TimedLine[], chunks: PrepChunk[], ai: SpeechAi | undefined, chunkSeconds = 30): Promise<AlignedLyrics> {
  if (!ai || !chunks.length) return { lines, timing: "synced" };
  const firstSung = lines.find((l) => l.text)?.time ?? 0;
  const first = Math.floor(firstSung / chunkSeconds);
  const order = [first, first + 1, first + 2, first - 1, first + 3, first + 4, Math.floor(chunks.length / 2)];
  const tried = new Set<number>();
  const segs: Segment[] = [];
  for (let round = 0; round < order.length; round += 2) {
    const next = order.slice(round, round + 2).filter((k) => k >= 0 && k < chunks.length && !tried.has(k));
    next.forEach((k) => tried.add(k));
    segs.push(...(await transcribeSome(ai, chunks, next)));
    const est = estimateOffset(lines, segs);
    if (est.matches >= 3) return { lines: lines.map((l) => ({ ...l, time: l.time + est.offset })), timing: "aligned" };
    if (tried.size >= 6) break;
  }
  const est = estimateOffset(lines, segs);
  if (est.matches >= 1) return { lines: lines.map((l) => ({ ...l, time: l.time + est.offset })), timing: "aligned" };
  return { lines, timing: "synced" };
}

/** Plain lyrics (no timing) -> lines placed where speech recognition hears them, in order. */
export async function alignPlain(plain: string[], chunks: PrepChunk[], ai: SpeechAi | undefined): Promise<AlignedLyrics> {
  const sung = plain.filter((l) => l.trim());
  if (!ai || !chunks.length) return { lines: [], timing: "none" };
  const segs = await transcribeSome(ai, chunks, chunks.map((_, i) => i));
  return { lines: matchInOrder(sung, segs), timing: "aligned" };
}

/** Greedy in-order matching of lines to segments; unmatched lines are spread between neighbours. */
export function matchInOrder(lines: string[], segs: Segment[]): TimedLine[] {
  const times: (number | null)[] = lines.map(() => null);
  let s = 0;
  lines.forEach((line, i) => {
    let best = -1;
    let bestSim = 0.45;
    for (let k = s; k < Math.min(segs.length, s + 8); k++) {
      const sim = similarity(line, segs[k].text);
      if (sim > bestSim) {
        bestSim = sim;
        best = k;
      }
    }
    if (best >= 0) {
      times[i] = segs[best].start;
      s = best + 1;
    }
  });
  const known = times.map((t, i) => ({ t, i })).filter((x): x is { t: number; i: number } => x.t !== null);
  if (!known.length) return [];
  return lines.map((text, i) => {
    if (times[i] !== null) return { time: times[i]!, text };
    const before = [...known].reverse().find((k) => k.i < i);
    const after = known.find((k) => k.i > i);
    let time: number;
    if (before && after) time = before.t + ((after.t - before.t) * (i - before.i)) / (after.i - before.i);
    else if (before) time = before.t + (i - before.i) * 3;
    else time = Math.max(0, after!.t - (after!.i - i) * 3);
    return { time, text };
  });
}

/** No lyrics found anywhere: the words speech recognition hears become the lyric lines. */
export async function transcribeAll(chunks: PrepChunk[], ai: SpeechAi | undefined): Promise<AlignedLyrics> {
  if (!ai || !chunks.length) return { lines: [], timing: "none" };
  const segs = await transcribeSome(ai, chunks, chunks.map((_, i) => i));
  return { lines: segs.map((s) => ({ time: s.start, text: s.text })), timing: "transcribed" };
}
