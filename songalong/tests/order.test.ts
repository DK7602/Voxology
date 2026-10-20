import { describe, expect, it } from "vitest";
import { inOrder, mergeOrder } from "../shared/order";

describe("custom orders", () => {
  it("reorders a filtered view in the places its songs held", () => {
    expect(mergeOrder(["a", "b", "c", "d", "e"], ["d", "b"])).toEqual(["a", "d", "c", "b", "e"]);
    expect(mergeOrder(["a", "b", "c"], ["c", "a", "b"])).toEqual(["c", "a", "b"]);
    // unknown and repeated ids are ignored
    expect(mergeOrder(["a", "b", "c"], ["x", "c", "c", "a"])).toEqual(["c", "b", "a"]);
  });

  it("puts songs not yet in the saved order first", () => {
    const list = ["n", "a", "b"].map((id) => ({ id }));
    expect(inOrder(list, ["b", "a"]).map((s) => s.id)).toEqual(["n", "b", "a"]);
    expect(inOrder(list, []).map((s) => s.id)).toEqual(["n", "a", "b"]);
  });
});
