import { useEffect, useRef, useState } from "react";
import { Link, useNavigate, useParams, useSearchParams } from "react-router-dom";
import { isVideoId, type PrepareEvent } from "../../shared/prep";
import { guessSongName } from "../../shared/songName";
import { api, prepareSong } from "../api";
import { IconCheck } from "../components/Icons";
import { errorText } from "../components/ui";

const STEPS = [
  { key: "get", label: "Getting the song", match: /getting|starting|pc is getting/i },
  { key: "beat", label: "Finding the beat", match: /reading|preparing audio|beat/i },
  { key: "chords", label: "Working out the chords", match: /harmony|chord|naming|chart/i },
  { key: "lyrics", label: "Finding and lining up the lyrics", match: /lyrics|words|timing/i },
  { key: "save", label: "Getting it ready to play", match: /saving|sending|ready/i },
];

function stepIndex(stage: string): number {
  for (let i = STEPS.length - 1; i >= 0; i--) if (STEPS[i].match.test(stage)) return i;
  return 0;
}

type Phase = { kind: "working"; stage: string; fraction: number } | { kind: "waiting_pc"; message: string; online: boolean; stage: string; fraction: number } | { kind: "error"; message: string };

/** Search result clicked: prepares the song (about half a minute), then opens it to play. */
export function PreparePage() {
  const { videoId = "" } = useParams();
  const [params] = useSearchParams();
  const navigate = useNavigate();
  const [phase, setPhase] = useState<Phase>({ kind: "working", stage: "Starting", fraction: 0 });
  const [attempt, setAttempt] = useState(0);
  const started = useRef(Date.now());
  const [elapsed, setElapsed] = useState(0);
  const title = params.get("t") ?? "";
  const channel = params.get("c") ?? "";
  const name = guessSongName({ title, channel });

  useEffect(() => {
    const t = setInterval(() => setElapsed(Math.round((Date.now() - started.current) / 1000)), 1000);
    return () => clearInterval(t);
  }, []);

  useEffect(() => {
    if (!isVideoId(videoId)) {
      setPhase({ kind: "error", message: "That isn't a song we can open." });
      return;
    }
    const ctrl = new AbortController();
    let poll = 0;
    let stopped = false;
    const done = (songId: string) => navigate(`/song/${songId}`, { replace: true });

    const pollStatus = () => {
      poll = window.setTimeout(async () => {
        if (stopped) return;
        try {
          const s = await api.prepStatus(videoId);
          if (s.status === "ready" && s.songId) return done(s.songId);
          if (s.status === "failed") return setPhase({ kind: "error", message: s.error || "This song couldn't be prepared." });
          if (s.status === "waiting_pc" || (s.status === "working" && s.stage.toLowerCase().includes("pc"))) {
            setPhase({
              kind: "waiting_pc",
              online: s.helperOnline,
              stage: s.stage,
              fraction: s.progress,
              message: s.helperOnline ? "Your PC helper is preparing this song." : "Waiting for your PC helper.",
            });
          } else if (s.status === "working") setPhase({ kind: "working", stage: s.stage, fraction: s.progress });
        } catch {
          /* keep trying */
        }
        pollStatus();
      }, 2500);
    };

    const onEvent = (e: PrepareEvent) => {
      if (e.type === "progress") setPhase({ kind: "working", stage: e.stage, fraction: e.fraction });
    };
    prepareSong(
      { videoId, title, channel, duration: Number(params.get("d")) || 0, retry: attempt > 0 },
      onEvent,
      ctrl.signal,
    )
      .then((last) => {
        if (stopped) return;
        if (last?.type === "ready") return done(last.songId);
        if (last?.type === "error") return setPhase({ kind: "error", message: last.message });
        if (last?.type === "waiting_pc") setPhase({ kind: "waiting_pc", message: last.message, online: false, stage: "", fraction: 0 });
        pollStatus(); // waiting for the PC, or the connection dropped
      })
      .catch((e) => {
        if ((e as Error).name !== "AbortError") setPhase({ kind: "error", message: errorText(e) });
      });
    return () => {
      stopped = true;
      ctrl.abort();
      clearTimeout(poll);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [videoId, attempt]);

  const current = phase.kind === "error" ? -1 : stepIndex(phase.stage);
  const fraction = phase.kind === "error" ? 0 : phase.fraction;

  return (
    <div className="panel page-panel prepare">
      <div className="prepare-head">
        <div className="artwork prepare-art">
          <img src={`https://i.ytimg.com/vi/${videoId}/hqdefault.jpg`} alt="" />
        </div>
        <div>
          <span className="chip">Preparing</span>
          <h1>{name.title || "Your song"}</h1>
          {name.artist && <div className="muted">{name.artist}</div>}
        </div>
      </div>

      {phase.kind !== "error" && (
        <>
          <div className="prepare-bar" role="progressbar" aria-valuenow={Math.round(fraction * 100)} aria-valuemin={0} aria-valuemax={100}>
            <div style={{ width: `${Math.max(3, Math.round(fraction * 100))}%` }} />
          </div>
          <ol className="prepare-steps">
            {STEPS.map((s, i) => (
              <li key={s.key} className={i < current ? "done" : i === current ? "now" : ""}>
                <span className="step-mark">{i < current ? <IconCheck size={16} /> : i + 1}</span>
                {s.label}
              </li>
            ))}
          </ol>
          <p className="muted">
            {phase.kind === "waiting_pc" ? phase.message : "Usually 20–45 seconds the first time. After that this song opens instantly, for everyone."}{" "}
            <span className="faint">({elapsed}s)</span>
          </p>
          {phase.kind === "waiting_pc" && !phase.online && (
            <div className="notice">
              YouTube didn't let the cloud fetch this song, so your own PC will prepare it.{" "}
              <Link to="/settings#helper">Set up or start your PC helper</Link>. You can leave this page; the song will be in
              search results marked <b>Ready</b> when it's done.
            </div>
          )}
        </>
      )}

      {phase.kind === "error" && (
        <>
          <div className="notice error">{phase.message}</div>
          <div style={{ display: "flex", gap: 10, marginTop: 14, flexWrap: "wrap" }}>
            <button className="btn btn-primary" onClick={() => setAttempt((a) => a + 1)}>Try again</button>
            <button className="btn" onClick={() => navigate(-1)}>Back to results</button>
          </div>
        </>
      )}
    </div>
  );
}
