export interface Env {
  DB: D1Database;
  AUDIO: R2Bucket;
  /** Cloudflare Email Service binding (optional). */
  EMAIL?: SendEmail;
  /** Gmail account that sends the mail when EMAIL is not bound (optional secrets): the address,
   *  and an app password from myaccount.google.com/apppasswords (needs 2-Step Verification). */
  GMAIL_USER?: string;
  GMAIL_APP_PASSWORD?: string;
  /** Resend API key (optional secret), used when neither EMAIL nor Gmail is set up. */
  RESEND_API_KEY?: string;
  APP_NAME: string;
  /** e.g. `SongAlong <no-reply@example.com>` */
  MAIL_FROM: string;
  /** Public origin of the app, e.g. https://songalong.example.com (for links in email). */
  APP_URL?: string;
  /** Song-prep container (yt-dlp + ffmpeg + chord detection). Optional: without it, songs go
   *  straight to the user's PC helper. */
  PREP?: DurableObjectNamespace<import("./prepContainer").PrepContainer>;
  /** Workers AI, for speech recognition when lining lyrics up with a video. */
  AI?: import("./align").SpeechAi;
  /** YouTube Data API key for search (optional; without it the prep container searches). */
  YOUTUBE_API_KEY?: string;
  /** Sign in with Google / Facebook / Apple (each optional; see worker/oauth.ts). */
  GOOGLE_CLIENT_ID?: string;
  GOOGLE_CLIENT_SECRET?: string;
  FACEBOOK_APP_ID?: string;
  FACEBOOK_APP_SECRET?: string;
  APPLE_CLIENT_ID?: string;
  APPLE_TEAM_ID?: string;
  APPLE_KEY_ID?: string;
  APPLE_PRIVATE_KEY?: string;
}

export interface UserRow {
  id: string;
  email: string;
  name: string;
  password_hash: string;
  settings: string;
  created_at: number;
  password_changed_at: number;
}

export interface AppEnv {
  Bindings: Env;
  Variables: {
    user: UserRow | null;
    /** SHA-256 hex of the current session token */
    sessionHash: string | null;
    /** set on PC-helper requests (Bearer helper key) */
    helperUserId?: string;
    helperTokenHash?: string;
  };
}
