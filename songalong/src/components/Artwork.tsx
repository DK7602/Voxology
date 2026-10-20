// Song artwork: the YouTube thumbnail when there is one, otherwise a generated cover in the
// theme's colors (gold / green, or red / white / blue) with an instrument motif (deterministic
// per title).
import type { CSSProperties } from "react";
import { IconGuitar, IconMandolin, IconNote, IconPiano } from "./Icons";

function hash(s: string): number {
  let h = 2166136261;
  for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619);
  return h >>> 0;
}

export function Artwork({
  title,
  youtubeId,
  className,
  rounded = 14,
  label,
}: {
  title: string;
  youtubeId?: string;
  className?: string;
  rounded?: number;
  /** words to show on a generated cover instead of the instrument motif (a playlist's name) */
  label?: string;
}) {
  if (youtubeId) {
    return (
      <div className={"artwork " + (className ?? "")} style={{ borderRadius: rounded }}>
        <img src={`https://i.ytimg.com/vi/${youtubeId}/hqdefault.jpg`} alt="" loading="lazy" />
      </div>
    );
  }
  const h = hash(title || "song");
  const hueShift = (h % 40) - 20;
  const angle = h % 360;
  const Icon = [IconGuitar, IconNote, IconPiano, IconMandolin][h % 4];
  return (
    <div
      className={"artwork generated " + (className ?? "")}
      // each song its own angle and shade; the colors come from the theme (app.css)
      style={{ borderRadius: rounded, "--art-angle": `${angle}deg`, "--art-hue": hueShift } as CSSProperties}
      aria-hidden="true"
    >
      {label ? (
        // a long name may break after a slash ("Fun/" "Learning"), not mid-word
        <span className="art-label">{label.replace(/\//g, "/\u200b")}</span>
      ) : (
        <>
          <Icon size={44} />
          <span className="art-initial">{(title || "♪").trim().charAt(0).toUpperCase()}</span>
        </>
      )}
    </div>
  );
}

export function Logo({ compact }: { compact?: boolean }) {
  return (
    <span className="logo" aria-label="SongAlong">
      <img className="logo-emblem" src="/brand/emblem.webp" alt="" width={compact ? 28 : 58} height={compact ? 28 : 58} />
      {!compact && <img className="logo-text" src="/brand/wordmark.webp" alt="" width={174} height={48} />}
    </span>
  );
}
