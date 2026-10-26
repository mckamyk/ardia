import { openWindow, type OpenWindowOptions } from "@ardia/shell";
import { useSettingsNav, type SettingsPane, type SettingsPlace } from "../../browser/src/components/settings/windows";
import { ardiaHost, reportHostError } from "./host";
import { flushLinuxSession } from "./session";

export * from "../../browser/src/components/settings/windows";

export function openSettings(pane?: SettingsPane, profileId?: string | null) {
  let settings: SettingsPlace | undefined;
  if (pane) {
    useSettingsNav.getState().go(pane, profileId ?? null);
    settings = { pane, profileId: profileId ?? null, detail: null };
  }
  const options: OpenWindowOptions & { settings?: SettingsPlace } = { kind: "settings", title: "Settings", ...(settings ? { settings } : {}) };
  void flushLinuxSession().then(() => openWindow("settings", options)).catch(reportHostError);
}

let preferredImportSource = ardiaHost?.bootstrap.utility?.importSource ?? null;
export const setPreferredImportSource = (source: string) => void (preferredImportSource = source);

export function takePreferredImportSource() {
  const source = preferredImportSource;
  preferredImportSource = null;
  return source;
}

export function openImport(browserId?: string) {
  const options: OpenWindowOptions & { importSource?: string } = { kind: "import", title: "Import from Another Browser", ...(browserId ? { importSource: browserId } : {}) };
  void flushLinuxSession().then(() => openWindow("import", options)).catch(reportHostError);
}
