import { readCanonicalLiveDocument } from "./canonicalLiveDocument";
import type { ArdiaHost } from "./host";
import { invokeHost } from "./host.ts";

export function createHostDocuments(host: ArdiaHost) {
  const documents = new Map(Object.entries(host.bootstrap.documents ?? {}));
  let writes = Promise.resolve();
  const invoke = (method: string, args: unknown[]) => {
    const next = writes.catch(() => {}).then(() => invokeHost<void>(`ArdiaShell.${method}`, { args }, host));
    writes = next;
    return next;
  };
  return {
    read: (name: string) => name === "live.canonical.json" ? null : name === "live.json" ? readCanonicalLiveDocument(host) : documents.get(name) ?? null,
    write: (name: string, contents: string) => {
      if (name === "live.json" || name === "live.canonical.json") return Promise.reject(Object.assign(new Error("Canonical Live documents require typed operations"), { code: "ERR_DOCUMENT_RESERVED" }));
      return invoke("writeDocument", [name, contents]).then(() => { documents.set(name, contents); });
    },
    remove: (name: string) => {
      if (name === "live.json" || name === "live.canonical.json") return Promise.reject(Object.assign(new Error("Canonical Live documents cannot be removed generically"), { code: "ERR_DOCUMENT_RESERVED" }));
      return invoke("removeDocument", [name]).then(() => { documents.delete(name); });
    },
  };
}
