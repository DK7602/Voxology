// A Map-backed stand-in for the parts of R2Bucket the Worker uses: put / get (with range) /
// head / delete / list.

interface Stored {
  data: Uint8Array;
  contentType?: string;
  uploaded: Date;
  etag: string;
}

type Range = { offset?: number; length?: number; suffix?: number };

async function toBytes(value: unknown): Promise<Uint8Array> {
  if (value === null || value === undefined) return new Uint8Array();
  if (typeof value === "string") return new TextEncoder().encode(value);
  if (value instanceof Uint8Array) return value.slice();
  if (value instanceof ArrayBuffer) return new Uint8Array(value.slice(0));
  if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength));
  if (value instanceof Blob) return new Uint8Array(await value.arrayBuffer());
  if (value instanceof ReadableStream) return new Uint8Array(await new Response(value).arrayBuffer());
  throw new TypeError("Unsupported R2 put value");
}

export class FakeR2 {
  readonly objects = new Map<string, Stored>();
  private counter = 0;

  private describe(key: string, s: Stored) {
    return {
      key,
      size: s.data.byteLength,
      etag: s.etag,
      httpEtag: `"${s.etag}"`,
      uploaded: s.uploaded,
      httpMetadata: { contentType: s.contentType },
      customMetadata: {},
      writeHttpMetadata(headers: Headers) {
        if (s.contentType) headers.set("Content-Type", s.contentType);
      },
    };
  }

  async put(key: string, value: unknown, options?: { httpMetadata?: { contentType?: string } }) {
    const stored: Stored = {
      data: await toBytes(value),
      contentType: options?.httpMetadata?.contentType,
      uploaded: new Date(),
      etag: `etag-${++this.counter}`,
    };
    this.objects.set(key, stored);
    return this.describe(key, stored);
  }

  async head(key: string) {
    const s = this.objects.get(key);
    return s ? this.describe(key, s) : null;
  }

  async get(key: string, options?: { range?: Range }) {
    const s = this.objects.get(key);
    if (!s) return null;
    const size = s.data.byteLength;
    let offset = 0;
    let length = size;
    const r = options?.range;
    if (r) {
      if (r.suffix !== undefined) {
        length = Math.min(r.suffix, size);
        offset = size - length;
      } else {
        offset = r.offset ?? 0;
        length = Math.min(r.length ?? size - offset, size - offset);
      }
    }
    const bytes = s.data.slice(offset, offset + length);
    return {
      ...this.describe(key, s),
      range: { offset, length },
      body: new Blob([bytes]).stream(),
      bodyUsed: false,
      arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength),
      text: async () => new TextDecoder().decode(bytes),
    };
  }

  async delete(keys: string | string[]) {
    for (const k of Array.isArray(keys) ? keys : [keys]) this.objects.delete(k);
  }

  async list(options?: { prefix?: string; limit?: number; cursor?: string }) {
    const prefix = options?.prefix ?? "";
    const limit = options?.limit ?? 1000;
    const start = options?.cursor ? Number(options.cursor) : 0;
    const keys = [...this.objects.keys()].filter((k) => k.startsWith(prefix)).sort();
    const page = keys.slice(start, start + limit);
    const truncated = start + limit < keys.length;
    return {
      objects: page.map((k) => this.describe(k, this.objects.get(k)!)),
      truncated,
      cursor: truncated ? String(start + limit) : undefined,
      delimitedPrefixes: [],
    };
  }
}
