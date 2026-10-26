import { forwardRef, useEffect, useImperativeHandle, useRef } from "react";
import { View } from "react-native";
import { toAppUrl, toEngineUrl } from "@ardia/core";
import type { NavigationState, WebViewHandle, WebViewProps } from "@ardia-real/ardiacore";
import { ardiaHost, reportHostError } from "../../host";
import { HostTab, type HostTabEvent } from "../../hostTab";
import { hostOverlayBlocked, watchTabViewport } from "../../hostViewport";
import { menuShown } from "../../menus";

const EVENT_CALLBACKS: Record<string, keyof WebViewProps> = {
  media: "onMedia", nowPlaying: "onNowPlaying", mediaAccess: "onMediaAccess", openWindow: "onOpenWindow",
  popupBlocked: "onPopupBlocked", findResult: "onFindResult", fullscreen: "onFullscreen", status: "onStatus", crashed: "onCrashed",
  unresponsive: "onUnresponsive", responsive: "onResponsive", security: "onSecurity", zoom: "onZoom", contentBlocked: "onContentBlocked",
  downloadNavigation: "onDownloadNavigation", notification: "onNotification", notificationClose: "onNotificationClose",
  pictureInPicture: "onPictureInPicture", activateRequest: "onActivateRequest", displayMediaRequest: "onDisplayMediaRequest",
  windowClose: "onWindowClose", command: "onCommand", pageFocus: "onPageFocus", discarded: "onDiscarded",
  passwordPrompt: "onPasswordPrompt", externalApp: "onExternalApp", preferredSize: "onPreferredSize",
};

export const HostWebView = forwardRef<WebViewHandle, WebViewProps>(function HostWebView(props, ref) {
  const latest = useRef(props);
  latest.current = props;
  const element = useRef<HTMLDivElement>(null);
  const tab = useRef<HostTab | null>(null);
  const navigation = useRef<NavigationState | null>(null);
  const key = useRef(props.transferKey ?? `host_${crypto.randomUUID()}`);
  const requestedUrl = useRef(props.url);
  const ready = useRef(false);

  const receive = (event: HostTabEvent) => {
    const p = latest.current;
    if (event.type === "ready") {
      if (!ready.current) p.onReady?.(event.browserId as number, event.chromeTabId as number);
      ready.current = true;
    } else if (event.type === "navigation") {
      const state = event.state as NavigationState;
      navigation.current = { ...state, url: toAppUrl(state.url), ...(state.committedUrl ? { committedUrl: toAppUrl(state.committedUrl) } : {}) };
      p.onNavigationChange?.(navigation.current);
    } else if (event.type === "title" && navigation.current) {
      navigation.current = { ...navigation.current, title: String(event.title ?? "") };
      p.onNavigationChange?.(navigation.current);
    } else if (event.type === "progress") p.onProgress?.(event.progress as number);
    else if (event.type === "favicon") p.onFavicon?.(event.url as string, (event.candidates ?? []) as string[]);
    else if (event.type === "loadError") {
      const error = event.error as { url: string; code: number; text: string };
      p.onLoadError?.({ ...error, url: toAppUrl(error.url) });
    } else if (event.type === "pageMessage") p.onPageMessage?.(event.kind as string, event.data);
    else if (event.type === "autofillPrompt") p.onAutofillPrompt?.((event.value ?? null) as Parameters<NonNullable<WebViewProps["onAutofillPrompt"]>>[0], event.id as number);
    else {
      const callback = p[EVENT_CALLBACKS[event.type]!] as ((value?: unknown) => void) | undefined;
      let value = event.value;
      if (["openWindow", "popupBlocked", "downloadNavigation"].includes(event.type) && value && typeof value === "object" && "url" in value) {
        const v = value as { url: string; committedUrl?: string };
        value = { ...v, url: toAppUrl(v.url), ...(v.committedUrl ? { committedUrl: toAppUrl(v.committedUrl) } : {}) };
      }
      if (event.type === "discarded" && typeof value === "string") value = toAppUrl(value);
      callback?.(value);
    }
  };

  useEffect(() => {
    const host = ardiaHost!;
    const current = new HostTab(host, { key: key.current, profile: props.profile, url: toEngineUrl(props.url ?? "about:blank"), ...(props.adoptId ? { adoptId: props.adoptId } : {}) }, receive);
    tab.current = current;
    void current.ready.catch((error) => {
      if (!current.disposed) latest.current.onLoadError?.({ url: latest.current.url ?? "", code: -2, text: String(error) });
      reportHostError(error);
    });
    return () => {
      current.dispose();
      if (tab.current === current) tab.current = null;
    };
  }, []);

  useEffect(() => {
    if (props.url === undefined || props.url === requestedUrl.current) return;
    requestedUrl.current = props.url;
    void tab.current?.command("loadUrl", [toEngineUrl(props.url)]).catch(reportHostError);
  }, [props.url]);

  useEffect(() => {
    void tab.current?.presentation(props.visible !== false).catch(reportHostError);
  }, [props.visible]);

  useImperativeHandle(ref, () => new Proxy({} as WebViewHandle, {
    get(_target, property) {
      if (typeof property !== "string" || property === "then") return undefined;
      if (property === "nativeTag") return () => null;
      return async (...args: unknown[]) => {
        const current = tab.current;
        if (!current) throw new Error("Ardia tab is not mounted");
        if (property === "loadUrl") {
          requestedUrl.current = args[0] as string;
          args[0] = toEngineUrl(args[0] as string);
        }
        const result = await current.command(property, args);
        if (property === "navigationEntries" && Array.isArray(result)) return result.map((entry: { url: string }) => ({ ...entry, url: toAppUrl(entry.url) }));
        return result;
      };
    },
  }), []);

  useEffect(() => {
    const node = element.current;
    if (!node) return;
    let revision = 0;
    let last = "";
    let backdrop: Promise<boolean> | null = null;
    let retryAfter = 0;
    const stop = watchTabViewport(node, () => {
      const p = latest.current;
      return p.visible !== false && !menuShown() && !hostOverlayBlocked(p.transferKey);
    }, (viewport) => {
      const current = tab.current;
      if (!current?.identity) return;
      if (viewport.occlusions?.length && Date.now() < retryAfter) return;
      const json = JSON.stringify(viewport);
      if (json === last) return;
      last = json;
      const mine = ++revision;
      void (async () => {
        if (viewport.visible && viewport.occlusions?.length) {
          if (!backdrop) {
            const capture: Promise<boolean> = current.command<string | null>("captureViewport").then(async (data) => {
              if (!data) return false;
              const image = new Image();
              image.src = data;
              await image.decode();
              if (current.disposed || backdrop !== capture) return false;
              node.style.backgroundImage = `url("${data}")`;
              return true;
            }).catch((error) => {
              reportHostError(error);
              return false;
            });
            backdrop = capture;
          }
          if (!await backdrop) {
            if (mine === revision) {
              backdrop = null;
              last = "";
              retryAfter = Date.now() + 250;
            }
            return;
          }
        }
        if (mine !== revision || current.disposed) return;
        await current.viewport(viewport);
        if (mine === revision && !viewport.occlusions?.length) {
          backdrop = null;
          node.style.backgroundImage = "";
        }
      })().catch((error) => {
        if (mine === revision) last = "";
        reportHostError(error);
      });
    });
    return () => {
      revision++;
      backdrop = null;
      stop();
    };
  }, []);

  return <View style={[props.style, { backgroundColor: "transparent" }]}><div ref={element} data-ardia-native-tab={key.current} style={{ position: "absolute", inset: 0, backgroundSize: "100% 100%" }} /></View>;
});
