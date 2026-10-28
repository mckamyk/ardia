import f01 from "./f01-page-commands.mjs";
import f02 from "./f02-profiles.mjs";
import f03 from "./f03-site-security.mjs";
import f06 from "./f06-site-controls.mjs";
import f07 from "./f07-passwords-autofill.mjs";
import f08 from "./f08-downloads.mjs";
import f09 from "./f09-extensions.mjs";
import f10 from "./f10-suggestions.mjs";
import f12 from "./f12-import.mjs";
import { i109, i110, i111 } from "./i-import-followups.mjs";
import f17 from "./f17-page-status.mjs";
import f15 from "./f15-media.mjs";
import f16 from "./f16-page-messaging.mjs";
import f18 from "./f18-policies.mjs";
import f13 from "./f13-sync.mjs";
import f20 from "./f20-drop.mjs";
import f22 from "./f22-focus.mjs";
import f23 from "./f23-history-zoom.mjs";
import f24 from "./f24-favicons.mjs";
import f25 from "./f25-tint-toolbar.mjs";
import f26 from "./f26-platform-settings.mjs";
import f27 from "./f27-app-services.mjs";
import w78 from "./w78-page-fullscreen.mjs";
import s102 from "./s102-key-provider.mjs";
import s104 from "./s104-credential-wording.mjs";
import s105 from "./s105-polkit-policy.mjs";
import s106 from "./s106-blocker-hold.mjs";
import s107 from "./s107-component-restriction.mjs";
import s108 from "./s108-isolated-blocker.mjs";
import privateServices from "./private-services.mjs";
import { w70, w77, w79, w92, w114 } from "./r-window-followups.mjs";
import { w74, w75, w76, w137 } from "./r-downloads-followups.mjs";

export const SCENARIOS = [f01, f02, f03, f06, f23, f07, f08, f09, f20, f10, f12, f13, f22, f26, f27, f17, w78, f24, f15, f18, f16, f25, s102, s104, s105, s106, s107, s108, ...privateServices, w92, w77, w79, w70, w114, w74, w75, w76, w137, i109, i110, i111];

export function selectScenarios(only, scenarios = SCENARIOS) {
  if (!only?.length) return scenarios;
  const wanted = new Set(only.map((id) => String(id).trim().toUpperCase().replace(/^#/, "")));
  const unknown = [...wanted].filter((id) => !scenarios.some((s) => s.id === id || String(s.issue) === id));
  if (unknown.length) throw new Error(`Unknown scenario ${unknown.join(", ")}; choose from ${scenarios.map((s) => `${s.id} (#${s.issue})`).join(", ")}`);
  return scenarios.filter((s) => wanted.has(s.id) || wanted.has(String(s.issue)));
}
