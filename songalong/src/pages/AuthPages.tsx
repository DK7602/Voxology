import { useEffect, useState, type FormEvent, type ReactNode } from "react";
import { Link, useLocation, useNavigate, useSearchParams } from "react-router-dom";
import { PASSWORD_MIN } from "../../shared/api";
import { api } from "../api";
import { useAuth } from "../auth";
import { IconChevronRight, IconEye, IconEyeOff, IconLock, IconMail, IconUser } from "../components/Icons";
import { errorText } from "../components/ui";

/** Black-and-gold sign-in screens: the logo on the left, the form card on the right. */
function AuthScene({ title, lead, children }: { title: string; lead?: string; children: ReactNode }) {
  return (
    <div className="auth-scene">
      <div className="auth-brand">
        <img className="auth-logo" src="/brand/logo.webp" alt="SongAlong" width={900} height={900} />
        <p className="auth-tagline">Play along with chords and lyrics.</p>
      </div>
      <div className="auth-card">
        <h1>{title}</h1>
        {lead && <p className="auth-lead">{lead}</p>}
        {children}
        <p className="auth-legal">
          By continuing, you agree to our <Link to="/terms">Terms of Service</Link> and <Link to="/privacy">Privacy Policy</Link>.
        </p>
      </div>
    </div>
  );
}

function Field(props: {
  icon: ReactNode;
  label: string;
  type?: string;
  value: string;
  onChange: (v: string) => void;
  autoComplete?: string;
  required?: boolean;
  minLength?: number;
  maxLength?: number;
}) {
  const [shown, setShown] = useState(false);
  const isPassword = props.type === "password";
  return (
    <label className="auth-field">
      <span className="auth-field-icon">{props.icon}</span>
      <input
        type={isPassword && shown ? "text" : props.type ?? "text"}
        placeholder={props.label}
        aria-label={props.label}
        value={props.value}
        onChange={(e) => props.onChange(e.target.value)}
        autoComplete={props.autoComplete}
        required={props.required}
        minLength={props.minLength}
        maxLength={props.maxLength}
      />
      {isPassword && (
        <button type="button" className="auth-eye" onClick={() => setShown((s) => !s)} aria-label={shown ? "Hide password" : "Show password"}>
          {shown ? <IconEyeOff size={22} /> : <IconEye size={22} />}
        </button>
      )}
    </label>
  );
}

// ---- Apple / Google / Facebook ----

const PROVIDER_NAMES = { apple: "Apple", google: "Google", facebook: "Facebook" } as const;
type Provider = keyof typeof PROVIDER_NAMES;

const OAUTH_ERRORS: Record<string, string> = {
  not_configured: "Signing in with {p} isn't switched on yet. Use your email and password for now.",
  cancelled: "Signing in with {p} was cancelled.",
  expired: "That sign-in took too long or was opened in another browser. Please try again.",
  no_email: "{p} didn't share a confirmed email address, so we couldn't make your account. Try another way to sign in.",
  failed: "Signing in with {p} didn't work. Please try again.",
};

function oauthMessage(code: string | null, provider: string | null): string | null {
  if (!code) return null;
  const name = PROVIDER_NAMES[provider as Provider] ?? "that service";
  return (OAUTH_ERRORS[code] ?? OAUTH_ERRORS.failed).replace("{p}", name);
}

function ProviderIcon({ p }: { p: Provider }) {
  if (p === "google")
    return (
      <svg width="26" height="26" viewBox="0 0 48 48" aria-hidden="true">
        <path fill="#EA4335" d="M24 9.5c3.54 0 6.71 1.22 9.21 3.6l6.85-6.85C35.9 2.38 30.47 0 24 0 14.62 0 6.51 5.38 2.56 13.22l7.98 6.19C12.43 13.72 17.74 9.5 24 9.5z" />
        <path fill="#4285F4" d="M46.98 24.55c0-1.57-.15-3.09-.38-4.55H24v9.02h12.94c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z" />
        <path fill="#FBBC05" d="M10.53 28.59c-.48-1.45-.76-2.99-.76-4.59s.27-3.14.76-4.59l-7.98-6.19C.92 16.46 0 20.12 0 24c0 3.88.92 7.54 2.56 10.78l7.97-6.19z" />
        <path fill="#34A853" d="M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.26 0-11.57-4.22-13.47-9.91l-7.98 6.19C6.51 42.62 14.62 48 24 48z" />
      </svg>
    );
  if (p === "facebook")
    return (
      <svg width="26" height="26" viewBox="0 0 24 24" aria-hidden="true">
        <circle cx="12" cy="12" r="12" fill="#1877F2" />
        <path fill="#fff" d="M15.1 15.5l.5-3.5h-3.3V9.8c0-1 .5-1.9 2-1.9h1.5v-3s-1.4-.2-2.7-.2c-2.7 0-4.5 1.7-4.5 4.7V12H5.5v3.5h3.1V24a12 12 0 0 0 3.7 0v-8.5h2.8z" />
      </svg>
    );
  return (
    <svg width="26" height="26" viewBox="0 0 24 24" aria-hidden="true">
      <path
        fill="currentColor"
        d="M17.05 20.28c-.98.95-2.05.8-3.08.35-1.09-.46-2.09-.48-3.24 0-1.44.62-2.2.44-3.06-.35C2.79 15.25 3.51 7.59 9.05 7.31c1.35.07 2.29.74 3.08.8 1.18-.24 2.31-.93 3.57-.84 1.51.12 2.65.72 3.4 1.8-3.12 1.87-2.38 5.98.48 7.13-.57 1.5-1.31 2.99-2.54 4.09zM12.03 7.25c-.15-2.23 1.66-4.07 3.74-4.25.29 2.58-2.34 4.5-3.74 4.25z"
      />
    </svg>
  );
}

function SocialSignIn({ verb, onMessage }: { verb: string; onMessage: (m: string) => void }) {
  const location = useLocation();
  const from = (location.state as { from?: string } | null)?.from ?? "/";
  const [on, setOn] = useState<Partial<Record<Provider, boolean>>>({});
  useEffect(() => {
    api.oauthProviders().then((r) => setOn(r.providers)).catch(() => {});
  }, []);
  const go = (p: Provider) => {
    if (on[p] === false) return onMessage(oauthMessage("not_configured", p)!);
    window.location.href = `/api/auth/oauth/${p}/start?${new URLSearchParams({ from })}`;
  };
  return (
    <div className="auth-social">
      {(Object.keys(PROVIDER_NAMES) as Provider[]).map((p) => (
        <button key={p} type="button" className="auth-social-btn" onClick={() => go(p)}>
          <ProviderIcon p={p} />
          <span>
            {verb} with
            <b>{PROVIDER_NAMES[p]}</b>
          </span>
        </button>
      ))}
    </div>
  );
}

function Or() {
  return (
    <div className="auth-or" role="separator">
      <span>OR</span>
    </div>
  );
}

function useAfterLogin() {
  const navigate = useNavigate();
  const location = useLocation();
  const from = (location.state as { from?: string } | null)?.from;
  return () => navigate(from && from !== "/login" ? from : "/", { replace: true });
}

export function LoginPage() {
  const { setUser } = useAuth();
  const done = useAfterLogin();
  const [params] = useSearchParams();
  const location = useLocation();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(() => oauthMessage(params.get("oauth_error"), params.get("provider")));

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const r = await api.login({ email, password, remember: true });
      setUser(r.user);
      done();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthScene title="Welcome Back" lead="Log in to your SongAlong account.">
      <form onSubmit={submit} className="auth-form">
        <Field icon={<IconMail size={22} />} label="Email" type="email" autoComplete="email" required value={email} onChange={setEmail} />
        <Field icon={<IconLock size={22} />} label="Password" type="password" autoComplete="current-password" required value={password} onChange={setPassword} />
        <Link className="auth-forgot" to="/forgot-password">Forgot password?</Link>
        {error && <div className="auth-notice" role="alert">{error}</div>}
        <button className="auth-gold" disabled={busy}>
          <span>{busy ? "Logging in…" : "Log In"}</span>
          <IconChevronRight size={24} />
        </button>
      </form>
      <Or />
      <Link className="auth-outline" to="/register" state={location.state}>Create Account</Link>
      <SocialSignIn verb="Sign in" onMessage={setError} />
    </AuthScene>
  );
}

export function RegisterPage() {
  const { setUser } = useAuth();
  const navigate = useNavigate();
  const location = useLocation();
  const [name, setName] = useState("");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (password.length < PASSWORD_MIN) return setError(`Use at least ${PASSWORD_MIN} characters for your password.`);
    setBusy(true);
    setError(null);
    try {
      const r = await api.register({ name, email, password });
      setUser(r.user);
      navigate("/", { replace: true });
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthScene title="Create Account" lead="Save songs, playlists and your capo and transpose settings.">
      <form onSubmit={submit} className="auth-form">
        <Field icon={<IconUser size={22} />} label="Your name" autoComplete="name" maxLength={80} value={name} onChange={setName} />
        <Field icon={<IconMail size={22} />} label="Email" type="email" autoComplete="email" required value={email} onChange={setEmail} />
        <Field
          icon={<IconLock size={22} />}
          label={`Password (at least ${PASSWORD_MIN} characters)`}
          type="password"
          autoComplete="new-password"
          required
          minLength={PASSWORD_MIN}
          value={password}
          onChange={setPassword}
        />
        {error && <div className="auth-notice" role="alert">{error}</div>}
        <button className="auth-gold" disabled={busy}>
          <span>{busy ? "Creating…" : "Create Account"}</span>
          <IconChevronRight size={24} />
        </button>
      </form>
      <Or />
      <Link className="auth-outline" to="/login" state={location.state}>I already have an account</Link>
      <SocialSignIn verb="Sign up" onMessage={setError} />
    </AuthScene>
  );
}

export function ForgotPasswordPage() {
  const [email, setEmail] = useState("");
  const [sent, setSent] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await api.forgot(email);
      setSent(true);
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthScene title="Reset Password" lead={sent ? undefined : "We'll email you a link to choose a new one."}>
      {sent ? (
        <div className="auth-notice ok" role="status">
          If an account exists for <b>{email}</b>, a reset link is on its way. It works for one hour. Check your spam folder if it
          doesn't arrive in a few minutes.
        </div>
      ) : (
        <form onSubmit={submit} className="auth-form">
          <Field icon={<IconMail size={22} />} label="Email" type="email" autoComplete="email" required value={email} onChange={setEmail} />
          {error && <div className="auth-notice" role="alert">{error}</div>}
          <button className="auth-gold" disabled={busy}>
            <span>{busy ? "Sending…" : "Send Reset Link"}</span>
            <IconChevronRight size={24} />
          </button>
        </form>
      )}
      <Link className="auth-outline" to="/login">Back to Log In</Link>
    </AuthScene>
  );
}

export function ResetPasswordPage() {
  const [params] = useSearchParams();
  const token = params.get("token") ?? "";
  const { setUser } = useAuth();
  const navigate = useNavigate();
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (password.length < PASSWORD_MIN) return setError(`Use at least ${PASSWORD_MIN} characters.`);
    setBusy(true);
    setError(null);
    try {
      const r = await api.reset(token, password);
      setUser(r.user);
      navigate("/", { replace: true });
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  };

  if (!token)
    return (
      <AuthScene title="Reset Link Missing">
        <div className="auth-notice" role="alert">This page needs the link from your email. Ask for a new one below.</div>
        <Link className="auth-outline" to="/forgot-password">Send a New Link</Link>
      </AuthScene>
    );

  return (
    <AuthScene title="New Password" lead="You'll be logged in here, and logged out everywhere else.">
      <form onSubmit={submit} className="auth-form">
        <Field
          icon={<IconLock size={22} />}
          label={`New password (at least ${PASSWORD_MIN} characters)`}
          type="password"
          autoComplete="new-password"
          required
          minLength={PASSWORD_MIN}
          value={password}
          onChange={setPassword}
        />
        {error && (
          <div className="auth-notice" role="alert">
            {error} {/expired|invalid/i.test(error) && <Link to="/forgot-password">Get a new link</Link>}
          </div>
        )}
        <button className="auth-gold" disabled={busy}>
          <span>{busy ? "Saving…" : "Save Password"}</span>
          <IconChevronRight size={24} />
        </button>
      </form>
    </AuthScene>
  );
}
