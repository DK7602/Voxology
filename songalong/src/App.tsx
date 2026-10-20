import { useState } from "react";
import { Navigate, Outlet, Route, Routes, useLocation } from "react-router-dom";
import { useAuth } from "./auth";
import { ErrorBoundary } from "./components/ErrorBoundary";
import { NavBar } from "./components/NavBar";
import { SongEditorPage } from "./editor/SongEditor";
import { ForgotPasswordPage, LoginPage, RegisterPage, ResetPasswordPage } from "./pages/AuthPages";
import {
  DiscoverPage,
  HomePage,
  LibraryPage,
  PlaylistPage,
  PlaylistsPage,
} from "./pages/LibraryPages";
import { PreparePage } from "./pages/PreparePage";
import { SearchPage } from "./pages/SearchPage";
import { SettingsPage } from "./pages/SettingsPage";
import { ChordLibraryPage, ToolsPage, TunerPage } from "./pages/ToolsPages";
import { PrintPage } from "./song/PrintPage";
import { SongPage } from "./song/SongPage";
import { useNewVersion } from "./update";

function Loading() {
  return (
    <div className="boot" aria-busy="true">
      <img src="/brand/emblem.webp" alt="Loading SongAlong" width={110} height={110} />
    </div>
  );
}

/** "A new version is ready": shown once a newer version is live than the one running. */
function UpdateBanner() {
  const ready = useNewVersion();
  const [later, setLater] = useState(false);
  if (!ready || later) return null;
  return (
    <div className="update-banner" role="status">
      <span>A new version is ready.</span>
      <button className="btn btn-sm btn-primary" onClick={() => location.reload()}>Reload</button>
      <button className="btn btn-sm" onClick={() => setLater(true)}>Later</button>
    </div>
  );
}

/** Signed-in area: redirects to sign-in otherwise. */
function RequireUser({ shell = true }: { shell?: boolean }) {
  const { user, loading } = useAuth();
  const location = useLocation();
  if (loading) return <Loading />;
  if (!user) return <Navigate to="/login" replace state={{ from: location.pathname + location.search }} />;
  if (!shell) return <Outlet />;
  return (
    <div className="app">
      <NavBar />
      <UpdateBanner />
      <main className="page">
        <ErrorBoundary resetKey={location.pathname}>
          <Outlet />
        </ErrorBoundary>
      </main>
    </div>
  );
}

/** Sign-in pages: skip them when already signed in. */
function GuestOnly() {
  const { user, loading } = useAuth();
  if (loading) return <Loading />;
  if (user) return <Navigate to="/" replace />;
  return <Outlet />;
}

export function App() {
  return (
    <Routes>
      <Route element={<GuestOnly />}>
        <Route path="/login" element={<LoginPage />} />
        <Route path="/register" element={<RegisterPage />} />
        <Route path="/forgot-password" element={<ForgotPasswordPage />} />
      </Route>
      {/* reachable signed in or out: the link comes from an email */}
      <Route path="/reset-password" element={<ResetPasswordPage />} />
      <Route element={<RequireUser shell={false} />}>
        <Route path="/song/:id/print" element={<PrintPage />} />
      </Route>
      <Route element={<RequireUser />}>
        <Route path="/" element={<HomePage />} />
        <Route path="/discover" element={<DiscoverPage />} />
        <Route path="/library" element={<LibraryPage />} />
        <Route path="/search" element={<SearchPage />} />
        <Route path="/playlists" element={<PlaylistsPage />} />
        <Route path="/playlists/:id" element={<PlaylistPage />} />
        <Route path="/song/:id" element={<SongPage />} />
        <Route path="/yt/:videoId" element={<PreparePage />} />
        <Route path="/song/:id/edit" element={<SongEditorPage />} />
        <Route path="/new" element={<SongEditorPage />} />
        <Route path="/tools" element={<ToolsPage />} />
        <Route path="/tools/chords" element={<ChordLibraryPage />} />
        <Route path="/tools/tuner" element={<TunerPage />} />
        <Route path="/settings" element={<SettingsPage />} />
        <Route path="*" element={<Navigate to="/" replace />} />
      </Route>
    </Routes>
  );
}
