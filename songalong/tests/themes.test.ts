import { describe, expect, it } from "vitest";
import { validateUserSettings } from "../shared/validate";

describe("colour schemes", () => {
  it("saves light / dark and the colours separately", () => {
    for (const theme of ["light", "dark", "system"]) expect(validateUserSettings({ theme }).theme).toBe(theme);
    for (const palette of ["gold", "usyay"]) expect(validateUserSettings({ palette }).palette).toBe(palette);
    expect(validateUserSettings({ theme: "dark", palette: "usyay" })).toEqual({ theme: "dark", palette: "usyay" });
    expect(() => validateUserSettings({ theme: "neon" })).toThrow();
    expect(() => validateUserSettings({ palette: "neon" })).toThrow();
  });

  it("reads the older USyay theme as USyay in light", () => {
    expect(validateUserSettings({ theme: "usyay" })).toEqual({ theme: "light", palette: "usyay" });
    expect(validateUserSettings({ theme: "usyay", palette: "gold" })).toEqual({ theme: "light", palette: "gold" });
  });
});
