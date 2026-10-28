#ifndef ARDIA_LINUX_AC_LINUX_CREDENTIAL_SERVICE_H_
#define ARDIA_LINUX_AC_LINUX_CREDENTIAL_SERVICE_H_

#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/values.h"

namespace ardia {

namespace linux_sync {
class StoredLogin;
struct LoginRead;
}

class LinuxShellWebView;

bool LinuxCredentialsHandles(std::string_view name);
void LinuxCredentialsCall(const std::string& engine_profile, bool private_window, const std::string& name,
                          const base::DictValue& input, base::OnceCallback<void(bool, base::Value)> done);

bool LinuxCredentialsInvokes(std::string_view method);
void LinuxCredentialsInvoke(bool private_window, LinuxShellWebView* shell, const std::string& method,
                            const base::ListValue& values, base::OnceCallback<void(bool, base::Value)> done);

void LinuxCredentialsReadLogins(const std::string& engine_profile,
                                base::OnceCallback<void(linux_sync::LoginRead)> done);
void LinuxCredentialsSaveLogin(const std::string& engine_profile, linux_sync::StoredLogin login, bool replacing,
                               base::OnceCallback<void(bool)> done);

}

#endif
