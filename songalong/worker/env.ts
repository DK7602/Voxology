export interface Env {
  DB: D1Database;
  AUDIO: R2Bucket;
  /** Cloudflare Email Service binding (optional). */
  EMAIL?: SendEmail;
  /** Resend API key (optional secret), used when EMAIL is not bound. */
  RESEND_API_KEY?: string;
  APP_NAME: string;
  /** e.g. `SongAlong <no-reply@example.com>` */
  MAIL_FROM: string;
  /** Public origin of the app, e.g. https://songalong.example.com (for links in email). */
  APP_URL?: string;
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
  };
}
