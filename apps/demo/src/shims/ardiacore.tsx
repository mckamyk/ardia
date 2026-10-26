import type { FaviconImage } from "@ardia-real/ardiacore";
import { faviconFor } from "../pages";
import { provideNativeModule } from "./expo-modules-core";

export * from "@ardia-real/ardiacore";
export { devPagerState, pagerState, selectPager, stepPager, SwipeArea, switchPager } from "./web/Pager";
export { WebView } from "./web/WebView";

provideNativeModule("ArdiaCEF", {
  engineCall: async (name: string) => JSON.stringify({ error: `${name} is not on the web` }),
  tabStrips: async () => ({ rev: 0, cmd: null, strips: [] }),
});
provideNativeModule("ArdiaExtensions", { list: async () => ({ extensions: [] }), searchEngineList: async () => ({ list: null }) });

export async function faviconsFor(_profile: string, pages: string[], _icons: string[] = []) {
  const found = pages.flatMap((page) => {
    const uri = faviconFor(page);
    return uri ? [[page, { uri, icon: uri }] as const] : [];
  });
  return { pages: new Map(found), icons: new Map<string, string>() };
}

export async function fetchFavicon(url: string, _profile: string, _page?: string): Promise<FaviconImage | null> {
  return { uri: url, width: 32, height: 32 };
}
