import { isLinuxUtility } from "./session";

export type WindowCloseRequest = { windowId: string; rendererGeneration: string; closeGeneration: string };
export type CloseBarrierHooks = {
  quiesce(request: WindowCloseRequest): void;
  drain(): Promise<number>;
  acknowledge(request: WindowCloseRequest, watermark: number): Promise<void>;
  reject(request: WindowCloseRequest): Promise<void>;
  resume(request: WindowCloseRequest): void;
};

export function createWindowCloseBarrier(hooks: CloseBarrierHooks) {
  const pending = new Map<string, Promise<void>>();
  return (request: WindowCloseRequest): Promise<void> => {
    const key = JSON.stringify([request.windowId, request.rendererGeneration, request.closeGeneration]);
    const existing = pending.get(key);
    if (existing) return existing;
    const work = Promise.resolve().then(() => hooks.quiesce(request)).then(() => hooks.drain()).then((watermark) => hooks.acknowledge(request, watermark)).catch(async (error) => {
      await hooks.reject(request);
      hooks.resume(request);
      throw error;
    });
    pending.set(key, work);
    const failed = () => { if (pending.get(key) === work) pending.delete(key); };
    void work.catch(failed);
    return work;
  };
}

export function createUtilityWindowCloser(save: () => Promise<void>, close: (id: string) => Promise<void>) {
  const pending = new Map<string, Promise<void>>();
  return (id: string): Promise<void> => {
    if (!isLinuxUtility(id)) return close(id);
    const current = pending.get(id);
    if (current) return current;
    const request = Promise.resolve().then(save).then(() => close(id));
    pending.set(id, request);
    const release = () => { if (pending.get(id) === request) pending.delete(id); };
    void request.then(release, release);
    return request;
  };
}
