(() => {
  "use strict";
  if (location.protocol !== "chrome:" || location.hostname !== "ardia" || window !== window.top) return;
  if (typeof globalThis.chrome?.send !== "function" || window.__ardiaHost) return;

  const bootstrap = globalThis.__ardiaBootstrap ?? {};
  delete globalThis.__ardiaBootstrap;
  const pending = new Map();
  const listeners = new Set();
  const beforeClose = new Set();
  const prefix = crypto.randomUUID();
  let sequence = 0;
  let disposed = false;

  const request = (method, args) => {
    if (disposed) return Promise.reject(new Error("The browser UI has closed"));
    if (pending.size >= 256) return Promise.reject(new Error("Too many pending native requests"));
    return new Promise((resolve, reject) => {
      const id = `${prefix}:${++sequence}`;
      const timer = setTimeout(() => {
        if (!pending.delete(id)) return;
        reject(new Error(`Native request timed out: ${method}`));
      }, 30000);
      pending.set(id, { resolve, reject, timer });
      try {
        chrome.send("ardiaRequest", [id, method, args]);
      } catch (error) {
        pending.delete(id);
        clearTimeout(timer);
        reject(error);
      }
    });
  };

  Object.defineProperty(window, "__ardiaReply", {
    value(id, success, result) {
      const entry = pending.get(id);
      if (!entry) return;
      pending.delete(id);
      clearTimeout(entry.timer);
      if (success) entry.resolve(result);
      else entry.reject(new Error(typeof result === "string" ? result : "Native request failed"));
    },
  });

  Object.defineProperty(window, "__ardiaEvent", {
    value(module, event, payload) {
      if (disposed) return;
      for (const listener of [...listeners]) {
        try {
          listener(module, event, payload);
        } catch (error) {
          console.error("[ardia host] event listener failed", error);
        }
      }
    },
  });

  Object.defineProperty(window, "__ardiaHost", {
    value: Object.freeze({
      bootstrap: Object.freeze(bootstrap),
      call(name, profile, args = null) {
        if (typeof name !== "string" || typeof profile !== "string" || (args !== null && typeof args !== "string")) {
          return Promise.reject(new TypeError("Invalid engine call"));
        }
        return request("engine.call", { name, profile, args });
      },
      invoke(method, args = {}) {
        if (typeof method !== "string" || !args || typeof args !== "object" || Array.isArray(args)) {
          return Promise.reject(new TypeError("Invalid host call"));
        }
        return request("host.invoke", { method, args });
      },
      onEvent(listener) {
        if (typeof listener !== "function") throw new TypeError("Expected an event listener");
        listeners.add(listener);
        return () => listeners.delete(listener);
      },
      onBeforeClose(listener) {
        if (typeof listener !== "function") throw new TypeError("Expected a close listener");
        beforeClose.add(listener);
        return () => beforeClose.delete(listener);
      },
    }),
  });

  addEventListener("pagehide", () => {
    for (const listener of [...beforeClose]) {
      try {
        listener();
      } catch (error) {
        console.error("[ardia host] close listener failed", error);
      }
    }
    disposed = true;
    for (const { reject, timer } of pending.values()) {
      clearTimeout(timer);
      reject(new Error("The browser UI has closed"));
    }
    pending.clear();
    listeners.clear();
    beforeClose.clear();
  }, { once: true });
})();
