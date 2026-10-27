import { startBookmarks } from "../../browser/src/state/bookmarks/engineSync";
import { startHistory } from "../../browser/src/state/history/engineSync";
import { startFavicons } from "../../browser/src/state/favicons/favicons";
import { startProfileDataCleanup } from "../../browser/src/state/profiles/profileData";

export function startHostData() {
  const stops = [startFavicons(), startHistory(), startBookmarks(), startProfileDataCleanup()];
  return () => { for (const stop of stops) stop(); };
}
