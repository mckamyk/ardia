import { provideNativeModuleProvider } from "../../../demo/src/shims/expo-modules-core";
import { ardiaHost, callHost } from "../../../demo/src/host";

export * from "@ardia-real/ardiacore";
export { devPagerState, pagerState, selectPager, stepPager, SwipeArea, switchPager } from "../../../demo/src/shims/web/Pager";
export { HostWebView as WebView } from "../../../demo/src/shims/web/HostWebView";

provideNativeModuleProvider((module, method) => module === "ArdiaCEF" && method === "engineCall" && ardiaHost
  ? (name: string, profile: string, args: string | null) => callHost(name, profile, args)
  : undefined);
