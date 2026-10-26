import { provideNativeModuleProvider } from "./expo-modules-core";
import { ardiaHost, callHost } from "../host";

export * from "@ardia-real/ardiacore";
export { devPagerState, pagerState, selectPager, stepPager, SwipeArea, switchPager } from "./web/Pager";
export { HostWebView as WebView } from "./web/HostWebView";

provideNativeModuleProvider((module, method) => module === "ArdiaCEF" && method === "engineCall" && ardiaHost
  ? (name: string, profile: string, args: string | null) => callHost(name, profile, args)
  : undefined);
