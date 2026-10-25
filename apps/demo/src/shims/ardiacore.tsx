import type { FaviconImage } from "@ardia-real/ardiacore";
import { faviconFor } from "../pages";
import { provideNativeModule } from "./expo-modules-core";
import { provideNativeModuleProvider } from "./expo-modules-core";
import { ardiaHost, callHost } from "../host";
import { faviconsFor as nativeFaviconsFor, fetchFavicon as nativeFetchFavicon } from "@ardia-real/ardiacore";

export * from "@ardia-real/ardiacore";
export { devPagerState, pagerState, selectPager, stepPager, SwipeArea, switchPager } from "./web/Pager";
export { WebView } from "./web/WebView";

provideNativeModule("ArdiaCEF", {
  engineCall: async (name: string) => JSON.stringify({ error: `${name} is not on the web` }),
  tabStrips: async () => ({ rev: 0, cmd: null, strips: [] }),
});
provideNativeModule("ArdiaExtensions", { list: async () => ({ extensions: [] }), searchEngineList: async () => ({ list: null }) });

provideNativeModuleProvider((module, method) => module === "ArdiaCEF" && method === "engineCall" && ardiaHost
  ? (name: string, profile: string, args: string | null) => callHost(name, profile, args)
  : undefined);

export async function faviconsFor(_profile: string, pages: string[], _icons: string[] = []) {
  if (ardiaHost) return nativeFaviconsFor(_profile, pages, _icons);
  const found = pages.flatMap((page) => {
    const uri = faviconFor(page);
    return uri ? [[page, { uri, icon: uri }] as const] : [];
  });
  return { pages: new Map(found), icons: new Map<string, string>() };
}

export async function fetchFavicon(url: string, _profile: string, _page?: string): Promise<FaviconImage | null> {
  if (ardiaHost) return nativeFetchFavicon(url, _profile, _page);
  return { uri: url, width: 32, height: 32 };
}
