import { startBookmarks } from "../../browser/src/lib/bookmarks";
import { startHistory } from "../../browser/src/lib/history";
import { startFavicons } from "../../browser/src/lib/favicons";
import { startProfileDataCleanup } from "../../browser/src/lib/profileData";

export function startHostData() {
  const stops = [startFavicons(), startHistory(), startBookmarks(), startProfileDataCleanup()];
  return () => { for (const stop of stops) stop(); };
}
