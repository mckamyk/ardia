type InstallRequestStore = {
  getState(): { install: { windowId: string } | null };
  subscribe(listener: (state: { install: { windowId: string } | null }) => void): () => void;
};

export function followInstallRequest(store: InstallRequestStore, windowId: string, onClosed: () => void) {
  let shown = store.getState().install?.windowId === windowId;
  return store.subscribe((state) => {
    if (state.install?.windowId === windowId) shown = true;
    else if (shown) {
      shown = false;
      onClosed();
    }
  });
}
