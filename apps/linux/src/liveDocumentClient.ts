import type { ArdiaHost } from "./host";
import { acceptCanonicalLiveDocument } from "./canonicalLiveDocument";
import { callHost } from "./host";
import type { LivePersistenceIntent, LiveProjection } from "../../browser/src/platform/livePersistence";
import { captureLiveIntent, projectLiveIntent } from "./liveDocumentIntent";
import { documentError, type DocumentSnapshot, type DocumentOperation, type DocumentReply, type DocumentChange } from "./liveDocumentTypes";

type Pending = { operations: DocumentOperation[]; requestId?: string; observedRevision?: number; epoch?: string };
const initial = (): DocumentSnapshot => ({ document: "live.json", scope: "", clientId: "", epoch: "", revision: -1, watermark: 0, payload: {}, incarnations: {}, generations: {} });

export function createLiveDocumentClient(host: ArdiaHost) {
  const invoke = (verb: string, args: Record<string, unknown>) => callHost(`documents.${verb}`, host.bootstrap.profileId, JSON.stringify(args), host).then((reply) => JSON.parse(reply));
  let canonical = initial();
  const pending: Pending[] = [];
  let loading = true;
  let events: DocumentChange[] = [];
  let sequence = 0;
  let watermark = 0;
  const closeTokens = new Set<string>();
  let failure: unknown;
  let captureFailure: unknown;
  let connection = 0;
  let apply: (payload: LiveProjection) => void = () => {};
  let active: Promise<void> | undefined;
  let ready: Promise<void>;
  const publish = () => apply(pending.reduce((payload, batch) => projectLiveIntent(payload, batch.operations), canonical.payload));
  const accept = (snapshot: DocumentSnapshot) => {
    if (snapshot.document !== "live.json" || !Number.isSafeInteger(snapshot.revision) || snapshot.revision < 0 || !Number.isSafeInteger(snapshot.watermark) || snapshot.watermark < 0 || snapshot.watermark > 2147483000) throw documentError("ERR_BAD_REPLY", "Invalid canonical document snapshot");
    if (canonical.scope && (snapshot.scope !== canonical.scope || snapshot.clientId !== canonical.clientId)) throw documentError("ERR_DOCUMENT_SCOPE", "Document authority changed");
    if (snapshot.epoch !== canonical.epoch && canonical.epoch) throw documentError("ERR_DOCUMENT_EPOCH", "Document authority requires reconnect");
    if (snapshot.revision >= canonical.revision) {
      canonical = snapshot;
      acceptCanonicalLiveDocument(host, snapshot.payload);
      watermark = Math.max(watermark, snapshot.watermark ?? 0);
      sequence = Math.max(sequence, watermark);
    }
  };
  const acknowledge = (clientId: string, requestId: string) => {
    if (clientId !== canonical.clientId) return;
    const at = pending.findIndex((batch) => batch.requestId === requestId);
    if (at < 0) return;
    watermark = Math.max(watermark, Number(requestId.slice(requestId.lastIndexOf(":") + 1)));
    pending.splice(at, 1);
  };
  const stop = host.onEvent((module, event, payload) => {
    if (module !== "ArdiaDocuments" || event !== "documents") return;
    const envelope = payload as { name: string; data: DocumentChange };
    if (envelope?.name !== "documents.changed") return;
    const change = envelope.data;
    if (loading) { events.push(change); return; }
    try { accept(change.snapshot); acknowledge(change.clientId, change.requestId); publish(); }
    catch (error) { failure = error; }
  });
  const reconnect = () => {
    const generation = ++connection;
    loading = true;
    ready = invoke("watch", { document: "live.json" }).then((reply) => {
      if (generation !== connection) return;
      const snapshot = reply as DocumentSnapshot;
      if (canonical.epoch && canonical.epoch !== snapshot.epoch) {
        if (snapshot.clientId !== canonical.clientId || snapshot.scope !== canonical.scope) throw documentError("ERR_DOCUMENT_SCOPE", "Document client requires a new renderer");
        for (let index = pending.length - 1; index >= 0; index--) {
          const batch = pending[index]!;
          const at = batch.requestId ? Number(batch.requestId.slice(batch.requestId.lastIndexOf(":") + 1)) : undefined;
          if (at !== undefined && at <= snapshot.watermark) pending.splice(index, 1);
          else { delete batch.epoch; delete batch.observedRevision; }
        }
        canonical = initial();
      }
      accept(snapshot);
      loading = false;
      for (const change of events) {
        if (change.snapshot.epoch !== canonical.epoch || change.snapshot.clientId !== canonical.clientId) continue;
        accept(change.snapshot); acknowledge(change.clientId, change.requestId);
      }
      events = [];
      publish();
    });
    void ready.catch((error) => { failure = error; });
    return ready;
  };
  const recondition = (operations: DocumentOperation[], snapshot: DocumentSnapshot): DocumentOperation[] => {
    const payload = snapshot.payload as Record<string, any>;
    return operations.map((op) => {
      if ("incarnation" in op && op.type !== "create" && snapshot.incarnations[op.id!] !== op.incarnation) throw documentError("ERR_DOCUMENT_DELETED", "The edited folder no longer exists");
      if (op.type === "refresh" || op.type === "place") throw documentError("ERR_DOCUMENT_CONFLICT", "The producer must retry from current state");
      if (op.type === "field") {
        const target = op.target === "folder" ? payload.folders?.[op.id!] : op.target === "account" ? payload.accounts : op.target === "meetingGroup" ? payload.meetingGroups : op.target === "expandedStack" ? payload.expandedStacks : payload[op.target];
        const expected = op.field.startsWith("filters.") ? target?.filters?.[op.field.slice(8)] : target?.[op.field === "value" ? op.id! : op.field];
        return { ...op, expected: expected ?? null };
      }
      if (op.type === "entry") {
        const entry = payload[op.target]?.[op.id]?.find((entry: any) => (op.target === "completed" ? entry.item.id : entry.id) === op.key);
        return { ...op, expected: entry ?? null };
      }
      return op;
    });
  };
  const drain = () => {
    if (active) return active;
    const run = async () => {
      await ready;
      let conflicts = 0;
      while (pending.length) {
        const batch = pending[0]!;
        if (!batch.requestId && sequence >= 2147483000) throw documentError("ERR_DOCUMENT_LIMIT", "Document request sequence exhausted");
        batch.requestId ??= `${canonical.clientId}:${++sequence}`;
        batch.epoch ??= canonical.epoch;
        batch.observedRevision ??= canonical.revision;
        const reply = await invoke("commit", { document: "live.json", epoch: batch.epoch, requestId: batch.requestId, observedRevision: batch.observedRevision, operations: batch.operations }) as DocumentReply;
        accept(reply.snapshot);
        if (reply.status === "conflict") {
          if (++conflicts > 8) throw documentError("ERR_DOCUMENT_CONFLICT", "Document kept changing during reconciliation");
          batch.operations = recondition(batch.operations, canonical);
          batch.observedRevision = canonical.revision;
          publish();
          continue;
        }
        acknowledge(canonical.clientId, reply.requestId);
        publish();
      }
      failure = undefined;
    };
    active = run().catch((error) => { failure = error; throw error; }).finally(() => {
      active = undefined;
      if (pending.length && !failure) void drain().catch(() => {});
    });
    return active;
  };
  reconnect();
  return {
    start: (callback: typeof apply) => { apply = callback; if (!loading) publish(); },
    changed: (before: LiveProjection, after: LiveProjection, intent?: LivePersistenceIntent) => {
      if (closeTokens.size) { publish(); return; }
      try {
        const operations = captureLiveIntent(before, after, canonical, intent);
        if (operations.length) { pending.push({ operations }); void drain().catch(() => {}); }
      } catch (error) { captureFailure = failure = error; throw error; }
    },
    beginRefresh: async (id: string) => {
      if (closeTokens.size) throw documentError("ERR_DOCUMENT_CLOSING", "Document producers are quiesced");
      await drain();
      const incarnation = canonical.incarnations[id];
      if (!incarnation) throw documentError("ERR_DOCUMENT_DELETED", "Folder no longer exists");
      pending.push({ operations: [{ type: "claim", id, incarnation }] });
      await drain();
      return { generation: canonical.generations[id]!, owner: canonical.clientId };
    },
    flush: async () => {
      do { await drain(); } while (pending.length || active);
      if (captureFailure || failure) throw captureFailure ?? failure;
      await invoke("flush", { document: "live.json", epoch: canonical.epoch, watermark });
      return watermark;
    },
    quiesce: (token: string) => { closeTokens.add(token); },
    resume: (token: string) => { closeTokens.delete(token); },
    reconnect,
    retry: drain,
    discardPending: () => { if (active) throw documentError("ERR_DOCUMENT_BUSY", "Await accepted work before discarding pending intent"); pending.splice(0); sequence = watermark; captureFailure = failure = undefined; publish(); },
    state: () => ({ canonical, dirty: pending.length > 0 || !!captureFailure, failure, watermark, closing: closeTokens.size > 0 }),
    dispose: () => { stop(); return invoke("unwatch", { document: "live.json" }); },
  };
}
