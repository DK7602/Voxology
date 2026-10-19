// A small SMTP client over Workers TCP sockets, enough to send one message through an account
// such as Gmail (smtp.gmail.com with an app password). Tries implicit TLS on 465 first, then
// STARTTLS on 587 if 465 can't be reached.

import { connect } from "cloudflare:sockets";
import { errorText } from "./http";

export interface SmtpAccount {
  host: string;
  user: string;
  pass: string;
}

export interface SmtpMessage {
  /** `Name <addr>` display name for the From header; the address is always the account's. */
  fromName?: string;
  to: string;
  subject: string;
  text: string;
  html: string;
}

const GREETING_MS = 10_000;
const TIMEOUT_MS = 20_000;

export class SmtpError extends Error {}

/** The server couldn't be reached at all (so another port may still work). */
class Unreachable extends SmtpError {}

interface Reply {
  code: number;
  text: string;
}

const encoder = new TextEncoder();
const decoder = new TextDecoder();

function base64(s: string): string {
  let bin = "";
  for (const b of encoder.encode(s)) bin += String.fromCharCode(b);
  return btoa(bin);
}

/** Header-safe: no line breaks, and RFC 2047 words when it isn't plain ASCII. */
function headerText(s: string): string {
  const flat = s.replace(/[\r\n]+/g, " ").trim();
  return /^[\x20-\x7e]*$/.test(flat) ? flat : `=?UTF-8?B?${base64(flat)}?=`;
}

function bareAddress(s: string): string {
  const a = s.replace(/[\r\n<>\s]/g, "");
  if (!/^[^@]+@[^@]+$/.test(a)) throw new SmtpError(`Not an email address: ${a}`);
  return a;
}

function wrap76(b64: string): string {
  return b64.replace(/.{1,76}/g, "$&\r\n");
}

/** The message as sent after DATA (headers, plain and HTML parts), dot-stuffed and ended. */
export function buildMessage(from: string, msg: SmtpMessage): string {
  const boundary = `sa-${crypto.randomUUID()}`;
  const name = msg.fromName ? headerText(msg.fromName.replace(/["\\]/g, "")) : "";
  const domain = from.split("@")[1];
  const lines = [
    // an encoded word can't sit inside quotes
    `From: ${!name ? "" : name.startsWith("=?") ? `${name} ` : `"${name}" `}<${from}>`,
    `To: <${bareAddress(msg.to)}>`,
    `Subject: ${headerText(msg.subject)}`,
    `Date: ${new Date().toUTCString()}`,
    `Message-ID: <${crypto.randomUUID()}@${domain}>`,
    "MIME-Version: 1.0",
    `Content-Type: multipart/alternative; boundary="${boundary}"`,
    "",
    `--${boundary}`,
    "Content-Type: text/plain; charset=utf-8",
    "Content-Transfer-Encoding: base64",
    "",
    wrap76(base64(msg.text)),
    `--${boundary}`,
    "Content-Type: text/html; charset=utf-8",
    "Content-Transfer-Encoding: base64",
    "",
    wrap76(base64(msg.html)),
    `--${boundary}--`,
    "",
  ];
  return lines.join("\r\n").replace(/^\./gm, "..") + ".\r\n";
}

/** One SMTP conversation: reads replies line by line, writes commands. */
class Session {
  private reader: ReadableStreamDefaultReader<Uint8Array>;
  private writer: WritableStreamDefaultWriter<Uint8Array>;
  private buffer = "";

  constructor(private socket: Socket) {
    this.reader = socket.readable.getReader();
    this.writer = socket.writable.getWriter();
  }

  private async line(): Promise<string> {
    for (;;) {
      const end = this.buffer.indexOf("\r\n");
      if (end >= 0) {
        const line = this.buffer.slice(0, end);
        this.buffer = this.buffer.slice(end + 2);
        return line;
      }
      const { value, done } = await this.reader.read();
      if (done) throw new SmtpError("The mail server closed the connection");
      this.buffer += decoder.decode(value, { stream: true });
    }
  }

  /** A reply, joining the lines of a multi-line one ("250-…" … "250 …"). */
  async reply(): Promise<Reply> {
    const parts: string[] = [];
    for (;;) {
      const line = await this.line();
      parts.push(line.slice(4));
      if (line[3] !== "-") return { code: Number(line.slice(0, 3)), text: parts.join("\n") };
    }
  }

  async expect(code: number, what: string): Promise<Reply> {
    const r = await this.reply();
    if (r.code !== code) throw new SmtpError(`${what}: ${r.code} ${r.text}`);
    return r;
  }

  async send(command: string, code: number, what = command.split(" ")[0]): Promise<Reply> {
    await this.writer.write(encoder.encode(command + "\r\n"));
    return this.expect(code, what);
  }

  async write(raw: string): Promise<void> {
    await this.writer.write(encoder.encode(raw));
  }

  /** Switches the same connection to TLS (after "STARTTLS" was accepted). */
  upgrade(): void {
    this.reader.releaseLock();
    this.writer.releaseLock();
    this.socket = this.socket.startTls();
    this.reader = this.socket.readable.getReader();
    this.writer = this.socket.writable.getWriter();
    this.buffer = "";
  }

  async close(): Promise<void> {
    try {
      await this.socket.close();
    } catch {
      // already closed
    }
  }
}

/** Everything after the server's greeting. */
async function converse(session: Session, account: SmtpAccount, msg: SmtpMessage, starttls: boolean): Promise<void> {
  await session.send("EHLO songalong", 250);
  if (starttls) {
    await session.send("STARTTLS", 220);
    session.upgrade();
    await session.send("EHLO songalong", 250);
  }
  const user = bareAddress(account.user);
  await session.send(`AUTH PLAIN ${base64(`\0${user}\0${account.pass}`)}`, 235, "Sign-in");
  await session.send(`MAIL FROM:<${user}>`, 250);
  await session.send(`RCPT TO:<${bareAddress(msg.to)}>`, 250);
  await session.send("DATA", 354);
  await session.write(buildMessage(user, msg));
  await session.expect(250, "Message");
  try {
    await session.send("QUIT", 221);
  } catch {
    // the message is already accepted
  }
}

async function within<T>(ms: number, task: Promise<T>, what: string): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const late = new Promise<never>((_, reject) => {
    timer = setTimeout(() => reject(new SmtpError(what)), ms);
  });
  try {
    return await Promise.race([task, late]);
  } finally {
    if (timer !== undefined) clearTimeout(timer);
  }
}

async function attempt(account: SmtpAccount, msg: SmtpMessage, port: number): Promise<void> {
  const starttls = port !== 465;
  const where = `${account.host}:${port}`;
  const socket = connect({ hostname: account.host, port }, { secureTransport: starttls ? "starttls" : "on", allowHalfOpen: false });
  socket.closed.catch(() => undefined); // a failed connection also rejects this
  const session = new Session(socket);
  try {
    // connected and greeted, or the other port gets a try
    try {
      await within(GREETING_MS, socket.opened.then(() => session.expect(220, "Greeting")), "no greeting");
    } catch (err) {
      throw new Unreachable(`Couldn't reach ${where} (${errorText(err)})`);
    }
    await within(TIMEOUT_MS, converse(session, account, msg, starttls), `${where} stopped answering`);
  } finally {
    await session.close();
  }
}

/** Sends one message; throws SmtpError with the server's reply when it is refused. */
export async function sendSmtp(account: SmtpAccount, msg: SmtpMessage): Promise<void> {
  try {
    await attempt(account, msg, 465);
  } catch (err) {
    if (!(err instanceof Unreachable)) throw err;
    console.warn(`${err.message}; trying port 587`);
    await attempt(account, msg, 587);
  }
}
