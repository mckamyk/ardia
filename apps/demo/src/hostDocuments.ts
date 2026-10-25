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
    read: (name: string) => documents.get(name) ?? null,
    write: (name: string, contents: string) => {
      documents.set(name, contents);
      return invoke("writeDocument", [name, contents]);
    },
    remove: (name: string) => {
      documents.delete(name);
      return invoke("removeDocument", [name]);
    },
  };
}
