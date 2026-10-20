import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from "react";
import type { User, UserSettings } from "../shared/api";
import { api, ApiError } from "./api";
import { applyTheme, localLook, lookOf, saveLocalLook } from "./theme";

interface AuthState {
  user: User | null;
  loading: boolean;
  setUser: (u: User | null) => void;
  logout: () => Promise<void>;
  updateSettings: (patch: UserSettings) => Promise<void>;
}

const AuthContext = createContext<AuthState | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [user, setUser] = useState<User | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    api
      .me()
      .then((r) => setUser(r.user))
      .catch((e) => {
        if (!(e instanceof ApiError) || e.status !== 401) console.warn(e);
      })
      .finally(() => setLoading(false));
  }, []);

  useEffect(() => {
    // the account's look once it has one, else this device's
    const s = user?.settings;
    applyTheme(s?.theme || s?.palette ? lookOf(s.theme, s.palette) : localLook());
  }, [user?.settings.theme, user?.settings.palette]);

  const logout = useCallback(async () => {
    try {
      await api.logout();
    } finally {
      setUser(null);
    }
  }, []);

  const updateSettings = useCallback(
    async (patch: UserSettings) => {
      if (!user) return;
      const settings = { ...user.settings, ...patch };
      setUser({ ...user, settings }); // optimistic
      saveLocalLook(patch);
      const r = await api.updateMe({ settings });
      setUser(r.user);
    },
    [user],
  );

  const value = useMemo(() => ({ user, loading, setUser, logout, updateSettings }), [user, loading, logout, updateSettings]);
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth(): AuthState {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error("useAuth outside AuthProvider");
  return ctx;
}

/** The signed-in user (only use inside protected routes). */
export function useUser(): User {
  const { user } = useAuth();
  if (!user) throw new Error("not signed in");
  return user;
}

