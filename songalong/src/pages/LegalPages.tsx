import type { ReactNode } from "react";
import { Link, useNavigate } from "react-router-dom";

function LegalPage({ title, children }: { title: string; children: ReactNode }) {
  const navigate = useNavigate();
  return (
    <div className="auth-scene legal-scene">
      <article className="auth-card legal-card">
        <Link to="/" aria-label="SongAlong home">
          <img className="legal-logo" src="/brand/wordmark.webp" alt="SongAlong" width={174} height={48} />
        </Link>
        <h1>{title}</h1>
        <p className="auth-lead">Last updated October 2026</p>
        <div className="legal-body">{children}</div>
        <button type="button" className="auth-outline" onClick={() => (history.length > 1 ? navigate(-1) : navigate("/"))}>
          Back
        </button>
      </article>
    </div>
  );
}

export function TermsPage() {
  return (
    <LegalPage title="Terms of Service">
      <p>SongAlong helps you learn and play along with songs. By making an account or using the app you agree to these terms.</p>
      <h2>Your account</h2>
      <p>
        Keep your password to yourself; you're responsible for what happens under your account. You can delete your account at any
        time from Settings.
      </p>
      <h2>Using SongAlong</h2>
      <ul>
        <li>SongAlong is for personal practice and learning.</li>
        <li>Only upload recordings and add songs you have the right to use, and don't make public anything you don't have the right to share.</li>
        <li>Don't misuse the service: no attempts to break in, overload it, or collect other people's data.</li>
      </ul>
      <h2>Chords, lyrics and timing</h2>
      <p>
        Chords, beats and lyric timing are worked out automatically and may contain mistakes. Lyrics come from outside sources. Songs
        and recordings belong to their writers and owners; SongAlong doesn't claim them.
      </p>
      <h2>Changes and availability</h2>
      <p>
        We may change or pause features, and update these terms; when the change matters we'll say so in the app. We may close
        accounts that break these terms. SongAlong is provided as is, without warranties, as far as the law allows.
      </p>
    </LegalPage>
  );
}

export function PrivacyPage() {
  return (
    <LegalPage title="Privacy Policy">
      <p>This page explains what SongAlong keeps about you and why. We don't sell your information or show ads.</p>
      <h2>What we keep</h2>
      <ul>
        <li>Your account: email address, name, and a scrambled (hashed) password. If you sign in with Apple, Google or Facebook we keep the id that service gives us and the email it shares.</li>
        <li>What you make and choose: songs, playlists, favourites, settings, practice progress, and recordings you upload.</li>
        <li>One sign-in cookie that keeps you logged in. No advertising or tracking cookies.</li>
      </ul>
      <h2>Getting a song ready</h2>
      <p>
        When you pick a song from search, its audio is fetched for a short time to find the beat and chords, and short pieces of it
        are run through speech recognition to line the lyrics up. The audio is deleted afterwards; only the chords, timing and lyrics
        are kept so the song opens quickly next time.
      </p>
      <h2>Who helps us run it</h2>
      <p>
        SongAlong runs on Cloudflare (hosting, database, storage and speech recognition). Lyrics come from LRCLIB and search results
        from YouTube. Password-reset emails go through our email provider.
      </p>
      <h2>Your choices</h2>
      <p>
        You can change your details in Settings and delete your account there; that removes your account, songs, playlists and
        uploads.
      </p>
    </LegalPage>
  );
}
