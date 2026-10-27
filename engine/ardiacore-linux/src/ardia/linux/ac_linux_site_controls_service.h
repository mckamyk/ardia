#ifndef ARDIA_LINUX_AC_LINUX_SITE_CONTROLS_SERVICE_H_
#define ARDIA_LINUX_AC_LINUX_SITE_CONTROLS_SERVICE_H_

#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/values.h"

class Profile;

namespace ardia {

using SiteControlsDone = base::OnceCallback<void(bool, base::Value)>;
using ContentBlockerChanged = base::RepeatingCallback<void(Profile*, base::Value)>;

bool LinuxSiteControlsHandles(std::string_view name);
void LinuxSiteControlsCall(Profile* window_profile, const std::string& name,
                           const std::string& requested_profile, const base::Value* args,
                           SiteControlsDone done);

void StartLinuxSiteControls(ContentBlockerChanged changed);
void StartLinuxContentBlocker(Profile* profile);

}

#endif
