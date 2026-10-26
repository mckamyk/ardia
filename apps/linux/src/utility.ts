import { closeWindow, isDarkAppearance, onAppEvent, onCommand, setAppearance } from "@ardia/shell";
import { Appearance } from "react-native";
import { reportHostError, type ArdiaHost } from "../../demo/src/host";
import { useBrowser } from "../../browser/src/store/browser";
import { openImport, openSettings, useSettingsNav, type SettingsPlace } from "../../browser/src/components/settings/windows";
import { PANES } from "../../browser/src/components/settings/settingsIndex";
import { focusSettingsSearch } from "../../browser/src/components/settings/searchState";
import { isDemoMenuShown } from "../../demo/src/shims/web/menus";
import { startUtilityNavigation } from "./utilityNavigation";
import { setPreferredImportSource } from "./settingsWindows";

function setNavigation(value: unknown) {
  if (!value || typeof value !== "object") return;
  const place = value as SettingsPlace;
  const importSource = (value as { importSource?: unknown }).importSource;
  if (typeof importSource === "string") setPreferredImportSource(importSource);
  if (!PANES.some((pane) => pane.id === place.pane)) return;
  const nav = useSettingsNav.getState();
  nav.go(place.pane, typeof place.profileId === "string" ? place.profileId : null, typeof place.detail === "string" ? place.detail : null);
}

export function startUtility(host: ArdiaHost) {
  const id = host.bootstrap.windowId;
  onCommand(({ command }) => {
    if (command === "closeTab") void closeWindow(id).catch(reportHostError);
    else if (command === "findInPage" && id === "settings") focusSettingsSearch();
    else if (command === "openSettings") openSettings();
    else if (command === "keyboardShortcuts") openSettings("shortcuts");
    else if (command === "importBrowserData") openImport();
  });
  window.addEventListener("keydown", (event) => {
    if (event.key !== "Escape" || event.defaultPrevented || isDemoMenuShown()) return;
    event.preventDefault();
    void closeWindow(id).catch(reportHostError);
  });
  const store = useBrowser;
  store.getState().setAppDark(Appearance.getColorScheme() === "dark");
  void setAppearance(store.getState().settings.appearance);
  void isDarkAppearance().then((dark) => store.getState().setAppDark(dark));
  onAppEvent((event) => {
    if (event.type === "appearance") store.getState().setAppDark(event.dark);
  });
  store.subscribe((state, previous) => {
    if (state.settings.appearance !== previous.settings.appearance) void setAppearance(state.settings.appearance);
  });
  return startUtilityNavigation(host, setNavigation).ready;
}
