import { describe, expect, it } from "vitest";
import { drumOnsets, RATE } from "../cajon/drums";
import {
  cajonPart,
  cajonStrokes,
  defaultLevel,
  drumHits,
  eighthNotes,
  hands,
  noDrums,
  onTheBeats,
  validDrums,
  type Stroke,
} from "../shared/music/cajon";

/** 16ths of a rock beat: kick on 1 and the "and" of 3, snare on 2 and 4, 8th-note hi-hat. */
const ROCK = "5040604050506040";

/** A drum part for `bars` bars of the same 16-slot pattern. */
const drumsOf = (pattern: string, bars: number) => ({ v: 1 as const, hits: pattern.repeat(bars) });
const song = (hits?: string, bars = 16, bpm = 120) => ({ beatsPerBar: 4, bpm, length: bars * 4, drums: hits === undefined ? undefined : { v: 1 as const, hits } });

describe("drums on the beat grid", () => {
  it("puts each hit on the nearest 16th note, if close enough", () => {
    const beats = [0, 0.5, 1, 1.5, 2];
    // kick on beat 0, snare 20 ms after beat 1, hi-hat on the "and" of beat 0, a stray hit far off the grid
    const hits = drumHits({ kick: [0.0], snare: [0.52], hat: [0.25, 1.0625] }, beats);
    expect(hits).toBe("1040" + "2");
  });

  it("reads strokes from the hits: snare wins, then kick, then hi-hat", () => {
    const part = cajonPart(song(drumsOf(ROCK, 16).hits), "groove");
    expect(part.source).toBe("drums");
    expect(part.grooves.map((g) => g.bar.join(""))).toEqual(["B.t.S.t.B.B.S.t."]);
    expect(part.barGroove).toHaveLength(16);
    expect(part.busy).toBe(false);
  });

  it("Easy keeps Bass and Slap on the beats", () => {
    const bar = [..."B.t.S.t.B.B.S.t."] as Stroke[];
    expect(onTheBeats(bar).join("")).toBe("B...S...B...S...");
    expect(eighthNotes([..."BtBt" + "S..." + "B..." + "S..."] as Stroke[]).join("")).toBe("B.B.S...B...S...");
  });

  it("Easy follows the drums when they sit half a beat off the grid", () => {
    expect(onTheBeats([..."..B...S...B...S."] as Stroke[]).join("")).toBe("..B...S...B...S.");
  });

  it("gives each stroke a hand: 8th notes alternate, Easy alternates by beat", () => {
    expect(hands([..."B.t.S.t.B.B.S.t."] as Stroke[], "groove").join(",")).toBe("R,,L,,R,,L,,R,,L,,R,,L,");
    expect(hands([..."B...S...B...S..."] as Stroke[], "easy").filter(Boolean)).toEqual(["R", "L", "R", "L"]);
  });

  it("names grooves by letter and keeps a letter when the groove comes back; no drums is a rest", () => {
    const verse = ROCK.repeat(4);
    const chorus = "5050605050506050".repeat(4);
    const quiet = "0000000000000000".repeat(4);
    const part = cajonPart(song(verse + chorus + quiet + verse, 16), "groove");
    expect(part.grooves.map((g) => g.name)).toEqual(["A", "B", "Rest"]);
    expect(part.barGroove).toEqual([0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0]);
    expect(part.grooves[2].rest).toBe(true);
    expect(part.grooves[2].bar.every((x) => x === ".")).toBe(true);
  });

  it("a groove from most bars of the phrase: a fill in one bar doesn't change it", () => {
    const fill = "2222222222222222";
    const part = cajonPart(song(ROCK + ROCK + ROCK + fill, 4), "groove");
    expect(part.grooves[0].bar.join("")).toBe("B.t.S.t.B.B.S.t.");
  });

  it("opens busy songs on Easy, steady ones on Groove", () => {
    expect(defaultLevel(song(ROCK.repeat(16)))).toBe("groove");
    // Bass or Slap on every 8th note
    expect(defaultLevel(song("1020102010201020".repeat(16)))).toBe("easy");
    // four quite different grooves, none played most of the time
    const grooves = ["1000200010002000", "2000100020001000", "1010101010101010", "2020202020202020"];
    expect(defaultLevel(song(grooves.map((g) => g.repeat(4)).join(""), 16))).toBe("easy");
  });

  it("phrases that differ by a stroke or two play one groove (the most common)", () => {
    const main = ROCK.repeat(4);
    const missedSnare = "5040604050506040".repeat(3) + "5040404050506040"; // one bar lost a snare: still the same groove
    const extraKick = "5040604050506050".repeat(4); // a kick on the last "and"
    const part = cajonPart(song(main + main + missedSnare + extraKick + main, 20), "groove");
    expect(part.grooves).toHaveLength(1);
    expect(part.grooves[0].bar.join("")).toBe("B.t.S.t.B.B.S.t.");
  });

  it("an acoustic song's strums heard as drums (kick and snare at once) get the suggested groove", () => {
    const strums = "3030303030303030".repeat(16);
    expect(noDrums({ v: 1, hits: strums }, 64)).toBe(true);
    expect(cajonPart(song(strums), "groove").source).toBe("suggested");
    // a real beat with the odd kick and crash together is still drums
    expect(noDrums({ v: 1, hits: ("3040604050506040" + ROCK.repeat(3)).repeat(4) }, 64)).toBe(false);
  });

  it("songs without drums get a suggested groove for their tempo", () => {
    expect(noDrums(undefined, 64)).toBe(true);
    expect(noDrums({ v: 1, hits: "4040404040404040".repeat(16) }, 64)).toBe(true); // hi-hat only
    expect(noDrums(drumsOf(ROCK, 16), 64)).toBe(false);
    const ballad = cajonPart(song(undefined, 16, 70), "groove");
    expect(ballad.source).toBe("suggested");
    expect(ballad.grooves[0].bar.join("")).toBe("B...S.t.B.t.S...");
    expect(cajonPart(song(undefined, 16, 120), "easy").grooves[0].bar.join("")).toBe("B...S...B...S...");
    expect(cajonPart({ beatsPerBar: 3, bpm: 90, length: 24 }, "groove").grooves[0].bar.join("")).toBe("B.t.S.t.S.t.");
  });

  it("lists every stroke with its beat and hand", () => {
    const strokes = cajonStrokes(cajonPart(song(ROCK.repeat(2), 2), "easy"));
    expect(strokes.slice(0, 5)).toEqual([
      { beat: 0, stroke: "B", hand: "R" },
      { beat: 1, stroke: "S", hand: "L" },
      { beat: 2, stroke: "B", hand: "R" },
      { beat: 3, stroke: "S", hand: "L" },
      { beat: 4, stroke: "B", hand: "R" },
    ]);
  });

  it("checks drum parts from songs", () => {
    expect(validDrums({ v: 1, hits: "0123" })).toEqual({ v: 1, hits: "0123" });
    expect(validDrums({ v: 1, hits: "9" })).toBeUndefined();
    expect(validDrums({ v: 2, hits: "1" })).toBeUndefined();
    expect(validDrums("x")).toBeUndefined();
  });
});

/** Ten seconds of made-up drums at 120 bpm: kick, snare and hi-hat at known times. */
function syntheticDrums() {
  const x = new Float32Array(RATE * 10);
  let seed = 1;
  const noise = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 31 - 1);
  const add = (t: number, f: (i: number) => number, len: number) => {
    const start = Math.round(t * RATE);
    for (let i = 0; i < len * RATE && start + i < x.length; i++) x[start + i] += f(i);
  };
  const kick = (t: number) =>
    add(t, (i) => {
      const s = i / RATE;
      return 0.9 * Math.sin(2 * Math.PI * (50 * s + (70 * 0.03) * (1 - Math.exp(-s / 0.03)))) * Math.exp(-s / 0.12);
    }, 0.4);
  /** RBJ band-pass filter: the snare's wires sit in the middle, the hi-hat's hiss up high */
  const bandPass = (hz: number, q: number) => {
    const w = (2 * Math.PI * hz) / RATE;
    const alpha = Math.sin(w) / (2 * q);
    const [b0, b2, a0, a1, a2] = [alpha, -alpha, 1 + alpha, -2 * Math.cos(w), 1 - alpha];
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    return (v: number) => {
      const y = (b0 * v + b2 * x2 - a1 * y1 - a2 * y2) / a0;
      [x2, x1, y2, y1] = [x1, v, y1, y];
      return y;
    };
  };
  const wires = bandPass(2500, 1.2);
  const hiss = bandPass(9000, 2);
  const snare = (t: number) =>
    add(t, (i) => {
      const s = i / RATE;
      const body = 0.35 * Math.sin(2 * Math.PI * 190 * s) * (1 - Math.exp(-s / 0.002)) * Math.exp(-s / 0.05);
      return body + 1.4 * wires(noise()) * Math.exp(-s / 0.07);
    }, 0.3);
  const hat = (t: number) => add(t, (i) => 1.2 * hiss(noise()) * Math.exp(-(i / RATE) / 0.03), 0.1);
  const truth = { kick: [] as number[], snare: [] as number[], hat: [] as number[] };
  for (let beat = 0; beat < 18; beat++) {
    const t = 0.5 + beat * 0.5;
    if (beat % 2 === 0) (kick(t), truth.kick.push(t));
    else (snare(t), truth.snare.push(t));
    hat(t + 0.25);
    truth.hat.push(t + 0.25);
  }
  return { x, truth };
}

describe("finding drum hits", () => {
  it("finds the kick, snare and hi-hat of a drum track", () => {
    const { x, truth } = syntheticDrums();
    const found = drumOnsets(x);
    for (const drum of ["kick", "snare", "hat"] as const) {
      const hit = truth[drum].filter((t) => found[drum].some((f) => Math.abs(f - t) <= 0.05)).length;
      const extra = found[drum].filter((f) => !truth[drum].some((t) => Math.abs(f - t) <= 0.05)).length;
      expect(hit / truth[drum].length, `${drum} found`).toBeGreaterThanOrEqual(0.9);
      expect(extra, `${drum} extras`).toBeLessThanOrEqual(1);
    }
  });
});

describe("YouTube's refusals", () => {
  it("a 403 while downloading is YouTube refusing this computer (tried again later)", async () => {
    const { classifyYtDlpError } = await import("../prep/core");
    expect(classifyYtDlpError("ERROR: unable to download video data: HTTP Error 403: Forbidden").code).toBe("blocked");
    expect(classifyYtDlpError("ERROR: [youtube] x: Video unavailable").code).toBe("unavailable");
  });
});
