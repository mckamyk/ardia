import { Symbol } from "@ardia/shell";
import { openProfileMenu } from "../../browser/src/components/ProfileIndicator";
import { openSettings } from "./settingsWindows";
import { profileNameColor, useTheme } from "../../browser/src/lib/theme";
import { useBrowser } from "../../browser/src/store/browser";
import { useWindowId } from "../../browser/src/store/hooks";

export { PROFILE_INDICATOR_X, openProfileMenu } from "../../browser/src/components/ProfileIndicator";

export const useProfileIndicatorWidth = (room: number) => Math.max(0, Math.min(room, 140));

export function ProfileIndicator({ room }: { room: number }) {
  const theme = useTheme();
  const windowId = useWindowId();
  const profile = useBrowser((s) => s.profiles[s.windows[windowId]?.profileId ?? ""]);
  const incognito = useBrowser((s) => !!s.windows[windowId]?.incognito);
  const name = incognito ? "Incognito" : profile?.name ?? "Personal";
  const color = !incognito && profile ? profileNameColor(profile, theme.dark) : theme.textPrimary;
  return (
    <div className="ardia-profile-controls" data-ardia-profile-controls style={{ width: useProfileIndicatorWidth(room), color }}>
      <button className="ardia-header-button ardia-current-space" data-ardia-current-space={profile?.id} aria-label={`Current space: ${name}`} title={name} onClick={() => void openProfileMenu(windowId)} disabled={incognito}>
        <span>{name}</span>
      </button>
      <button className="ardia-header-button ardia-settings-button" data-ardia-open-settings aria-label="Settings" title="Settings (Ctrl+,)" aria-keyshortcuts="Control+," onClick={() => openSettings()}>
        <Symbol name="gearshape" size={15} color={theme.icon} style={{ width: 18, height: 18 }} />
      </button>
    </div>
  );
}
