import { useState, type FormEvent, type ReactNode } from "react";
import { Link, useLocation, useNavigate, useSearchParams } from "react-router-dom";
import { PASSWORD_MIN } from "../../shared/api";
import { api } from "../api";
import { useAuth } from "../auth";
import { Logo } from "../components/Artwork";
import { errorText } from "../components/ui";

function AuthCard({ title, lead, children }: { title: string; lead?: string; children: ReactNode }) {
  return (
    <div className="auth-wrap">
      <div className="panel auth-card">
        <Logo />
        <h1>{title}</h1>
        {lead && <p className="lead">{lead}</p>}
        {children}
      </div>
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
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [remember, setRemember] = useState(true);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const r = await api.login({ email, password, remember });
      setUser(r.user);
      done();
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthCard title="Welcome back" lead="Sign in to play along with your songs.">
      <form onSubmit={submit} style={{ display: "grid", gap: 14 }}>
        <label className="field">
          <span>Email</span>
          <input className="input" type="email" autoComplete="email" required value={email} onChange={(e) => setEmail(e.target.value)} />
        </label>
        <label className="field">
          <span>Password</span>
          <input className="input" type="password" autoComplete="current-password" required value={password} onChange={(e) => setPassword(e.target.value)} />
        </label>
        <label className="check">
          <input type="checkbox" checked={remember} onChange={(e) => setRemember(e.target.checked)} /> Keep me signed in
        </label>
        {error && <div className="notice error" role="alert">{error}</div>}
        <button className="btn btn-primary btn-block" disabled={busy}>
          {busy ? "Signing in…" : "Sign in"}
        </button>
      </form>
      <div className="auth-links">
        <Link to="/forgot-password">Forgot your password?</Link>
        <Link to="/register">Create an account</Link>
      </div>
    </AuthCard>
  );
}

export function RegisterPage() {
  const { setUser } = useAuth();
  const navigate = useNavigate();
  const [name, setName] = useState("");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [confirm, setConfirm] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (password !== confirm) return setError("The two passwords don't match.");
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
    <AuthCard title="Create your account" lead="Save songs, playlists and your capo / transpose settings.">
      <form onSubmit={submit} style={{ display: "grid", gap: 14 }}>
        <label className="field">
          <span>Your name</span>
          <input className="input" autoComplete="name" value={name} onChange={(e) => setName(e.target.value)} maxLength={80} />
        </label>
        <label className="field">
          <span>Email</span>
          <input className="input" type="email" autoComplete="email" required value={email} onChange={(e) => setEmail(e.target.value)} />
        </label>
        <label className="field">
          <span>Password (at least {PASSWORD_MIN} characters)</span>
          <input className="input" type="password" autoComplete="new-password" required minLength={PASSWORD_MIN} value={password} onChange={(e) => setPassword(e.target.value)} />
        </label>
        <label className="field">
          <span>Password again</span>
          <input className="input" type="password" autoComplete="new-password" required value={confirm} onChange={(e) => setConfirm(e.target.value)} />
        </label>
        {error && <div className="notice error" role="alert">{error}</div>}
        <button className="btn btn-primary btn-block" disabled={busy}>
          {busy ? "Creating…" : "Create account"}
        </button>
      </form>
      <div className="auth-links">
        <span className="muted">Already have an account?</span>
        <Link to="/login">Sign in</Link>
      </div>
    </AuthCard>
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
    <AuthCard title="Reset your password" lead={sent ? undefined : "We'll email you a link to choose a new one."}>
      {sent ? (
        <div className="notice" role="status">
          If an account exists for <b>{email}</b>, a reset link is on its way. It works for one hour. Check your spam
          folder if it doesn't arrive in a few minutes.
        </div>
      ) : (
        <form onSubmit={submit} style={{ display: "grid", gap: 14 }}>
          <label className="field">
            <span>Email</span>
            <input className="input" type="email" autoComplete="email" required value={email} onChange={(e) => setEmail(e.target.value)} />
          </label>
          {error && <div className="notice error" role="alert">{error}</div>}
          <button className="btn btn-primary btn-block" disabled={busy}>
            {busy ? "Sending…" : "Send reset link"}
          </button>
        </form>
      )}
      <div className="auth-links">
        <Link to="/login">Back to sign in</Link>
      </div>
    </AuthCard>
  );
}

export function ResetPasswordPage() {
  const [params] = useSearchParams();
  const token = params.get("token") ?? "";
  const { setUser } = useAuth();
  const navigate = useNavigate();
  const [password, setPassword] = useState("");
  const [confirm, setConfirm] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (password !== confirm) return setError("The two passwords don't match.");
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
      <AuthCard title="Reset link missing">
        <div className="notice error">This page needs the link from your email. Ask for a new one below.</div>
        <Link className="btn btn-block" to="/forgot-password">Send a new link</Link>
      </AuthCard>
    );

  return (
    <AuthCard title="Choose a new password" lead="You'll be signed in, and signed out everywhere else.">
      <form onSubmit={submit} style={{ display: "grid", gap: 14 }}>
        <label className="field">
          <span>New password (at least {PASSWORD_MIN} characters)</span>
          <input className="input" type="password" autoComplete="new-password" required minLength={PASSWORD_MIN} value={password} onChange={(e) => setPassword(e.target.value)} />
        </label>
        <label className="field">
          <span>New password again</span>
          <input className="input" type="password" autoComplete="new-password" required value={confirm} onChange={(e) => setConfirm(e.target.value)} />
        </label>
        {error && (
          <div className="notice error" role="alert">
            {error} {/expired|invalid/i.test(error) && <Link to="/forgot-password">Get a new link</Link>}
          </div>
        )}
        <button className="btn btn-primary btn-block" disabled={busy}>
          {busy ? "Saving…" : "Save new password"}
        </button>
      </form>
    </AuthCard>
  );
}
