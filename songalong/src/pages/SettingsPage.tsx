import { useState, type FormEvent } from "react";
import { useNavigate } from "react-router-dom";
import { PASSWORD_MIN } from "../../shared/api";
import type { Instrument, UserSettings } from "../../shared/types";
import { api } from "../api";
import { useAuth, useUser } from "../auth";
import { Toggle, errorText, useToast } from "../components/ui";
import { HelperSettings } from "./HelperSettings";

export function SettingsPage() {
  const user = useUser();
  const { updateSettings, setUser, logout } = useAuth();
  const toast = useToast();
  const navigate = useNavigate();
  const s = user.settings;
  const [name, setName] = useState(user.name);
  const [pw, setPw] = useState({ current: "", next: "", again: "" });
  const [delPw, setDelPw] = useState("");

  const set = (patch: UserSettings) => updateSettings(patch).catch((e) => toast(errorText(e), true));

  const saveName = async (e: FormEvent) => {
    e.preventDefault();
    try {
      const r = await api.updateMe({ name: name.trim() });
      setUser(r.user);
      toast("Name saved");
    } catch (err) {
      toast(errorText(err), true);
    }
  };
  const changePassword = async (e: FormEvent) => {
    e.preventDefault();
    if (pw.next !== pw.again) return toast("The new passwords don't match.", true);
    if (pw.next.length < PASSWORD_MIN) return toast(`Use at least ${PASSWORD_MIN} characters.`, true);
    try {
      await api.changePassword(pw.current, pw.next);
      setPw({ current: "", next: "", again: "" });
      if (!user.hasPassword) setUser({ ...user, hasPassword: true });
      toast(user.hasPassword ? "Password changed. Other devices were signed out." : "Password set. You can now also log in with your email.");
    } catch (err) {
      toast(errorText(err), true);
    }
  };
  const deleteAccount = async (e: FormEvent) => {
    e.preventDefault();
    if (!confirm("Delete your account, songs, playlists and uploads for good?")) return;
    try {
      await api.deleteAccount(delPw, delPw);
      await logout().catch(() => {});
      navigate("/login");
    } catch (err) {
      toast(errorText(err), true);
    }
  };

  return (
    <div className="panel page-panel" style={{ maxWidth: 860, margin: "0 auto" }}>
      <div className="page-head">
        <h1>Settings</h1>
      </div>

      <section className="settings-section">
        <h2>Look</h2>
        <div className="setting-row">
          <div className="label">
            <b>Theme</b>
            <span>Gold & white glass, gold & black glass, or follow your device</span>
          </div>
          <div className="seg" role="group" aria-label="Theme">
            {(["light", "dark", "system"] as const).map((t) => (
              <button key={t} aria-pressed={(s.theme ?? "system") === t} onClick={() => set({ theme: t })}>
                {t === "system" ? "Device" : t[0].toUpperCase() + t.slice(1)}
              </button>
            ))}
          </div>
        </div>
      </section>

      <section className="settings-section">
        <h2>Playing</h2>
        <div className="setting-row">
          <div className="label">
            <b>Default instrument</b>
            <span>Used when you open a song for the first time</span>
          </div>
          <div className="seg" role="group" aria-label="Instrument">
            {(["guitar", "ukulele", "piano", "mandolin"] as Instrument[]).map((i) => (
              <button key={i} aria-pressed={(s.instrument ?? "guitar") === i} onClick={() => set({ instrument: i })}>
                {i[0].toUpperCase() + i.slice(1)}
              </button>
            ))}
          </div>
        </div>
        <div className="setting-row">
          <div className="label">
            <b>Left-handed diagrams</b>
            <span>Mirror chord boxes (low string on the right)</span>
          </div>
          <Toggle checked={!!s.leftHanded} onChange={(v) => set({ leftHanded: v })} label="Left-handed" />
        </div>
        <div className="setting-row">
          <div className="label">
            <b>With a capo, show the shapes you play</b>
            <span>Off: show the chords as they sound</span>
          </div>
          <Toggle checked={s.capoShapes ?? true} onChange={(v) => set({ capoShapes: v })} label="Capo shapes" />
        </div>
        <div className="setting-row">
          <div className="label">
            <b>Count-in</b>
            <span>Clicks before the music starts</span>
          </div>
          <div className="seg" role="group" aria-label="Count-in">
            {[0, 1, 2].map((n) => (
              <button key={n} aria-pressed={(s.countIn ?? 1) === n} onClick={() => set({ countIn: n })}>
                {n === 0 ? "Off" : `${n} bar${n > 1 ? "s" : ""}`}
              </button>
            ))}
          </div>
        </div>
      </section>

      <HelperSettings />

      <section className="settings-section">
        <h2>Account</h2>
        <form onSubmit={saveName} style={{ display: "flex", gap: 10, alignItems: "end", flexWrap: "wrap" }}>
          <label className="field" style={{ flex: 1, minWidth: 220 }}>
            <span>Name</span>
            <input className="input" value={name} onChange={(e) => setName(e.target.value)} maxLength={80} />
          </label>
          <button className="btn">Save name</button>
        </form>
        <p className="muted">Signed in as {user.email}</p>
        {!user.hasPassword && (
          <p className="muted" style={{ marginTop: 12 }}>
            You log in with Apple, Google or Facebook. Set a password to also log in with your email.
          </p>
        )}
        <form onSubmit={changePassword} className="form-grid" style={{ marginTop: 12 }}>
          {user.hasPassword && (
            <label className="field">
              <span>Current password</span>
              <input className="input" type="password" autoComplete="current-password" required value={pw.current} onChange={(e) => setPw({ ...pw, current: e.target.value })} />
            </label>
          )}
          <label className="field">
            <span>New password</span>
            <input className="input" type="password" autoComplete="new-password" required minLength={PASSWORD_MIN} value={pw.next} onChange={(e) => setPw({ ...pw, next: e.target.value })} />
          </label>
          <label className="field">
            <span>New password again</span>
            <input className="input" type="password" autoComplete="new-password" required value={pw.again} onChange={(e) => setPw({ ...pw, again: e.target.value })} />
          </label>
          <div style={{ display: "flex", alignItems: "end" }}>
            <button className="btn btn-primary">{user.hasPassword ? "Change password" : "Set password"}</button>
          </div>
        </form>
        <div style={{ marginTop: 16 }}>
          <button className="btn" onClick={() => void logout().then(() => navigate("/login"))}>Sign out</button>
        </div>
      </section>

      <section className="settings-section">
        <h2>Delete account</h2>
        <form onSubmit={deleteAccount} style={{ display: "flex", gap: 10, alignItems: "end", flexWrap: "wrap" }}>
          <label className="field" style={{ flex: 1, minWidth: 220 }}>
            <span>{user.hasPassword ? "Your password, to confirm" : "Type DELETE to confirm"}</span>
            <input
              className="input"
              type={user.hasPassword ? "password" : "text"}
              autoComplete={user.hasPassword ? "current-password" : "off"}
              required
              value={delPw}
              onChange={(e) => setDelPw(e.target.value)}
            />
          </label>
          <button className="btn btn-danger">Delete my account</button>
        </form>
      </section>
    </div>
  );
}
