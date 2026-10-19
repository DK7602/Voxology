// Outgoing mail: Cloudflare Email Service binding, else a Gmail account (SMTP with an app
// password), else Resend, else the reset link goes to the log (development). Sending never
// throws; failures are logged.

import type { Env } from "./env";
import { errorText } from "./http";
import { sendSmtp } from "./smtp";

export interface MailMessage {
  to: string;
  subject: string;
  html: string;
  text: string;
}

/** `Name <addr@host>` or a bare address -> { email, name? } */
export function parseMailbox(from: string): { email: string; name?: string } {
  const m = /^\s*"?([^"<]*?)"?\s*<\s*([^>\s]+)\s*>\s*$/.exec(from);
  if (m) return m[1] ? { email: m[2], name: m[1] } : { email: m[2] };
  return { email: from.trim() };
}

export function canSendMail(env: Env): boolean {
  return Boolean(env.EMAIL || (env.GMAIL_USER && env.GMAIL_APP_PASSWORD) || env.RESEND_API_KEY);
}

/** Returns true when a transport accepted the message. */
export async function sendMail(env: Env, msg: MailMessage): Promise<boolean> {
  try {
    if (env.EMAIL) {
      const mailbox = parseMailbox(env.MAIL_FROM);
      const from = mailbox.name ? { email: mailbox.email, name: mailbox.name } : mailbox.email;
      await env.EMAIL.send({ from, to: msg.to, subject: msg.subject, html: msg.html, text: msg.text });
      return true;
    }
    if (env.GMAIL_USER && env.GMAIL_APP_PASSWORD) {
      // Google shows app passwords in groups of four ("abcd efgh ijkl mnop")
      const account = { host: "smtp.gmail.com", user: env.GMAIL_USER.trim(), pass: env.GMAIL_APP_PASSWORD.replace(/\s+/g, "") };
      const fromName = parseMailbox(env.MAIL_FROM).name || env.APP_NAME;
      await sendSmtp(account, { fromName, ...msg });
      return true;
    }
    if (env.RESEND_API_KEY) {
      const res = await fetch("https://api.resend.com/emails", {
        method: "POST",
        headers: { Authorization: `Bearer ${env.RESEND_API_KEY}`, "Content-Type": "application/json" },
        body: JSON.stringify({ from: env.MAIL_FROM, to: [msg.to], subject: msg.subject, html: msg.html, text: msg.text }),
      });
      if (!res.ok) {
        console.error(`Resend rejected the email (${res.status}):`, (await res.text()).slice(0, 500));
        return false;
      }
      return true;
    }
    return false;
  } catch (err) {
    console.error("Sending email failed:", errorText(err));
    return false;
  }
}

function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, (ch) => `&#${ch.charCodeAt(0)};`);
}

export function passwordResetEmail(appName: string, name: string, link: string): Omit<MailMessage, "to"> {
  const app = escapeHtml(appName);
  const greeting = name ? `Hi ${escapeHtml(name)},` : "Hi,";
  const href = escapeHtml(link);
  const serif = "Georgia, 'Times New Roman', serif";
  const html = `<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Reset your password</title></head>
<body style="margin:0;padding:0;background:#f7f1e3;">
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background:#f7f1e3;padding:32px 12px;">
<tr><td align="center">
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="max-width:520px;background:#fffdf7;border:1px solid #e6dcc3;border-top:4px solid #c9a227;border-radius:6px;">
<tr><td style="padding:28px 32px 8px;font-family:${serif};color:#0d4a2c;font-size:26px;font-weight:bold;">${app}</td></tr>
<tr><td style="padding:0 32px;"><div style="height:1px;background:#c9a227;opacity:.6;"></div></td></tr>
<tr><td style="padding:20px 32px 0;font-family:${serif};color:#2b2a26;font-size:17px;line-height:1.55;">
<p style="margin:0 0 14px;">${greeting}</p>
<p style="margin:0 0 22px;">Someone asked to reset the password for your ${app} account. Choose a new one with the button below. The link works once and expires in one hour.</p>
</td></tr>
<tr><td align="center" style="padding:0 32px 24px;">
<a href="${href}" style="display:inline-block;background:#0d4a2c;color:#fdf8ea;font-family:${serif};font-size:17px;text-decoration:none;padding:12px 28px;border-radius:4px;border:1px solid #c9a227;">Choose a new password</a>
</td></tr>
<tr><td style="padding:0 32px 28px;font-family:${serif};color:#6b6555;font-size:14px;line-height:1.5;">
<p style="margin:0 0 10px;">If the button doesn't work, paste this link into your browser:<br><a href="${href}" style="color:#0d4a2c;word-break:break-all;">${href}</a></p>
<p style="margin:0;">If you didn't ask for this, you can ignore this email; your password won't change.</p>
</td></tr>
</table>
</td></tr>
</table>
</body></html>`;
  const text = `${name ? `Hi ${name},` : "Hi,"}

Someone asked to reset the password for your ${appName} account. Choose a new one here (the link works once and expires in one hour):

${link}

If you didn't ask for this, you can ignore this email; your password won't change.

— ${appName}`;
  return { subject: `Reset your ${appName} password`, html, text };
}

export async function sendPasswordReset(env: Env, to: string, name: string, link: string): Promise<void> {
  if (!canSendMail(env)) {
    // Development: no mail transport configured.
    console.log(`[${env.APP_NAME}] password reset link for ${to}: ${link}`);
    return;
  }
  await sendMail(env, { to, ...passwordResetEmail(env.APP_NAME || "SongAlong", name, link) });
}
