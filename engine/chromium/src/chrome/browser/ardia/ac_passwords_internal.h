// Copyright 2026 Ardia. Apache-2.0.

#ifndef CHROME_BROWSER_ARDIA_AC_PASSWORDS_INTERNAL_H_
#define CHROME_BROWSER_ARDIA_AC_PASSWORDS_INTERNAL_H_

#include <optional>
#include <string>

#include "base/functional/callback_helpers.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ardia/ac_engine.h"
#include "chrome/browser/extensions/api/passwords_private/passwords_private_delegate.h"

class Profile;

#define AC_PASSWORDS_OPEN_WITH(json)                      \
  Call call(profile_dir, json, reply, context);           \
  if (!call) {                                            \
    return;                                               \
  }                                                       \
  std::optional<Session> session =                        \
      ardia::passwords_internal::Open(call);              \
  if (!session) {                                         \
    return call.TakeReply().Error("no password manager"); \
  }

#define AC_PASSWORDS_OPEN() AC_PASSWORDS_OPEN_WITH(args_json)

namespace ardia {
namespace passwords_internal {

class PasswordsState;

struct Session {
  base::WeakPtr<PasswordsState> state;
  base::WeakPtr<extensions::PasswordsPrivateDelegate> delegate;
  base::ScopedClosureRunner busy;

  bool alive() const;
  Profile* profile() const;
};

std::optional<Session> Open(const Call& call);
std::string WithoutSlash(std::string realm);

}
}

#endif
