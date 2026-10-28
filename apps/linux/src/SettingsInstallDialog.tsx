import { useEffect } from "react";
import { InstallDialogHost as SharedInstallDialogHost } from "../../browser/src/components/extensions/ExtensionOverlays";
import { useExtensions } from "./extensionsStore";
import { followInstallRequest } from "./installRequestSheet";

export * from "../../browser/src/components/extensions/ExtensionOverlays";

const keepSheet = () => {};

export function InstallDialogHost({ windowId, onClosed }: { windowId: string; onClosed: () => void }) {
  const requested = useExtensions((e) => e.install?.windowId === windowId);
  useEffect(() => followInstallRequest(useExtensions, windowId, onClosed), [windowId, onClosed]);
  return requested ? <SharedInstallDialogHost windowId={windowId} onClosed={keepSheet} /> : null;
}
