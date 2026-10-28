#ifndef ARDIA_LINUX_AC_LINUX_CONTENT_HOOKS_H_
#define ARDIA_LINUX_AC_LINUX_CONTENT_HOOKS_H_

#include <utility>
#include <vector>

#include "content/public/browser/media_player_id.h"

namespace content {
class WebContents;

std::vector<std::pair<MediaPlayerId, bool>> ArdiaLinuxPictureInPictureCandidates(WebContents* contents);
std::vector<std::pair<MediaPlayerId, bool>> ArdiaLinuxPlayedVideoPlayers(WebContents* contents);
bool ArdiaLinuxRequestPictureInPicture(WebContents* contents, const MediaPlayerId& id);
}

#endif
