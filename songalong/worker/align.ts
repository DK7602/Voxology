// Putting lyrics on the video's clock. LRCLIB's timing belongs to the record; the video may have
// an intro or a different edit, so the song is run through speech recognition and the lyric lines
// are matched to what is heard to find the offset. When there is no timing at all, the plain
// lyrics are matched line by line, and with no lyrics the recognised words become the lyrics.
// Last, every lyric word is matched to the heard words, so the lyrics can follow the singing.

import type { PrepChunk } from "../shared/prep";
import { wordStarts } from "../shared/timeline";
import type { TimedLine } from "./lyrics";

export interface Segment {
  start: number;
  end: number;
  text: string;
  /** when each word starts: much closer than the segment's start, which often falls in the
   *  music before the singing, or a line or two early when a segment holds several lines */
  words?: { word: string; start: number; end?: number }[];
}

/** One heard word (as words() spells it), when it starts and ends. */
export interface HeardWord {
  text: string;
  start: number;
  end: number;
  /** the first word of a heard phrase: its start may lie in the music before it */
  lead?: boolean;
}

/** What we need from the Workers AI binding (kept narrow so tests can fake it). */
export interface SpeechAi {
  run(model: string, input: Record<string, unknown>): Promise<unknown>;
}

const MODEL = "@cf/openai/whisper-large-v3-turbo";

type WhisperSegment = {
  start: number;
  end: number;
  text: string;
  words?: { word: string; start: number; end: number }[];
  no_speech_prob?: number;
  avg_logprob?: number;
  compression_ratio?: number;
};
type WhisperResult = { segments?: WhisperSegment[]; text?: string };

/** A phrase the model most likely made up over the music: it thinks there was no speech and isn't
 *  sure of the words, or the words keep repeating themselves. */
function madeUp(s: WhisperSegment): boolean {
  if ((s.compression_ratio ?? 0) > 2.4) return true;
  return (s.no_speech_prob ?? 0) > 0.6 && (s.avg_logprob ?? 0) < -1;
}

/** Speech recognition for one piece of the song. The model's voice filter (VAD) is left off: it
 *  is made for speech and drops most singing over instruments. What the model makes up over the
 *  music instead is left out by its own confidence. */
export async function transcribe(ai: SpeechAi, chunk: PrepChunk): Promise<Segment[]> {
  try {
    const res = (await ai.run(MODEL, {
      audio: chunk.mp3,
      task: "transcribe",
      vad_filter: false,
      condition_on_previous_text: false,
    })) as WhisperResult;
    // no segment timing but some words: one segment for the piece
    if (!res.segments?.length) return res.text?.trim() ? [{ start: chunk.start, end: chunk.start + 30, text: res.text.trim() }] : [];
    return res.segments
      .filter((s) => typeof s.text === "string" && s.text.trim() && !madeUp(s))
      .map((s) => {
        const seg: Segment = { start: chunk.start + s.start, end: chunk.start + s.end, text: s.text.trim() };
        const timed = (s.words ?? []).filter((w) => typeof w.word === "string" && Number.isFinite(w.start));
        if (timed.length) {
          seg.words = timed.map((w) => ({
            word: w.word,
            start: chunk.start + w.start,
            ...(Number.isFinite(w.end) ? { end: chunk.start + w.end } : {}),
          }));
        }
        return seg;
      });
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
  return dice(words(a), words(b));
}

function dice(wa: string[], wb: string[]): number {
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

/** Every heard word in order, with when it starts. A segment without word times has its words
 *  spread evenly over it. */
export function heardWords(segs: Segment[]): HeardWord[] {
  const out: HeardWord[] = [];
  for (const s of segs) {
    if (s.words?.length) {
      s.words.forEach((w, i) => {
        const end = w.end ?? s.words![i + 1]?.start ?? s.end;
        for (const text of words(w.word)) out.push({ text, start: w.start, end: Math.max(w.start, end), lead: i === 0 });
      });
      continue;
    }
    const ws = words(s.text);
    const step = s.end > s.start ? (s.end - s.start) / ws.length : 0;
    ws.forEach((text, i) => out.push({ text, start: s.start + i * step, end: s.start + (i + 1) * step, lead: i === 0 }));
  }
  return out.sort((a, b) => a.start - b.start);
}

/**
 * Where a lyric line is heard: the time its first word is sung, and how well the words there match
 * the line (0..1). The line may start at its first word, or at its second when the first was
 * misheard.
 */
export function lineHits(line: string, heard: HeardWord[], min = 0.6): { time: number; sim: number }[] {
  const want = words(line);
  if (want.length < 2) return [];
  const hits: { time: number; sim: number }[] = [];
  for (let i = 0; i < heard.length; i++) {
    const skip = heard[i].text === want[0] ? 0 : heard[i].text === want[1] ? 1 : -1;
    if (skip < 0) continue;
    const rest = want.slice(skip);
    const sim = dice(rest, heard.slice(i, i + rest.length + 1).map((w) => w.text));
    if (sim < min) continue;
    // a misheard first word was most likely the word heard just before
    const prev = heard[i - 1];
    const time = skip === 0 ? heard[i].start : prev && heard[i].start - prev.start < 1.5 ? prev.start : heard[i].start - 0.3;
    const last = hits[hits.length - 1];
    if (last && time - last.time < 1) {
      if (sim > last.sim) hits[hits.length - 1] = { time, sim };
      continue;
    }
    hits.push({ time, sim });
  }
  return hits;
}

/** Offset (video time - lyric time) that the most places where lines are heard agree on. */
export function estimateOffset(lines: TimedLine[], segs: Segment[]): { offset: number; matches: number } {
  const heard = heardWords(segs);
  const candidates: number[] = [];
  for (const line of lines) for (const hit of lineHits(line.text, heard)) candidates.push(hit.time - line.time);
  if (!candidates.length) return { offset: 0, matches: 0 };
  let best: number[] = [];
  for (const c of candidates) {
    const cluster = candidates.filter((x) => Math.abs(x - c) <= 1);
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

/** Runs speech recognition over the whole song, a few pieces at a time. */
export async function listen(chunks: PrepChunk[], ai: SpeechAi | undefined): Promise<Segment[]> {
  if (!ai) return [];
  const out: Segment[] = [];
  for (let i = 0; i < chunks.length; i += 4) {
    const results = await Promise.all(chunks.slice(i, i + 4).map((c) => transcribe(ai, c)));
    out.push(...results.flat());
  }
  return out.sort((a, b) => a.start - b.start);
}

export type LyricsTiming = "synced" | "aligned" | "transcribed" | "none";

export interface AlignedLyrics {
  lines: TimedLine[]; // video time
  timing: LyricsTiming;
}

/**
 * Whether one offset explains where the lines are heard: most lines that are heard at all are
 * heard where that offset puts them. Then the video is the record itself, with more or less before
 * or after it (an official video's intro), whatever its length.
 */
export function oneOffsetFits(lines: TimedLine[], segs: Segment[]): boolean {
  const heard = heardWords(segs);
  const { offset } = estimateOffset(lines, segs);
  let heardLines = 0;
  let agree = 0;
  for (const line of lines) {
    const hits = lineHits(line.text, heard);
    if (!hits.length) continue;
    heardLines++;
    if (hits.some((h) => Math.abs(h.time - line.time - offset) <= 1.5)) agree++;
  }
  return agree >= 3 && agree >= 0.6 * heardLines;
}

/** Timed (LRC) lyrics -> video time: the offset most heard lines agree on. */
export function alignSynced(lines: TimedLine[], segs: Segment[]): AlignedLyrics {
  const est = estimateOffset(lines, segs);
  if (est.matches >= 1) return { lines: lines.map((l) => ({ ...l, time: l.time + est.offset })), timing: "aligned" };
  return { lines, timing: "synced" };
}

/**
 * Timed lyrics from a different recording (a cover, a live or shortened take): the words are right
 * but not the timing. The whole video is listened to; lines heard become anchors (lyric time ->
 * video time, kept in order), and the lyrics' own timing is stretched between them. With no
 * anchor at all, the timing is scaled to the video's length.
 */
export function alignVersion(lines: TimedLine[], segs: Segment[], lyricDuration: number, videoDuration: number): AlignedLyrics {
  const ratio = lyricDuration > 0 && videoDuration > 0 ? videoDuration / lyricDuration : 1;
  const scaled = () => lines.map((l) => ({ ...l, time: l.time * ratio }));
  const anchors = anchorChain(lines, segs);
  if (!anchors.length || !steadyChain(anchors)) return { lines: scaled(), timing: "synced" };
  return { lines: versionLines(lines, anchors, ratio, videoDuration), timing: "aligned" };
}

/**
 * Places the lyric lines on the video through the anchors. A stretch of the lyrics that the video
 * gets through at under a third of the record's pace is a part this version leaves out (a verse,
 * a repeated chorus): its lines are dropped rather than crammed together. Ends follow the
 * version's usual pace.
 */
export function versionLines(lines: TimedLine[], anchors: Anchor[], ratio: number, videoDuration: number): TimedLine[] {
  const stretchRate = (k: number) => (anchors[k + 1].video - anchors[k].video) / Math.max(1e-6, anchors[k + 1].lyric - anchors[k].lyric);
  const rates = anchors.slice(1).map((_, k) => stretchRate(k)).filter((r) => r >= 0.5 && r <= 2).sort((a, b) => a - b);
  const pace = rates.length ? rates[rates.length >> 1] : ratio;
  const anchored = new Set(anchors.map((a) => a.lyric));
  const out: TimedLine[] = [];
  for (const l of lines) {
    if (!anchored.has(l.time) && anchors.length >= 2) {
      let k = -1;
      for (let i = 0; i + 1 < anchors.length; i++) if (anchors[i].lyric < l.time && l.time < anchors[i + 1].lyric) k = i;
      if (k >= 0 && stretchRate(k) < 0.33) continue; // not sung in this version
    }
    const time = warp(anchors, l.time, pace);
    if (time < -1 || (videoDuration > 0 && time > videoDuration + 1)) continue;
    out.push({ ...l, time: Math.max(0, time) });
  }
  return out;
}

export interface Anchor {
  lyric: number;
  video: number;
}

/**
 * Whether the anchors read like someone singing the song: from one to the next, the video mostly
 * keeps a believable pace against the record (a version may still skip a part). Words "heard" in
 * music without singing (the model knows the tune) give anchors that jump about.
 */
export function steadyChain(anchors: Anchor[]): boolean {
  const rates = anchors.slice(1).map((a, k) => (a.video - anchors[k].video) / Math.max(1e-6, a.lyric - anchors[k].lyric));
  if (rates.length < 2) return true;
  return rates.filter((r) => r >= 0.5 && r <= 2).length >= 0.6 * rates.length;
}

/** The longest run of places where lines are heard that moves forward in both clocks. */
export function anchorChain(lines: TimedLine[], segs: Segment[]): Anchor[] {
  const heard = heardWords(segs);
  const pairs: (Anchor & { sim: number })[] = [];
  for (const line of lines) {
    for (const hit of lineHits(line.text, heard, 0.5)) pairs.push({ lyric: line.time, video: hit.time, sim: hit.sim });
  }
  pairs.sort((a, b) => a.lyric - b.lyric || a.video - b.video);
  const n = pairs.length;
  const score = new Array<number>(n).fill(0);
  const prev = new Array<number>(n).fill(-1);
  let bestEnd = -1;
  for (let i = 0; i < n; i++) {
    score[i] = pairs[i].sim;
    for (let j = 0; j < i; j++) {
      if (pairs[j].lyric < pairs[i].lyric && pairs[j].video < pairs[i].video && score[j] + pairs[i].sim > score[i]) {
        score[i] = score[j] + pairs[i].sim;
        prev[i] = j;
      }
    }
    if (bestEnd < 0 || score[i] > score[bestEnd]) bestEnd = i;
  }
  const chain: Anchor[] = [];
  for (let i = bestEnd; i >= 0; i = prev[i]) chain.push({ lyric: pairs[i].lyric, video: pairs[i].video });
  return chain.reverse();
}

/** Lyric time -> video time through the anchors (straight lines between them; beyond the ends,
 *  the nearest stretch's rate, kept sensible). */
export function warp(anchors: Anchor[], t: number, fallbackRate = 1): number {
  const rate = (a: Anchor, b: Anchor) => {
    const r = (b.video - a.video) / Math.max(1e-6, b.lyric - a.lyric);
    return r > 0.5 && r < 2 ? r : fallbackRate;
  };
  if (anchors.length === 1) return anchors[0].video + (t - anchors[0].lyric) * fallbackRate;
  const first = anchors[0];
  const last = anchors[anchors.length - 1];
  if (t <= first.lyric) return first.video + (t - first.lyric) * rate(first, anchors[1]);
  if (t >= last.lyric) return last.video + (t - last.lyric) * rate(anchors[anchors.length - 2], last);
  let k = 0;
  while (anchors[k + 1].lyric < t) k++;
  const a = anchors[k];
  const b = anchors[k + 1];
  return a.video + ((t - a.lyric) * (b.video - a.video)) / Math.max(1e-6, b.lyric - a.lyric);
}

/** Plain lyrics (no timing) -> lines placed where speech recognition hears them, in order. */
export function alignPlain(plain: string[], segs: Segment[]): AlignedLyrics {
  if (!segs.length) return { lines: [], timing: "none" };
  return { lines: matchInOrder(plain.filter((l) => l.trim()), segs), timing: "aligned" };
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
      // where in the segment the line starts
      const hits = lineHits(line, heardWords([segs[best]]), 0.3).sort((a, b) => b.sim - a.sim);
      times[i] = hits[0]?.time ?? segs[best].start;
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
export function transcribeAll(segs: Segment[]): AlignedLyrics {
  if (!segs.length) return { lines: [], timing: "none" };
  return { lines: segs.map((s) => ({ time: s.words?.[0]?.start ?? s.start, text: s.text })), timing: "transcribed" };
}

/** How far (seconds) a word may be heard from where its line's timing expects it. */
const WORD_WINDOW = 6;

/** A word as it is compared: no apostrophes, and "-ing" sung as "-in'". */
function matchKey(w: string): string {
  return w.replace(/'/g, "").replace(/ing$/, "in");
}

function wordMatch(a: string, b: string): number {
  if (a === b) return 1;
  if (a.length >= 4 && b.length >= 4 && (a.startsWith(b) || b.startsWith(a) || editDistance1(a, b))) return 0.7;
  return 0;
}

/** At most one letter added, dropped or changed. */
function editDistance1(a: string, b: string): boolean {
  if (Math.abs(a.length - b.length) > 1) return false;
  let i = 0;
  while (i < a.length && i < b.length && a[i] === b[i]) i++;
  if (a.length === b.length) return a.slice(i + 1) === b.slice(i + 1);
  return a.length > b.length ? a.slice(i + 1) === b.slice(i) : a.slice(i) === b.slice(i + 1);
}

/** When a heard word starts. A phrase's first word that lasts long has most likely been given the
 *  music before it, so it is taken to start shortly before it ends. */
/** Is the word starting at `at` in brackets: a backing vocal, "...turning back (thunder)"? */
function inBrackets(text: string, at: number): boolean {
  const upTo = text.slice(0, at + 1);
  return upTo.lastIndexOf("(") > upTo.lastIndexOf(")");
}

/** a backing vocal after the last word heard comes at least this long after that word ends */
const ANSWER_GAP = 0.15;

function sungStart(h: HeardWord): number {
  if (h.lead && h.end - h.start > 1) return h.end - Math.min(h.end - h.start, 0.3 + 0.06 * h.text.length);
  return h.start;
}

/**
 * Times every lyric word by matching the lyrics, word by word and in order, to what speech
 * recognition heard (each word only near where its line is expected). A line with enough of its
 * words heard gets `words` (and starts at its first word); the words in between that weren't
 * heard are spread by their place in the text. Lines barely heard keep their timing.
 */
export function timeWords(lines: TimedLine[], segs: Segment[]): TimedLine[] {
  const heard = heardWords(segs);
  if (!heard.length || !lines.length) return lines;
  const keys = heard.map((h) => matchKey(h.text));
  const want: { key: string; expect: number }[] = [];
  const layout = lines.map((l, li) => {
    const starts = wordStarts(l.text);
    const next = lines[li + 1]?.time ?? l.time + 8;
    const span = Math.max(0.5, Math.min(next - l.time, 0.07 * l.text.length + 0.6));
    for (const st of starts) {
      const token = l.text.slice(st).split(/\s/, 1)[0];
      want.push({ key: matchKey(words(token)[0] ?? ""), expect: l.time + (span * st) / Math.max(1, l.text.length) });
    }
    return starts;
  });

  // the best in-order matching (longest common subsequence, weighted by how well words match)
  const n = want.length;
  const m = heard.length;
  const W = m + 1;
  const score = new Float32Array((n + 1) * W);
  const move = new Uint8Array((n + 1) * W); // 1: lyric word not heard, 2: heard word not in the lyrics, 3: match
  for (let i = 1; i <= n; i++) {
    const a = want[i - 1];
    for (let j = 1; j <= m; j++) {
      let best = score[(i - 1) * W + j];
      let mv = 1;
      if (score[i * W + j - 1] > best) {
        best = score[i * W + j - 1];
        mv = 2;
      }
      if (a.key && Math.abs(heard[j - 1].start - a.expect) <= WORD_WINDOW) {
        const s = wordMatch(a.key, keys[j - 1]);
        if (s > 0 && score[(i - 1) * W + j - 1] + s > best) {
          best = score[(i - 1) * W + j - 1] + s;
          mv = 3;
        }
      }
      score[i * W + j] = best;
      move[i * W + j] = mv;
    }
  }
  const at: (HeardWord | null)[] = new Array(n).fill(null);
  for (let i = n, j = m; i > 0 && j > 0; ) {
    const mv = move[i * W + j];
    if (mv === 3) at[--i] = heard[--j];
    else if (mv === 1) i--;
    else j--;
  }

  const out: TimedLine[] = [];
  let first = 0;
  let floor = -Infinity; // nothing may come before the previous line's last word
  lines.forEach((l, li) => {
    const starts = layout[li];
    const mine = at.slice(first, first + starts.length);
    first += starts.length;
    const hits = mine.flatMap((h, k) => (h ? [{ k, t: sungStart(h), end: h.end }] : []));
    // the next sung line (an instrumental "♪" line moves after this one's last word instead)
    const ceiling = lines.slice(li + 1).find((x) => words(x.text).length)?.time ?? Infinity;
    if (!starts.length || hits.length < Math.max(1, Math.ceil(starts.length / 3))) {
      const time = Math.max(l.time, floor);
      out.push({ text: l.text, time });
      floor = Math.max(floor, time);
      return;
    }
    // seconds per letter, for words before the first / after the last heard one
    const a = hits[0];
    const z = hits[hits.length - 1];
    const perChar = z.k > a.k ? Math.min(0.25, Math.max(0.04, (z.t - a.t) / Math.max(1, starts[z.k] - starts[a.k]))) : 0.08;
    const times = starts.map((st, k) => {
      const h = mine[k];
      if (h) return sungStart(h);
      const before = [...hits].reverse().find((x) => x.k < k);
      const after = hits.find((x) => x.k > k);
      if (before && after) return before.t + ((after.t - before.t) * (st - starts[before.k])) / Math.max(1, starts[after.k] - starts[before.k]);
      if (after) return Math.max(floor, after.t - (starts[after.k] - st) * perChar);
      return Math.min(ceiling, before!.t + (st - starts[before!.k]) * perChar);
    });
    // a backing vocal after the last word heard (often shouted, so not heard) answers the sung
    // phrase: it comes once that has ended, not at the singer's pace
    const answer = starts.findIndex((st, k) => k > z.k && inBrackets(l.text, st));
    if (answer >= 0) {
      const shift = Math.max(0, z.end + ANSWER_GAP - times[answer]);
      for (let k = answer; k < times.length; k++) times[k] = Math.min(ceiling, times[k] + shift);
    }
    for (let k = 1; k < times.length; k++) times[k] = Math.max(times[k], times[k - 1]);
    const lastWord = times.length - 1;
    const lastHeard = mine[lastWord];
    const wordsEnd = lastHeard ? Math.max(lastHeard.end, times[lastWord]) : times[lastWord] + 0.3 + 0.06 * (l.text.length - starts[lastWord]);
    out.push({ text: l.text, time: times[0], words: times, wordsEnd });
    floor = Math.max(floor, times[lastWord]);
  });
  return out;
}
