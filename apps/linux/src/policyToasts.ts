import { showToast as showSharedToast } from "../../browser/src/state/splits/actions";
import { policyToastTarget } from "./hostPolicies";

export * from "../../browser/src/state/splits/actions";

export function showToast(...[windowId, ...rest]: Parameters<typeof showSharedToast>) {
  const target = policyToastTarget(windowId);
  if (target) showSharedToast(target, ...rest);
}
