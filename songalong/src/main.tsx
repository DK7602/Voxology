import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { BrowserRouter } from "react-router-dom";
import "@fontsource/playfair-display/600.css";
import "@fontsource/playfair-display/700.css";
import "@fontsource/eb-garamond/400.css";
import "@fontsource/eb-garamond/500.css";
import "@fontsource/eb-garamond/600.css";
import "./styles/theme.css";
import "./styles/app.css";
import "./styles/song.css";
import { App } from "./App";
import { AuthProvider } from "./auth";
import { ToastProvider } from "./components/ui";
import { startInstallSupport } from "./install";
import { applyTheme, localLook } from "./theme";

// paint the right theme before the user's settings load
try {
  applyTheme(localLook());
} catch {
  applyTheme();
}

startInstallSupport();

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <BrowserRouter>
      <ToastProvider>
        <AuthProvider>
          <App />
        </AuthProvider>
      </ToastProvider>
    </BrowserRouter>
  </StrictMode>,
);
