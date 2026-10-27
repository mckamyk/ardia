import { registerSessionCloseHooks } from "./sessionClose";
import type { BrowserState, HydrateData } from "../../browser/src/state/browser/store";
import type { BrowserWindow, Tab } from "../../browser/src/state/browser/types";
import type { ArdiaHost } from "./host";
import { reportHostError } from "./host.ts";
import { canonicalSessionValue, cloneSessionValue, equalSessionValue, mergeSession } from "./hostSessionMerge.ts";
import { sessionStatePatch } from "./hostSessionState.ts";
import { createWindowDeparture } from "./windowDeparture.ts";
import { parkWindowPins } from "../../browser/src/state/pins/parkedPins";
import { inPinnedContainer, newId, snapshotTab } from "../../browser/src/state/browser/model";
import { withPrivateWindow } from "./hostWindowProfiles.ts";

const CLOSED_WINDOW_LIMIT = 10;
const inClosingOrder = <T extends { closedAt: number }>(entries: T[]) => [...entries].sort((a, b) => a.closedAt - b.closedAt).slice(-CLOSED_WINDOW_LIMIT);

function withoutRestoredWindows(saved: HydrateData, now: number): HydrateData {
  const source = { tabs: saved.tabs ?? {}, groups: saved.groups ?? {}, splits: saved.splits ?? {}, history: {} };
  const windows = Object.values(saved.windows ?? {}).filter((w) => !w.incognito && w.kind !== "small");
  let parkedPins = saved.parkedPins ?? {};
  for (const w of windows) parkedPins = parkWindowPins(parkedPins, w, source);
  const closed = windows.map((w) => ({
    kind: "window" as const,
    id: newId("cw"),
    window: { profileId: w.profileId, sidebarOpen: w.sidebarOpen, frame: w.frame },
    tabs: w.tabIds.map((id) => source.tabs[id]).filter((t): t is Tab => !!t && !inPinnedContainer(source, t.id))
      .map((t) => ({ ...snapshotTab(t), active: Object.values(w.activeTabIds).includes(t.id) })),
    groups: [],
    closedAt: now,
  })).filter((c) => c.tabs.some((t) => t.url));
  return {
    ...saved, parkedPins, closedWindows: inClosingOrder([...(saved.closedWindows ?? []), ...closed]),
    windows: {}, windowOrder: [], tabs: {}, groups: {}, splits: {}, focusedWindowId: null,
  };
}

export function initialHostSession(host: ArdiaHost, saved: HydrateData | null | undefined, now = Date.now()): HydrateData {
  const { windowId, profileId } = host.bootstrap;
  if (!windowId || !profileId || /[\\/\0]/.test(windowId + profileId)) throw new Error("Invalid Ardia host bootstrap IDs");
  if (host.bootstrap.startup !== false && saved?.settings?.restoreSession === false) saved = withoutRestoredWindows(saved, now);
  if (saved?.closedWindows) saved = { ...saved, closedWindows: inClosingOrder(saved.closedWindows) };
  const privateSession = withPrivateWindow(host, saved ?? {});
  if (privateSession) return privateSession;
  const profile = { id: profileId, name: "Personal", color: "plum" as const, icon: null, createdAt: now };
  const profiles = { ...saved?.profiles };
  profiles[profileId] ??= profile;
  if (saved?.windows?.[windowId]?.tabIds.some((id) => saved.tabs?.[id])) return { ...saved, profiles, focusedWindowId: windowId };
  const tabId = `t_${crypto.randomUUID()}`;
  const tab: Tab = {
    id: tabId, windowId, profileId, url: "", title: "", favicon: null, pinned: false, muted: false, zoom: 1,
    customTitle: null, customIcon: null, pinnedUrl: null, navigation: null, openerId: null, createdAt: now, lastActiveAt: now,
  };
  const browserWindow: BrowserWindow = {
    id: windowId, profileId, incognito: false, tabIds: [tabId], activeTabIds: { [profileId]: tabId }, sidebarOpen: true, frame: null, createdAt: now,
  };
  return {
    ...saved, profiles, profileOrder: [...new Set([...(saved?.profileOrder ?? []), profileId])],
    windows: { ...saved?.windows, [windowId]: browserWindow },
    windowOrder: [...new Set([...(saved?.windowOrder ?? []), windowId])],
    tabs: { ...saved?.tabs, [tabId]: tab }, focusedWindowId: windowId,
  };
}

const publicProfile = (profile: string) => !profile.startsWith("incognito:");
const publicTabs = (entry: { tabs: { profileId: string }[] }) => entry.tabs.every((tab) => publicProfile(tab.profileId));
const publicTable = <T,>(table: Record<string, T>) => Object.fromEntries(Object.entries(table).filter(([id]) => publicProfile(id)));

export function snapshotHostSession(s: BrowserState): HydrateData {
  const windows = Object.fromEntries(Object.values(s.windows).filter((w) => !w.incognito && w.kind !== "small").map(({ chromeWindow: _, ...w }) => [w.id, w]));
  const tabs = Object.fromEntries(Object.values(s.tabs)
    .filter((t) => windows[t.windowId] && publicProfile(t.profileId) && !Object.values(s.peeks).some((p) => p.tabId === t.id))
    .map(({ navigation: _, adoptId: _a, wakeAdoptId: _w, pageIn, ...tab }) => [tab.id, { ...tab, navigation: null, ...(pageIn ? { unloaded: true as const } : {}) }]));
  for (const w of Object.values(windows)) {
    w.tabIds = w.tabIds.filter((id) => tabs[id]);
    w.activeTabIds = Object.fromEntries(Object.entries(w.activeTabIds).filter(([, id]) => tabs[id]));
  }
  return {
    profiles: publicTable(s.profiles), profileOrder: s.profileOrder.filter(publicProfile), orphanedProfileData: s.orphanedProfileData,
    windows, windowOrder: s.windowOrder.filter((id) => windows[id]), tabs,
    focusedWindowId: s.ui.focusOrder.find((id) => windows[id]) ?? null, lastProfileId: s.ui.lastProfileId,
    groups: Object.fromEntries(Object.entries(s.groups).filter(([, g]) => windows[g.windowId] && publicProfile(g.profileId)).map(([id, g]) => [id, { ...g, tabIds: g.tabIds.filter((key) => tabs[key]) }])),
    splits: Object.fromEntries(Object.entries(s.splits).filter(([, split]) => windows[split.windowId]).map(([id, split]) => [id, { ...split, tabIds: split.tabIds.filter((key) => tabs[key]) }])),
    closedTabs: s.closedTabs.filter((c) => publicProfile(c.tab.profileId)), closedWindows: s.closedWindows.filter(publicTabs),
    closedGroups: s.closedGroups.filter(publicTabs), deletedGroups: s.deletedGroups.filter(publicTabs), cleanedTabs: s.cleanedTabs.filter((c) => publicProfile(c.tab.profileId)),
    parkedPins: publicTable(s.parkedPins), pinChoices: publicTable(s.pinChoices), pinSync: publicTable(s.pinSync),
    settings: s.settings,
  };
}

type SessionPair = { base: HydrateData; session: HydrateData };
const sessionObject = (value: unknown): value is HydrateData => value !== null && typeof value === "object" && !Array.isArray(value);

export function createSessionWriter(host: ArdiaHost, initial: HydrateData = {}, onCanonical?: (base: HydrateData, session: HydrateData) => void) {
  let writes = Promise.resolve();
  let canonical = cloneSessionValue(initial);
  let observation = 0;
  let generation = 0;
  let pending: SessionPair | undefined;
  let latest: SessionPair | undefined;
  let issued: SessionPair | undefined;
  let issuedFailed = false;
  let failedWrite: Promise<void> | undefined;
  let dirty = false;
  let finalized = false;
  let finalWrite = Promise.resolve();
  const acceptCanonical = (session: HydrateData) => {
    canonical = cloneSessionValue(session);
    observation++;
  };
  const reconcile = (session: HydrateData, needsWrite: boolean) => {
    if (finalized) return;
    generation++;
    latest = { base: cloneSessionValue(canonical), session: cloneSessionValue(session) };
    pending = needsWrite ? latest : undefined;
    dirty = needsWrite;
  };
  const flush = () => {
    if (finalized) return finalWrite;
    if (!pending) return writes;
    const pair = pending;
    const version = generation;
    pending = undefined;
    const write = writes.catch((error) => { if (!finalized) reportHostError(error); }).then(async () => {
      if (finalized || version !== generation) return;
      issued = pair;
      issuedFailed = false;
      failedWrite = undefined;
      const observed = observation;
      try {
        const reply = await host.invoke("session.write", pair);
        if (finalized) return;
        if (observed === observation && sessionObject(reply)) {
          acceptCanonical(reply);
          onCanonical?.(pair.session, reply);
        }
        if (latest === pair) dirty = false;
      } catch (error) {
        if (issued === pair) {
          issuedFailed = true;
          failedWrite = write;
        }
        throw error;
      }
    });
    writes = write;
    return writes;
  };
  const drain = async () => {
    if (issuedFailed && failedWrite === writes) {
      if (dirty && !pending && latest) pending = latest;
      else if (!dirty && !pending) {
        writes = Promise.resolve();
        issuedFailed = false;
        failedWrite = undefined;
      }
    }
    for (let pass = 0; pass < 32; pass++) {
      const active = flush();
      await active;
      if (active === writes && !pending) return;
    }
    throw new Error("The session is still changing; save it again before closing");
  };
  const flushFinal = () => {
    if (finalized) return finalWrite;
    finalized = true;
    generation++;
    pending = undefined;
    if (!dirty || !latest || latest === issued && !issuedFailed) return finalWrite;
    issued = latest;
    try {
      finalWrite = Promise.resolve(host.invoke("session.write", latest)).then(() => {});
    } catch (error) {
      finalWrite = Promise.reject(error);
    }
    return finalWrite;
  };
  return { queue: (session: HydrateData) => reconcile(session, true), reconcile, acceptCanonical, flush, drain, flushFinal };
}

type SessionStore = {
  getState(): BrowserState;
  setState(patch: Partial<BrowserState>): void;
  subscribe(listener: (s: BrowserState) => void): () => void;
};

const withoutFocus = ({ focusedWindowId: _, lastProfileId: _last, ...session }: HydrateData) => session;
const sessionSources = (s: BrowserState) => [s.profiles, s.profileOrder, s.orphanedProfileData, s.windows, s.windowOrder, s.tabs, s.groups, s.splits, s.peeks, s.closedTabs, s.closedWindows, s.closedGroups, s.deletedGroups, s.cleanedTabs, s.parkedPins, s.pinChoices, s.pinSync, s.settings, s.ui.focusOrder, s.ui.lastProfileId];

export function startHostPersistence(host: ArdiaHost, store: SessionStore, initial: HydrateData = host.bootstrap.session ?? {}, snapshot: (state: BrowserState, canonical: HydrateData) => HydrateData = snapshotHostSession) {
  let canonical = initial;
  const snapshotOf = (state: BrowserState) => snapshot(state, canonical);
  const writer = createSessionWriter(host, initial, (base, session) => receive(base, session));
  let timer: ReturnType<typeof setTimeout> | undefined;
  let last = canonicalSessionValue(initial)!;
  let lastSources: unknown[] = [];
  let closing = false;
  const closeTokens = new Set<string>();
  let frozen: HydrateData | undefined;
  let applying = false;
  const flush = () => {
    clearTimeout(timer);
    timer = undefined;
    void writer.flush().catch((error) => { if (!closing) reportHostError(error); });
  };
  const queue = (s: BrowserState) => {
    if (closing || applying || closeTokens.size) return;
    const sources = sessionSources(s);
    if (sources.every((source, index) => source === lastSources[index])) return;
    lastSources = sources;
    const session = snapshotOf(s);
    const json = canonicalSessionValue(session)!;
    if (json === last) return;
    last = json;
    writer.queue(session);
    timer ??= setTimeout(flush, 300);
  };
  const receive = (base: HydrateData, session: HydrateData) => {
    if (closing) return;
    const state = store.getState();
    const local = frozen ?? snapshotOf(state);
    canonical = session;
    const merged = mergeSession(base, local, session);
    const runtime = { ...merged, ...(merged.closedWindows && { closedWindows: inClosingOrder(merged.closedWindows) }), settings: { ...state.settings, ...merged.settings } };
    const patch = sessionStatePatch(state, snapshotHostSession(state), runtime);
    applying = true;
    try {
      if (Object.keys(patch).length) store.setState(patch);
    } finally {
      applying = false;
    }
    const current = store.getState();
    const snapshot = snapshotOf(current);
    if (closeTokens.size) frozen = snapshot;
    const outstanding = !equalSessionValue(withoutFocus(snapshot), withoutFocus(session));
    lastSources = sessionSources(current);
    last = canonicalSessionValue(snapshot)!;
    writer.reconcile(snapshot, outstanding);
    if (outstanding && !closeTokens.size) timer ??= setTimeout(flush, 300);
    else {
      clearTimeout(timer);
      timer = undefined;
    }
  };
  const stopCloseHooks = registerSessionCloseHooks({
    quiesce: (token) => {
      if (!closeTokens.size) {
        queue(store.getState());
        frozen = snapshotOf(store.getState());
      }
      closeTokens.add(token);
      clearTimeout(timer);
      timer = undefined;
    },
    resume: (token) => {
      closeTokens.delete(token);
      if (closeTokens.size) return;
      frozen = undefined;
      queue(store.getState());
    },
  });
  queue(store.getState());
  const stop = store.subscribe(queue);
  const stopEvents = host.onEvent((module, event, payload) => {
    if (closing || module !== "ArdiaHost" || event !== "session" || !sessionObject(payload)) return;
    const update = payload as { base?: unknown; session?: unknown };
    if (!sessionObject(update.base) || !sessionObject(update.session)) return;
    writer.acceptCanonical(update.session);
    receive(update.base, update.session);
  });
  const final = () => {
    if (closing) return;
    if (closeTokens.size) { closing = true; clearTimeout(timer); timer = undefined; return; }
    queue(store.getState());
    closing = true;
    clearTimeout(timer);
    timer = undefined;
    void writer.flushFinal().catch(() => {});
  };
  const onVisibility = () => { if (document.hidden) flush(); };
  const stopClosing = host.onBeforeClose?.(final);
  if (!host.onBeforeClose) window.addEventListener("pagehide", final);
  document.addEventListener("visibilitychange", onVisibility);
  const dispose = () => {
    stopCloseHooks();
    stop();
    stopEvents();
    if (!closing) flush();
    stopClosing?.();
    window.removeEventListener("pagehide", final);
    document.removeEventListener("visibilitychange", onVisibility);
  };
  const departure = (windowId: string) => {
    const state = store.getState();
    return { base: snapshotOf(state), session: snapshotOf(createWindowDeparture(state, windowId)(state)) };
  };
  return Object.assign(dispose, { flush: () => { queue(store.getState()); clearTimeout(timer); timer = undefined; return writer.drain(); }, departure });
}
