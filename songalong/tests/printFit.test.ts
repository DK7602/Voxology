import { describe, expect, it } from "vitest";
import { chooseFit, MIN_SCALE_ONE_PAGE, pagesFor, SCALES } from "../src/song/printFit";

describe("fitting the printed words and chords to the page", () => {
  it("counts the pages blocks fill, moving a block that doesn't fit to the next column whole", () => {
    expect(pagesFor([], 1, 100, 1000)).toBe(1);
    // 900 px under a 100 px header: three 300s fit, a fourth starts page 2
    expect(pagesFor([300, 300, 300], 1, 100, 1000)).toBe(1);
    expect(pagesFor([300, 300, 300, 10], 1, 100, 1000)).toBe(2);
    // two columns: both of page 1's start under the header
    expect(pagesFor([500, 500, 500], 2, 100, 1000)).toBe(2);
    expect(pagesFor([400, 400, 400, 400], 2, 100, 1000)).toBe(1);
    // a block that won't fit where the column has room left goes whole to the next column
    expect(pagesFor([600, 400], 1, 100, 1000)).toBe(2);
    // a block taller than a column runs on into the next
    expect(pagesFor([2500], 1, 0, 1000)).toBe(3);
  });

  it("picks the fewest pages, then the largest text that keeps to them", () => {
    // fits on one page at every size: full size
    expect(chooseFit(() => 1)).toEqual({ scale: 1, pages: 1 });
    // two pages at full size, one page from 88% down: one page at 88%
    expect(chooseFit((s) => (s <= 0.88 ? 1 : 2))).toEqual({ scale: 0.88, pages: 1 });
    // one page only below the smallest size worth it: two pages, at full size
    expect(chooseFit((s) => (s < MIN_SCALE_ONE_PAGE ? 1 : 2))).toEqual({ scale: 1, pages: 2 });
    // three pages at full size, two from 76%: two pages at 76%
    expect(chooseFit((s) => (s <= 0.76 ? 2 : 3))).toEqual({ scale: 0.76, pages: 2 });
    // long songs shrink no further than the smallest size
    expect(chooseFit(() => 4)).toEqual({ scale: 1, pages: 4 });
    expect(chooseFit((s) => (s === SCALES.at(-1) ? 3 : 4))).toEqual({ scale: SCALES.at(-1), pages: 3 });
  });
});
