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

  const connected = keys?.some((k) => k.lastSeenAt !== null) ?? false;
  const started = keys?.some((k) => k.lastSeenAt === null) ?? false;

  const connect = async () => {
    // a second key is rarely what's wanted: the same downloaded file starts the helper every time
    if (connected && !confirm("A computer is already connected. To start it again, double-click SongAlong-Helper.cmd on that computer: it doesn't need a new one.\n\nConnect a different computer?")) return;
    if (!connected && started && !confirm("You've already started: double-click the SongAlong-Helper.cmd you downloaded.\n\nMake a new one instead? The earlier download will stop working.")) return;
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
                  <span>
                    {online
                      ? "Connected now"
                      : k.lastSeenAt
                        ? `Not running (last seen ${new Date(k.lastSeenAt).toLocaleString()}). To start it, double-click SongAlong-Helper.cmd on that computer.`
                        : connected
                          ? "Never connected. If you don't need it, disconnect it."
                          : "Not connected yet: double-click the SongAlong-Helper.cmd you downloaded."}
                  </span>
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
        <button className={"btn" + (connected ? "" : " btn-primary")} onClick={connect}>
          {connected ? "Connect another computer" : "Connect a computer"}
        </button>
      )}
      {fresh && (
        <div className="notice" style={{ display: "grid", gap: 10 }}>
          <b>Set up your computer (one time)</b>
          <span className="faint" style={{ fontSize: 15 }}>Do this on the computer that will prepare songs, not on your phone.</span>
          <ol style={{ margin: 0, paddingLeft: 20, display: "grid", gap: 8 }}>
            <li>
              <button className="btn btn-sm btn-primary" onClick={() => downloadBlob(windowsLauncher(fresh), "SongAlong-Helper.cmd", "text/plain")}>
                Download the helper
              </button>{" "}
              (Windows), then open your Downloads folder and double-click <b>SongAlong-Helper.cmd</b>. If Windows says “Windows protected your PC”,
              choose <b>More info → Run anyway</b>.
            </li>
            <li>
              The first time, it gets its tools (a minute or so). It's working when its window says <b>Connected</b> and a green dot shows above. Leave
              the window open; minimising it is fine.
            </li>
            <li>
              Only if the window says <b>Node.js is needed first</b>: install <a href="https://nodejs.org" target="_blank" rel="noreferrer">Node.js</a>{" "}
              (the LTS download), then double-click the helper again.
            </li>
          </ol>
          <details>
            <summary style={{ cursor: "pointer" }}>Mac or Linux?</summary>
            <p style={{ margin: "8px 0" }}>
              With <a href="https://nodejs.org" target="_blank" rel="noreferrer">Node.js</a> installed, run this in Terminal:{" "}
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
            </p>
            <pre className="helper-cmd">{`curl -O ${site}/helper/songalong-helper.mjs && node songalong-helper.mjs --site ${site} --token ${fresh}`}</pre>
          </details>
          <span className="faint" style={{ fontSize: 14 }}>
            After a restart, double-click the same file again: you don't need a new one. It contains a private key for your account, so don't share
            it; disconnect it here if you do.
          </span>
          <button className="btn btn-sm" style={{ justifySelf: "start" }} onClick={() => setFresh(null)}>
            Done
          </button>
        </div>
      )}
    </section>
  );
}
