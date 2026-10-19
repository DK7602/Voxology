import { useEffect, useRef, useState } from "react";
import type { SongDoc } from "../../shared/types";
import { audioUrl } from "../api";
import { Artwork } from "../components/Artwork";
import { IconPause, IconPlay } from "../components/Icons";
import { AudioMedia, YouTubeMedia } from "../player/media";
import type { PlayerEngine } from "../player/engine";
import { usePlayerState, useTick } from "../player/hooks";

function fmt(sec: number): string {
  if (!Number.isFinite(sec) || sec < 0) sec = 0;
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${m}:${s.toString().padStart(2, "0")}`;
}

/** The video (YouTube), the recording (uploaded audio) or the cover (backing track only).
 *  It also hands the right media source to the engine. */
export function MediaPanel({ engine, doc, title }: { engine: PlayerEngine; doc: SongDoc; title: string }) {
  const host = useRef<HTMLDivElement>(null);
  const st = usePlayerState(engine);
  const [duration, setDuration] = useState(engine.timeline.duration);
  const seek = useRef<HTMLInputElement>(null);
  const timeLabel = useRef<HTMLSpanElement>(null);
  const media = doc.media;
  const mediaKey = media.kind === "youtube" ? "yt:" + media.videoId : media.kind === "audio" ? "au:" + media.key : "none";

  useEffect(() => {
    if (media.kind === "youtube" && host.current) {
      host.current.innerHTML = "";
      const m = new YouTubeMedia(host.current, media.videoId);
      engine.setMedia(m);
      m.ready.then(() => setDuration(m.duration() ?? engine.timeline.duration)).catch(() => {});
    } else if (media.kind === "audio") {
      const m = new AudioMedia(audioUrl(media.key));
      m.el.addEventListener("loadedmetadata", () => setDuration(m.duration() ?? engine.timeline.duration));
      engine.setMedia(m);
    } else {
      setDuration(engine.timeline.duration);
    }
    // the engine owns the media from here and destroys it with itself
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [engine, mediaKey]);

  useTick(engine, (t) => {
    if (seek.current && document.activeElement !== seek.current) seek.current.value = String(t);
    if (timeLabel.current) timeLabel.current.textContent = `${fmt(t)} / ${fmt(duration)}`;
  });

  const playing = st.playing || st.countIn !== null;
  return (
    <div className="panel media-panel">
      <div className="media-box">
        {media.kind === "youtube" ? (
          <div ref={host} />
        ) : (
          <>
            <Artwork title={title} rounded={0} />
            <div className="media-overlay">
              <button className="play-btn" onClick={() => engine.toggle()} aria-label={playing ? "Pause" : "Play"}>
                {playing ? <IconPause /> : <IconPlay />}
              </button>
            </div>
            <span className="media-note">{media.kind === "audio" ? "♪ " + media.name : "♪ Backing track"}</span>
            <span className="media-duration">{fmt(duration)}</span>
          </>
        )}
      </div>
      <input
        ref={seek}
        className="media-seek"
        type="range"
        min={Math.min(0, engine.timeline.beatToTime(0))}
        max={duration || 1}
        step={0.1}
        defaultValue={0}
        aria-label="Position"
        onChange={(e) => engine.seekTime(parseFloat(e.target.value))}
      />
      <div className="faint" style={{ display: "flex", justifyContent: "space-between", fontSize: 14 }}>
        <span ref={timeLabel} />
        {st.error && <span className="error-text">{st.error}</span>}
      </div>
    </div>
  );
}
