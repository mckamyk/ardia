#include "chrome/browser/ardia/ac_reauth.h"

#include <utility>

#include "base/check.h"
#include "base/functional/callback.h"
#include "chrome/browser/ardia/ac_engine.h"
#include "content/public/browser/browser_thread.h"

namespace ardia {

bool ReauthAvailable() {
  return false;
}

void Reauth(Profile* profile,
            device_reauth::DeviceAuthSource,
            const std::u16string& purpose,
            base::TimeDelta,
            base::OnceCallback<void(bool)> done) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  Emit("reauth.requested", profile,
       base::DictValue()
           .Set("purpose", purpose)
           .Set("granted", false)
           .Set("error", "OS authentication is unavailable on this platform"));
  std::move(done).Run(false);
}

}
