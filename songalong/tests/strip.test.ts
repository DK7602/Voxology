import { describe, expect, it } from "vitest";
import { stripSize } from "../src/song/stripScale";

describe("the chord strip's scale", () => {
  const wide = { lead: false, narrow: false };
  it("gives a song of one chord a bar a compact bar", () => {
    expect(stripSize([4, 4, 4, 4], wide).beat).toBe(36);
  });
  it("widens every beat when chords change every beat, so their names fit", () => {
    const s = stripSize([2, 2, 1, 1, 1, 1, 4, 2, 2, 1, 1, 1, 1, 4], wide);
    expect(s.beat).toBe(64);
    expect(s.beat * 1).toBeGreaterThanOrEqual(s.name);
  });
  it("lets one rare passing chord squeeze rather than widen the whole song", () => {
    const beats = [...Array.from({ length: 40 }, () => 4), 0.5];
    expect(stripSize(beats, wide).beat).toBe(36);
  });
  it("is smaller on a phone, and wider for a lead part's quick notes", () => {
    expect(stripSize([2, 2, 2, 2], { lead: false, narrow: true }).beat).toBe(28);
    expect(stripSize([1, 1, 2, 4], { lead: false, narrow: true }).beat).toBe(44); // two bars stay in view
    expect(stripSize([0.25, 0.25, 0.5, 0.5, 1], { lead: true, narrow: false }).beat).toBe(140);
  });
});
