import { isLinuxUtility } from "./session";

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
