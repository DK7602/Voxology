import { useState, type FormEvent } from "react";
import { NavLink, useNavigate, useSearchParams } from "react-router-dom";
import { useAuth, useUser } from "../auth";
import { Logo } from "./Artwork";
import {
  IconChevronDown,
  IconCompass,
  IconGear,
  IconHome,
  IconLibrary,
  IconLogout,
  IconMoon,
  IconPiano,
  IconPlaylist,
  IconPlus,
  IconSearch,
  IconSun,
  IconTuner,
  IconWrench,
} from "./Icons";
import { Popover } from "./ui";
import { currentPalette, currentTheme } from "../theme";

const LINKS = [
  { to: "/", label: "Home", icon: IconHome, end: true },
  { to: "/discover", label: "Discover", icon: IconCompass },
  { to: "/library", label: "My Library", icon: IconLibrary },
  { to: "/playlists", label: "Playlists", icon: IconPlaylist },
];

export function initials(name: string, email: string): string {
  const src = name.trim() || email;
  const parts = src.split(/[\s@._-]+/).filter(Boolean);
  return ((parts[0]?.[0] ?? "?") + (parts[1]?.[0] ?? "")).toUpperCase();
}

function ToolsMenu({ onPick }: { onPick: (path: string) => void }) {
  return (
    <>
      <button className="menu-item" onClick={() => onPick("/new")}>
        <IconPlus size={20} /> Add a song
      </button>
      <button className="menu-item" onClick={() => onPick("/tools/chords")}>
        <IconPiano size={20} /> Chord library
      </button>
      <button className="menu-item" onClick={() => onPick("/tools/tuner")}>
        <IconTuner size={20} /> Tuner
      </button>
    </>
  );
}

export function NavBar() {
  const user = useUser();
  const { logout, updateSettings } = useAuth();
  const navigate = useNavigate();
  const [params] = useSearchParams();
  const [q, setQ] = useState(params.get("q") ?? "");

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (q.trim()) navigate(`/search?q=${encodeURIComponent(q.trim())}`);
  };
  // light <-> dark, keeping the colors
  const toggleTheme = () => updateSettings({ theme: currentTheme() === "dark" ? "light" : "dark", palette: currentPalette() });

  return (
    <>
      <header className="nav panel">
        <NavLink to="/" className="nav-logo" aria-label="SongAlong home">
          <Logo />
        </NavLink>
        <form className="nav-search" role="search" onSubmit={submit}>
          <IconSearch size={22} />
          <input
            className="nav-search-input"
            placeholder="Search for songs, artists, or chords..."
            value={q}
            onChange={(e) => setQ(e.target.value)}
            aria-label="Search"
          />
        </form>
        <nav className="nav-links" aria-label="Main">
          {LINKS.map(({ to, label, icon: Icon, end }) => (
            <NavLink key={to} to={to} end={end} className="nav-link">
              <Icon size={24} />
              <span>{label}</span>
            </NavLink>
          ))}
          <Popover
            align="end"
            width={230}
            trigger={(p) => (
              <button className="nav-link" {...p} ref={p.ref}>
                <IconWrench size={24} />
                <span>Tools</span>
                <IconChevronDown size={16} />
              </button>
            )}
          >
            {(close) => (
              <ToolsMenu
                onPick={(path) => {
                  close();
                  navigate(path);
                }}
              />
            )}
          </Popover>
        </nav>
        <div className="nav-user">
          <Popover
            align="end"
            width={250}
            trigger={(p) => (
              <button className="avatar" {...p} ref={p.ref} aria-label="Account">
                {initials(user.name, user.email)}
              </button>
            )}
          >
            {(close) => (
              <>
                <div className="menu-label">{user.name || "Signed in"}</div>
                <div className="menu-item faint" style={{ cursor: "default" }}>
                  {user.email}
                </div>
                <div className="menu-sep" />
                <button className="menu-item" onClick={() => { close(); toggleTheme(); }}>
                  {currentTheme() === "dark" ? <IconSun size={20} /> : <IconMoon size={20} />}
                  {currentTheme() === "dark" ? "Light mode" : "Dark mode"}
                </button>
                <button className="menu-item" onClick={() => { close(); navigate("/settings"); }}>
                  <IconGear size={20} /> Settings
                </button>
                <button className="menu-item" onClick={() => { close(); void logout().then(() => navigate("/login")); }}>
                  <IconLogout size={20} /> Sign out
                </button>
              </>
            )}
          </Popover>
          <NavLink to="/settings" className="nav-gear" aria-label="Settings">
            <IconGear size={26} />
          </NavLink>
        </div>
      </header>

      {/* phones: bottom tab bar */}
      <nav className="tabbar panel" aria-label="Main">
        {LINKS.map(({ to, label, icon: Icon, end }) => (
          <NavLink key={to} to={to} end={end} className="tab">
            <Icon size={22} />
            <span>{label === "My Library" ? "Library" : label}</span>
          </NavLink>
        ))}
        <NavLink to="/tools" className="tab">
          <IconWrench size={22} />
          <span>Tools</span>
        </NavLink>
      </nav>
    </>
  );
}
