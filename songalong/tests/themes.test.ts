import { describe, expect, it } from "vitest";
import { validateUserSettings } from "../shared/validate";

describe("colour schemes", () => {
  it("saves each scheme, USyay included, and refuses others", () => {
    for (const theme of ["light", "dark", "usyay", "system"]) expect(validateUserSettings({ theme }).theme).toBe(theme);
    expect(() => validateUserSettings({ theme: "neon" })).toThrow();
  });
});
