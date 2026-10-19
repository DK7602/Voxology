// Line icons (24 x 24, currentColor).
import type { SVGProps } from "react";

type P = SVGProps<SVGSVGElement> & { size?: number };

function Svg({ size = 24, children, ...rest }: P) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={1.8}
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
      {...rest}
    >
      {children}
    </svg>
  );
}

export const IconSearch = (p: P) => (
  <Svg {...p}>
    <circle cx="11" cy="11" r="7" />
    <path d="m20 20-4-4" />
  </Svg>
);
export const IconHome = (p: P) => (
  <Svg {...p}>
    <path d="M3 11.5 12 4l9 7.5" />
    <path d="M5.5 10v10h4.5v-6h4v6h4.5V10" />
  </Svg>
);
export const IconCompass = (p: P) => (
  <Svg {...p}>
    <circle cx="12" cy="12" r="9" />
    <path d="m15.5 8.5-2 5-5 2 2-5z" />
  </Svg>
);
export const IconLibrary = (p: P) => (
  <Svg {...p}>
    <path d="M5 4h11a2 2 0 0 1 2 2v14H7a2 2 0 0 1-2-2z" fill="currentColor" fillOpacity={0.9} />
    <path d="M18 20H7a2 2 0 0 1 0-4h11" />
  </Svg>
);
export const IconPlaylist = (p: P) => (
  <Svg {...p}>
    <path d="M9 18V6l10-2v12" />
    <circle cx="6.5" cy="18" r="2.5" fill="currentColor" />
    <circle cx="16.5" cy="16" r="2.5" fill="currentColor" />
  </Svg>
);
export const IconWrench = (p: P) => (
  <Svg {...p}>
    <path d="M14.5 6.5a4 4 0 0 0 5 5L12 19a2.1 2.1 0 0 1-3-3l7.5-7.5a4 4 0 0 0-5-5l2.5 2.5-1 2-2 1-2.5-2.5" />
  </Svg>
);
export const IconChevronDown = (p: P) => (
  <Svg {...p}>
    <path d="m6 9 6 6 6-6" />
  </Svg>
);
export const IconChevronLeft = (p: P) => (
  <Svg {...p}>
    <path d="m15 6-6 6 6 6" />
  </Svg>
);
export const IconChevronRight = (p: P) => (
  <Svg {...p}>
    <path d="m9 6 6 6-6 6" />
  </Svg>
);
export const IconArrowRight = (p: P) => (
  <Svg {...p}>
    <path d="M5 12h14M13 6l6 6-6 6" />
  </Svg>
);
export const IconGear = (p: P) => (
  <Svg {...p}>
    <circle cx="12" cy="12" r="3" />
    <path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z" />
  </Svg>
);
export const IconHeart = ({ filled, ...p }: P & { filled?: boolean }) => (
  <Svg {...p}>
    <path
      d="M12 20s-7-4.4-9.2-8.6C1.3 8.4 3.1 5 6.4 5c2 0 3.3 1.1 4.1 2.4h3c.8-1.3 2.1-2.4 4.1-2.4 3.3 0 5.1 3.4 3.6 6.4C19 15.6 12 20 12 20z"
      fill={filled ? "currentColor" : "none"}
    />
  </Svg>
);
export const IconShare = (p: P) => (
  <Svg {...p}>
    <circle cx="18" cy="5" r="2.5" />
    <circle cx="6" cy="12" r="2.5" />
    <circle cx="18" cy="19" r="2.5" />
    <path d="m8.2 10.8 7.6-4.4M8.2 13.2l7.6 4.4" />
  </Svg>
);
export const IconMore = (p: P) => (
  <Svg {...p}>
    <circle cx="12" cy="5" r="1.4" fill="currentColor" stroke="none" />
    <circle cx="12" cy="12" r="1.4" fill="currentColor" stroke="none" />
    <circle cx="12" cy="19" r="1.4" fill="currentColor" stroke="none" />
  </Svg>
);
export const IconDots = (p: P) => (
  <Svg {...p}>
    <circle cx="5" cy="12" r="1.5" fill="currentColor" stroke="none" />
    <circle cx="12" cy="12" r="1.5" fill="currentColor" stroke="none" />
    <circle cx="19" cy="12" r="1.5" fill="currentColor" stroke="none" />
  </Svg>
);
export const IconPlay = (p: P) => (
  <Svg {...p}>
    <path d="M7 4.5v15l13-7.5z" fill="currentColor" stroke="none" />
  </Svg>
);
export const IconPause = (p: P) => (
  <Svg {...p}>
    <rect x="6" y="4.5" width="4" height="15" rx="1" fill="currentColor" stroke="none" />
    <rect x="14" y="4.5" width="4" height="15" rx="1" fill="currentColor" stroke="none" />
  </Svg>
);
export const IconPrev = (p: P) => (
  <Svg {...p}>
    <path d="M6 5v14" strokeWidth={2.4} />
    <path d="M19 5v14L8 12z" fill="currentColor" stroke="none" />
  </Svg>
);
export const IconTimer = (p: P) => (
  <Svg {...p}>
    <circle cx="12" cy="13" r="8" />
    <path d="M12 9v4l2.5 2M9.5 2.5h5" />
  </Svg>
);
export const IconMetronome = (p: P) => (
  <Svg {...p}>
    <path d="M9 3h6l4 18H5z" />
    <path d="M12 16 17 6" />
    <path d="M7 16h10" />
  </Svg>
);
export const IconCapo = (p: P) => (
  <Svg {...p}>
    <path d="M8 3c3 0 5 1.5 5 4v3l-3 1.5V21H7V9.5L5 8V5c0-1.2 1.3-2 3-2z" fill="currentColor" stroke="none" />
    <path d="M13 7h3a2 2 0 0 1 2 2" />
  </Svg>
);
export const IconTranspose = (p: P) => (
  <Svg {...p}>
    <path d="M8 20V4M4 8l4-4 4 4M16 4v16M12 16l4 4 4-4" />
  </Svg>
);
export const IconPiano = (p: P) => (
  <Svg {...p}>
    <rect x="3" y="4" width="18" height="16" rx="1.5" />
    <path d="M8 4v16M13 4v16M18 4v16" />
    <path d="M6.5 4h3v8h-3zM11.5 4h3v8h-3zM16.5 4h3v8h-3z" fill="currentColor" />
  </Svg>
);
export const IconDownload = (p: P) => (
  <Svg {...p}>
    <path d="M12 3v12M7 10l5 5 5-5M4 20h16" />
  </Svg>
);
export const IconPrinter = (p: P) => (
  <Svg {...p}>
    <path d="M7 8V3h10v5" />
    <rect x="3" y="8" width="18" height="9" rx="2" />
    <path d="M7 14h10v7H7z" />
  </Svg>
);
export const IconSpeaker = (p: P) => (
  <Svg {...p}>
    <path d="M4 9.5h3.5L12 6v12l-4.5-3.5H4z" fill="currentColor" />
    <path d="M15.5 9a4 4 0 0 1 0 6M18 6.5a7.5 7.5 0 0 1 0 11" />
  </Svg>
);
export const IconGuitar = (p: P) => (
  <Svg {...p}>
    <path d="m14 10 6-6M18 3l3 3" />
    <path d="M13.2 10.8a4.2 4.2 0 0 0-5.9.1 3 3 0 0 1-2.2 1A3.2 3.2 0 0 0 2 15.2c0 3.9 3.6 6.8 6.8 6.8a3.2 3.2 0 0 0 3.3-3.1 3 3 0 0 1 1-2.2 4.2 4.2 0 0 0 .1-5.9z" fill="currentColor" fillOpacity={0.25} />
    <circle cx="9.5" cy="14.5" r="1.6" />
  </Svg>
);
export const IconUkulele = (p: P) => (
  <Svg {...p}>
    <path d="m14 10 6-6M18.5 2.5l3 3" />
    <path d="M13 11a3.6 3.6 0 0 0-5 0 2.6 2.6 0 0 1-1.9.8A2.9 2.9 0 0 0 3 14.6C3 18 6 21 9.4 21a2.9 2.9 0 0 0 2.8-3.1 2.6 2.6 0 0 1 .8-1.9 3.6 3.6 0 0 0 0-5z" fill="currentColor" fillOpacity={0.45} />
    <circle cx="9.5" cy="14.5" r="1.3" />
  </Svg>
);
export const IconMandolin = (p: P) => (
  <Svg {...p}>
    <path d="m13.5 10.5 7-7M19 2l3 3" />
    <path d="M12.5 11.5C10 9 4.5 10 3 14.5S6 22 9.5 21s5.5-7 3-9.5z" fill="currentColor" fillOpacity={0.3} />
    <circle cx="8.5" cy="15.5" r="1.4" />
  </Svg>
);
export const IconBass = (p: P) => (
  <Svg {...p}>
    <path d="m11 13 9.5-9.5M19.5 1.8l2.7 2.7" />
    <path d="M10.6 13.4a3.3 3.3 0 0 0-4.6.3 2.3 2.3 0 0 1-1.7.7A2.5 2.5 0 0 0 2 16.9C2 19.8 4.3 22 7.1 22a2.5 2.5 0 0 0 2.5-2.3 2.3 2.3 0 0 1 .7-1.7 3.3 3.3 0 0 0 .3-4.6z" fill="currentColor" fillOpacity={0.3} />
    <path d="m5.6 18.4 2.6-2.6" />
  </Svg>
);
export const IconList = (p: P) => (
  <Svg {...p}>
    <path d="M9 6h11M9 12h11M9 18h11" />
    <path d="M4 6h1M4 12h1M4 18h1" strokeWidth={2.6} />
  </Svg>
);
export const IconPlayCircle = (p: P) => (
  <Svg {...p}>
    <circle cx="12" cy="12" r="10" fill="currentColor" stroke="none" />
    <path d="M10 8v8l6-4z" fill="var(--panel-solid, #fff)" stroke="none" />
  </Svg>
);
export const IconTuner = (p: P) => (
  <Svg {...p}>
    <path d="M5 4v4M5 12v8M12 4v10M12 18v2M19 4v2M19 10v10" />
    <rect x="3" y="8" width="4" height="4" rx="1" />
    <rect x="10" y="14" width="4" height="4" rx="1" />
    <rect x="17" y="6" width="4" height="4" rx="1" />
  </Svg>
);
export const IconNote = (p: P) => (
  <Svg {...p}>
    <path d="M9 18V5l11-2v13" />
    <circle cx="6" cy="18" r="3" fill="currentColor" />
    <circle cx="17" cy="16" r="3" fill="currentColor" />
  </Svg>
);
export const IconHeadphones = (p: P) => (
  <Svg {...p}>
    <path d="M3 18v-6a9 9 0 0 1 18 0v6" />
    <path d="M21 19a2 2 0 0 1-2 2h-1v-7h3zM3 19a2 2 0 0 0 2 2h1v-7H3z" fill="currentColor" />
  </Svg>
);
export const IconPlus = (p: P) => (
  <Svg {...p}>
    <path d="M12 5v14M5 12h14" />
  </Svg>
);
export const IconClose = (p: P) => (
  <Svg {...p}>
    <path d="M6 6l12 12M18 6 6 18" />
  </Svg>
);
export const IconEdit = (p: P) => (
  <Svg {...p}>
    <path d="M4 20h4L19 9l-4-4L4 16z" />
    <path d="m13.5 6.5 4 4" />
  </Svg>
);
export const IconTrash = (p: P) => (
  <Svg {...p}>
    <path d="M4 7h16M9 7V4h6v3M6 7l1 13h10l1-13" />
  </Svg>
);
export const IconUpload = (p: P) => (
  <Svg {...p}>
    <path d="M12 16V4M7 9l5-5 5 5M4 20h16" />
  </Svg>
);
export const IconLoop = (p: P) => (
  <Svg {...p}>
    <path d="M17 2l3 3-3 3" />
    <path d="M4 11V9a4 4 0 0 1 4-4h12M7 22l-3-3 3-3" />
    <path d="M20 13v2a4 4 0 0 1-4 4H4" />
  </Svg>
);
export const IconCheck = (p: P) => (
  <Svg {...p}>
    <path d="m5 12 5 5L20 7" />
  </Svg>
);
export const IconLogout = (p: P) => (
  <Svg {...p}>
    <path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4M16 17l5-5-5-5M21 12H9" />
  </Svg>
);
export const IconSun = (p: P) => (
  <Svg {...p}>
    <circle cx="12" cy="12" r="4" />
    <path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4" />
  </Svg>
);
export const IconMoon = (p: P) => (
  <Svg {...p}>
    <path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z" />
  </Svg>
);
export const IconWand = (p: P) => (
  <Svg {...p}>
    <path d="m15 4 1 2 2 1-2 1-1 2-1-2-2-1 2-1zM19 12l.7 1.3L21 14l-1.3.7L19 16l-.7-1.3L17 14l1.3-.7zM3 21 13.5 10.5" />
  </Svg>
);
export const IconYoutube = (p: P) => (
  <Svg {...p}>
    <rect x="2.5" y="5.5" width="19" height="13" rx="4" />
    <path d="m10 9 5 3-5 3z" fill="currentColor" />
  </Svg>
);
export const IconMusicFile = (p: P) => (
  <Svg {...p}>
    <path d="M6 2h9l5 5v15H6z" />
    <path d="M14 2v6h6M11 18v-6l4-1" />
    <circle cx="9.5" cy="18" r="1.5" fill="currentColor" />
  </Svg>
);
export const IconBolt = (p: P) => (
  <Svg {...p}>
    <path d="M13 2 4 14h7l-1 8 9-12h-7z" fill="currentColor" stroke="none" />
  </Svg>
);
export const IconMenu = (p: P) => (
  <Svg {...p}>
    <path d="M4 7h16M4 12h16M4 17h16" />
  </Svg>
);
export const IconUser = (p: P) => (
  <Svg {...p}>
    <circle cx="12" cy="8" r="4" />
    <path d="M4 21a8 8 0 0 1 16 0" />
  </Svg>
);

export const IconMail = (p: P) => (
  <Svg {...p}>
    <rect x="3" y="5" width="18" height="14" rx="2" />
    <path d="m3.5 6.5 8.5 6.5 8.5-6.5" />
  </Svg>
);

export const IconLock = (p: P) => (
  <Svg {...p}>
    <rect x="4.5" y="10.5" width="15" height="10" rx="2" />
    <path d="M8 10.5V7.5a4 4 0 0 1 8 0v3" />
    <circle cx="12" cy="15.5" r="1.2" />
  </Svg>
);

export const IconEye = (p: P) => (
  <Svg {...p}>
    <path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12Z" />
    <circle cx="12" cy="12" r="3" />
  </Svg>
);

export const IconEyeOff = (p: P) => (
  <Svg {...p}>
    <path d="M10.6 5.1A10 10 0 0 1 12 5c6.4 0 10 7 10 7a17 17 0 0 1-3.2 4.1M6.6 6.6A17 17 0 0 0 2 12s3.6 7 10 7a9.6 9.6 0 0 0 5.4-1.6" />
    <path d="M9.9 9.9a3 3 0 0 0 4.2 4.2" />
    <path d="m3 3 18 18" />
  </Svg>
);
