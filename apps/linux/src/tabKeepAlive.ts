import { keepAliveReason, shownTabIds } from "../../browser/src/state/tabs/tabKeepAlive";
import type { webviews } from "../../browser/src/platform/webviews";

export { keepAliveReason, shownTabIds };

type WebViewHandle = NonNullable<ReturnType<typeof webviews.get>>;
type UnsavedInputHandle = { unsavedInput?: () => Promise<unknown> };

const CHECK_TIMEOUT_MS = 2_000;

export async function hasUnsavedInput(handle: WebViewHandle, timeoutMs = CHECK_TIMEOUT_MS): Promise<boolean | null> {
  const check = (handle as unknown as UnsavedInputHandle).unsavedInput;
  if (typeof check !== "function") return null;
  let timer: ReturnType<typeof setTimeout> | undefined;
  const expired = new Promise<null>((resolve) => { timer = setTimeout(() => resolve(null), timeoutMs); });
  try {
    const result = await Promise.race([Promise.resolve().then(() => check.call(handle)).catch(() => null), expired]);
    return typeof result === "boolean" ? result : null;
  } finally {
    clearTimeout(timer);
  }
}
