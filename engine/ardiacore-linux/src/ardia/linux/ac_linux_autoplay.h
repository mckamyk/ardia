#ifndef ARDIA_LINUX_AC_LINUX_AUTOPLAY_H_
#define ARDIA_LINUX_AC_LINUX_AUTOPLAY_H_

class GURL;

namespace content {
class BrowserContext;
}

namespace ardiacore {

bool LinuxAutoplayBlocked(const GURL& url, content::BrowserContext* context);
void InstallLinuxAutoplayPolicy();

}

#endif
