import { afterEach, describe, expect, it, vi } from "vitest";
import { sendMail } from "../worker/email";
import { buildMessage } from "../worker/smtp";
import { setConnect } from "./helpers/cloudflare-sockets-stub";
import { Client, makeEnv } from "./helpers/workerEnv";

interface Dialed {
  port: number;
  secureTransport?: string;
}

/** A pretend smtp.gmail.com: takes one message from `user` with `pass`, and records the talk. */
function fakeGmail({ pass = "abcdefghijklmnop", unreachable = [] as number[] } = {}) {
  const dialed: Dialed[] = [];
  const commands: string[] = [];
  const messages: string[] = [];
  let tls = false;

  setConnect((address, options) => {
    const { port } = address as { port: number };
    dialed.push({ port, secureTransport: options?.secureTransport });
    if (unreachable.includes(port)) {
      const refused = () => new Error("connection refused");
      return {
        readable: new ReadableStream({ start: (c) => c.error(refused()) }),
        writable: new WritableStream({ start: (c) => c.error(refused()) }),
        opened: Promise.reject(refused()),
        closed: Promise.reject(refused()),
        close: async () => undefined,
      };
    }
    tls = options?.secureTransport === "on";
    const toClient = new TransformStream<Uint8Array, Uint8Array>();
    const toServer = new TransformStream<Uint8Array, Uint8Array>();
    const out = toClient.writable.getWriter();
    const say = (s: string) => out.write(new TextEncoder().encode(s + "\r\n"));
    void (async () => {
      const reader = toServer.readable.getReader();
      const decoder = new TextDecoder();
      let buffer = "";
      let data: string[] | null = null;
      await say("220 smtp.gmail.com ESMTP ready");
      for (;;) {
        const { value, done } = await reader.read();
        if (done) return;
        buffer += decoder.decode(value, { stream: true });
        let end: number;
        while ((end = buffer.indexOf("\r\n")) >= 0) {
          const line = buffer.slice(0, end);
          buffer = buffer.slice(end + 2);
          if (data) {
            if (line === ".") {
              messages.push(data.join("\r\n"));
              data = null;
              await say("250 2.0.0 OK queued");
            } else data.push(line);
            continue;
          }
          commands.push(line);
          const verb = line.split(" ")[0];
          if (verb === "EHLO") await say("250-smtp.gmail.com at your service\r\n250-AUTH LOGIN PLAIN\r\n250 SMTPUTF8");
          else if (verb === "STARTTLS") {
            await say("220 2.0.0 Ready to start TLS");
            tls = true;
          } else if (verb === "AUTH") {
            const [, user, pw] = atob(line.split(" ")[2]).split("\0");
            if (!tls) await say("530 5.7.0 Must issue a STARTTLS command first");
            else if (user === "songalong.app@gmail.com" && pw === pass) await say("235 2.7.0 Accepted");
            else await say("535-5.7.8 Username and Password not accepted.\r\n535 5.7.8 https://support.google.com/mail/?p=BadCredentials");
          } else if (verb === "MAIL" || verb === "RCPT") await say("250 2.1.0 OK");
          else if (verb === "DATA") {
            data = [];
            await say("354 Go ahead");
          } else if (verb === "QUIT") {
            await say("221 2.0.0 closing connection");
            await out.close();
            return;
          } else await say("502 5.5.1 Unrecognized command");
        }
      }
    })();
    const socket = {
      readable: toClient.readable,
      writable: toServer.writable,
      opened: Promise.resolve({}),
      closed: new Promise(() => undefined),
      close: async () => undefined,
      startTls: () => socket,
    };
    return socket;
  });
  return { dialed, commands, messages };
}

/** The headers and the decoded plain-text and HTML parts of a sent message. */
function readMessage(raw: string) {
  const unstuffed = raw.replace(/^\.\./gm, ".");
  const [head] = unstuffed.split("\r\n\r\n");
  const header = (name: string) => new RegExp(`^${name}: (.*)$`, "m").exec(head)?.[1];
  const boundary = /boundary="([^"]+)"/.exec(head)![1];
  const part = (type: string) => {
    const p = unstuffed.split(`--${boundary}`).find((s) => s.includes(`Content-Type: ${type}`))!;
    const bytes = Uint8Array.from(atob(p.split("\r\n\r\n")[1].replace(/\s+/g, "")), (c) => c.charCodeAt(0));
    return new TextDecoder().decode(bytes);
  };
  return { header, text: part("text/plain"), html: part("text/html") };
}

const gmailEnv = { EMAIL: undefined, GMAIL_USER: "songalong.app@gmail.com", GMAIL_APP_PASSWORD: "abcd efgh ijkl mnop" };

afterEach(() => {
  setConnect(null);
  vi.restoreAllMocks();
});

describe("password-reset email through Gmail", () => {
  it("signs in on port 465 and sends the reset link from the Gmail address", async () => {
    const server = fakeGmail();
    const { env } = makeEnv(gmailEnv);
    const user = new Client(env);
    await user.call("POST", "/api/auth/register", { name: "Dave", email: "dave@example.com", password: "correct horse battery" });
    const anon = new Client(env);
    expect((await anon.call("POST", "/api/auth/forgot", { email: "dave@example.com" })).body).toEqual({ ok: true });
    await vi.waitFor(() => expect(server.messages).toHaveLength(1));

    expect(server.dialed).toEqual([{ port: 465, secureTransport: "on" }]);
    expect(server.commands).toEqual([
      "EHLO songalong",
      `AUTH PLAIN ${btoa("\0songalong.app@gmail.com\0abcdefghijklmnop")}`,
      "MAIL FROM:<songalong.app@gmail.com>",
      "RCPT TO:<dave@example.com>",
      "DATA",
      "QUIT",
    ]);
    const mail = readMessage(server.messages[0]);
    expect(mail.header("From")).toBe(`"SongAlong" <songalong.app@gmail.com>`);
    expect(mail.header("To")).toBe("<dave@example.com>");
    expect(mail.header("Subject")).toBe("Reset your SongAlong password");
    expect(mail.text).toMatch(/^Hi Dave,/);
    const token = /reset-password\?token=([A-Za-z0-9_-]{43})/.exec(mail.text)![1];
    expect(mail.html).toContain(`token=${token}`);
    expect((await anon.call("POST", "/api/auth/reset", { token, password: "brand new password" })).status).toBe(200);
  });

  it("uses STARTTLS on port 587 when 465 can't be reached", async () => {
    vi.spyOn(console, "warn").mockImplementation(() => undefined);
    const server = fakeGmail({ unreachable: [465] });
    const { env } = makeEnv(gmailEnv);
    const sent = await sendMail(env, { to: "dave@example.com", subject: "Hi", text: "Hello", html: "<p>Hello</p>" });
    expect(sent).toBe(true);
    expect(server.dialed).toEqual([
      { port: 465, secureTransport: "on" },
      { port: 587, secureTransport: "starttls" },
    ]);
    expect(server.commands.slice(0, 3)).toEqual(["EHLO songalong", "STARTTLS", "EHLO songalong"]);
    expect(server.messages).toHaveLength(1);
  });

  it("reports a wrong app password without sending", async () => {
    const errors = vi.spyOn(console, "error").mockImplementation(() => undefined);
    const server = fakeGmail({ pass: "something-else00" });
    const { env } = makeEnv(gmailEnv);
    expect(await sendMail(env, { to: "dave@example.com", subject: "Hi", text: "Hello", html: "<p>Hello</p>" })).toBe(false);
    expect(server.commands.some((c) => c.startsWith("MAIL"))).toBe(false);
    expect(String(errors.mock.calls[0])).toContain("535");
  });
});

describe("buildMessage", () => {
  it("encodes non-ASCII headers and bodies, and dot-stuffs", () => {
    const raw = buildMessage("me@gmail.com", { fromName: "Chansons Café", to: "you@example.com", subject: "Réglages ✓", text: ".hidden?\nnon", html: "<p>é</p>" });
    expect(raw.endsWith("\r\n.\r\n")).toBe(true);
    const mail = readMessage(raw.slice(0, -3));
    expect(mail.header("From")).toBe(`=?UTF-8?B?${btoa(String.fromCharCode(...new TextEncoder().encode("Chansons Café")))}?= <me@gmail.com>`);
    expect(mail.header("Subject")).toMatch(/^=\?UTF-8\?B\?/);
    expect(mail.text).toBe(".hidden?\nnon");
    expect(mail.html).toBe("<p>é</p>");
    expect(() => buildMessage("me@gmail.com", { to: "a@b.c\r\nBcc: x@y.z", subject: "s", text: "t", html: "h" })).toThrow();
  });
});
