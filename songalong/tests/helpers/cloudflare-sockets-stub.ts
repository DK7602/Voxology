// Stand-in for the `cloudflare:sockets` runtime module: tests plug in a fake server with
// setConnect(); without one, connecting fails.

type Connect = (address: { hostname: string; port: number } | string, options?: { secureTransport?: string }) => unknown;

const refuse: Connect = () => {
  throw new Error("No TCP sockets in tests");
};
let impl: Connect = refuse;

export function connect(address: Parameters<Connect>[0], options?: Parameters<Connect>[1]): unknown {
  return impl(address, options);
}

export function setConnect(fn: Connect | null): void {
  impl = fn ?? refuse;
}
