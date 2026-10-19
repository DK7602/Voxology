import { useState, type FormEvent, type ReactNode } from "react";
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
  const location = useLocation();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

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
