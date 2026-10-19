import { useEffect, useState } from "react";
import { api, type HelperKey } from "../api";
import { downloadBlob, errorText, useToast } from "../components/ui";

const ONLINE_MS = 60_000;

/** Settings → Your PC helper: connect a computer that prepares songs the cloud can't fetch. */
export function HelperSettings() {
  const toast = useToast();
  const [keys, setKeys] = useState<HelperKey[] | null>(null);
  const [fresh, setFresh] = useState<string | null>(null);
  const site = location.origin;

  const load = () => api.helpers().then((r) => setKeys(r.helpers)).catch((e) => toast(errorText(e), true));
  useEffect(() => {
    void load();
    const t = setInterval(load, 15000);
    return () => clearInterval(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  const connect = async () => {
    try {
      const r = await api.createHelper(navigator.platform.toLowerCase().includes("win") ? "Windows PC" : "My computer");
      setFresh(r.token);
      void load();
    } catch (e) {
      toast(errorText(e), true);
    }
  };

  const windowsLauncher = (token: string) =>
    [
      "@echo off",
      "title SongAlong PC helper",
      'cd /d "%~dp0"',
      "where node >nul 2>nul || (echo Node.js is needed first: https://nodejs.org  ^(the LTS download^) & pause & exit /b 1)",
      `powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing '${site}/helper/songalong-helper.mjs' -OutFile 'songalong-helper.mjs'"`,
      `node songalong-helper.mjs --site ${site} --token ${token}`,
      "pause",
      "",
    ].join("\r\n");

  return (
    <section className="settings-section" id="helper">
      <h2>Your PC helper</h2>
      <p className="muted" style={{ marginTop: 0 }}>
        YouTube sometimes refuses Cloudflare's servers. When that happens, a small helper on your own computer prepares the song
        instead, the same way Sample Snagger fetches audio. It reuses Sample Snagger's yt-dlp and ffmpeg if they're installed.
        Songs it prepares work everywhere afterwards, including your phone.
      </p>
      {keys?.length ? (
        <div style={{ display: "grid", gap: 6, marginBottom: 12 }}>
          {keys.map((k) => {
            const online = k.lastSeenAt !== null && Date.now() - k.lastSeenAt < ONLINE_MS;
            return (
              <div key={k.id} className="setting-row" style={{ padding: "6px 0" }}>
                <div className="label">
                  <b>
                    <span className={"dot-status" + (online ? " on" : "")} aria-hidden="true" /> {k.name}
                  </b>
                  <span>{online ? "Connected now" : k.lastSeenAt ? `Last seen ${new Date(k.lastSeenAt).toLocaleString()}` : "Not connected yet"}</span>
                </div>
                <button
                  className="btn btn-sm btn-danger"
                  onClick={async () => {
                    if (!confirm(`Disconnect "${k.name}"? It will stop preparing songs.`)) return;
                    await api.deleteHelper(k.id).catch((e) => toast(errorText(e), true));
                    void load();
                  }}
                >
                  Disconnect
                </button>
              </div>
            );
          })}
        </div>
      ) : null}
      {!fresh && (
        <button className="btn btn-primary" onClick={connect}>
          Connect a computer
        </button>
      )}
      {fresh && (
        <div className="notice" style={{ display: "grid", gap: 10 }}>
          <b>Set up your computer (one time)</b>
          <ol style={{ margin: 0, paddingLeft: 20, display: "grid", gap: 6 }}>
            <li>
              Install <a href="https://nodejs.org" target="_blank" rel="noreferrer">Node.js</a> (the LTS download) if you don't have it.
            </li>
            <li>
              <button className="btn btn-sm" onClick={() => downloadBlob(windowsLauncher(fresh), "SongAlong-Helper.cmd", "text/plain")}>
                Download the Windows helper
              </button>{" "}
              and double-click it. Keep the window open while you use SongAlong (minimising it is fine).
            </li>
            <li>
              Mac / Linux: run this in Terminal{" "}
              <button
                className="btn btn-sm"
                onClick={() =>
                  navigator.clipboard
                    .writeText(`curl -O ${site}/helper/songalong-helper.mjs && node songalong-helper.mjs --site ${site} --token ${fresh}`)
                    .then(() => toast("Command copied"))
                    .catch(() => toast("Couldn't copy: select it below instead", true))
                }
              >
                Copy command
              </button>
              <pre className="helper-cmd">{`curl -O ${site}/helper/songalong-helper.mjs && node songalong-helper.mjs --site ${site} --token ${fresh}`}</pre>
            </li>
          </ol>
          <span className="faint" style={{ fontSize: 14 }}>
            The download contains a private key for your account. Don't share it; disconnect it here if you do.
          </span>
          <button className="btn btn-sm" style={{ justifySelf: "start" }} onClick={() => setFresh(null)}>
            Done
          </button>
        </div>
      )}
    </section>
  );
}
