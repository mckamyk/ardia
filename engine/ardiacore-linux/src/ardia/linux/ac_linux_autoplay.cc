#include "ardia/linux/ac_linux_autoplay.h"

#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

extern bool (*g_ardia_linux_autoplay_blocked)(content::WebContents* contents);

namespace ardiacore {

bool LinuxAutoplayBlocked(const GURL& url, content::BrowserContext* context) {
  if (!context || !url.SchemeIsHTTPOrHTTPS()) return false;
  HostContentSettingsMap* settings = HostContentSettingsMapFactory::GetForProfile(Profile::FromBrowserContext(context));
  return settings && settings->GetContentSetting(url, url, ContentSettingsType::AUTOPLAY) == CONTENT_SETTING_BLOCK;
}

void InstallLinuxAutoplayPolicy() {
  g_ardia_linux_autoplay_blocked = [](content::WebContents* contents) {
    return contents && LinuxAutoplayBlocked(contents->GetLastCommittedURL(), contents->GetBrowserContext());
  };
}

}
