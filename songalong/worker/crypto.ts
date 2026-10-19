// Tokens, ids and password hashing on WebCrypto (works in Workers and Node).

const encoder = new TextEncoder();

/** Workers' PBKDF2 refuses more than 100 000 iterations. */
export const PBKDF2_ITERATIONS = 100_000;
const SALT_BYTES = 16;
const HASH_BYTES = 32;

export function randomBytes(n: number): Uint8Array<ArrayBuffer> {
  return crypto.getRandomValues(new Uint8Array(n));
}

export function base64(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s);
}

export function fromBase64(s: string): Uint8Array<ArrayBuffer> {
  const bin = atob(s);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

export function base64url(bytes: Uint8Array): string {
  return base64(bytes).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

/** A URL-safe random id: 12 bytes -> 16 characters. */
export function randomId(bytes = 12): string {
  return base64url(randomBytes(bytes));
}

/** A 256-bit secret for session cookies and reset links (43 characters, base64url). */
export function randomToken(): string {
  return base64url(randomBytes(32));
}

export async function sha256Hex(s: string): Promise<string> {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", encoder.encode(s)));
  let hex = "";
  for (const b of digest) hex += b.toString(16).padStart(2, "0");
  return hex;
}

/** Compares without an early exit, so timing does not reveal how many leading bytes match. */
export function timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a[i] ^ b[i];
  return diff === 0;
}

async function pbkdf2(password: string, salt: Uint8Array<ArrayBuffer>, iterations: number, bytes: number) {
  const key = await crypto.subtle.importKey("raw", encoder.encode(password), "PBKDF2", false, ["deriveBits"]);
  const bits = await crypto.subtle.deriveBits({ name: "PBKDF2", hash: "SHA-256", salt, iterations }, key, bytes * 8);
  return new Uint8Array(bits);
}

/** -> `pbkdf2-sha256$100000$<salt b64>$<hash b64>` */
export async function hashPassword(password: string): Promise<string> {
  const salt = randomBytes(SALT_BYTES);
  const hash = await pbkdf2(password, salt, PBKDF2_ITERATIONS, HASH_BYTES);
  return `pbkdf2-sha256$${PBKDF2_ITERATIONS}$${base64(salt)}$${base64(hash)}`;
}

// Checked when the account doesn't exist, so a wrong email takes as long as a wrong password.
const DUMMY_HASH = `pbkdf2-sha256$${PBKDF2_ITERATIONS}$${base64(new Uint8Array(SALT_BYTES))}$${base64(new Uint8Array(HASH_BYTES))}`;

export async function verifyPassword(password: string, stored: string | null): Promise<boolean> {
  const parts = (stored ?? DUMMY_HASH).split("$");
  if (parts.length !== 4 || parts[0] !== "pbkdf2-sha256") return false;
  const iterations = Number(parts[1]);
  if (!Number.isInteger(iterations) || iterations < 1 || iterations > PBKDF2_ITERATIONS) return false;
  let salt: Uint8Array<ArrayBuffer>;
  let expected: Uint8Array<ArrayBuffer>;
  try {
    salt = fromBase64(parts[2]);
    expected = fromBase64(parts[3]);
  } catch {
    return false;
  }
  if (expected.length === 0) return false;
  const actual = await pbkdf2(password, salt, iterations, expected.length);
  return timingSafeEqual(actual, expected) && stored !== null;
}
