import { describe, expect, it } from "vitest";
import { buildOf } from "../shared/release";

describe("new version check", () => {
  it("reads which release a page loads", () => {
    const html = '<script type="module" crossorigin src="/assets/index-2K6YP7oC.js"></script><link rel="stylesheet" href="/assets/index-WcUgfDQU.css">';
    expect(buildOf(html)).toBe("/assets/index-2K6YP7oC.js");
    expect(buildOf("<html></html>")).toBeNull();
  });
});
