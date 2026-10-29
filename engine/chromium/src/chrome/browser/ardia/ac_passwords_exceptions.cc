// Copyright 2026 Ardia. Apache-2.0.

#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "chrome/browser/ardia/ac_passwords.h"
#include "chrome/browser/ardia/ac_passwords_internal.h"
#include "chrome/common/extensions/api/passwords_private.h"

namespace ardia {
namespace {

std::string ExceptionOrigin(
    const extensions::api::passwords_private::ExceptionEntry& entry) {
  return passwords_internal::WithoutSlash(entry.urls.signon_realm.empty()
                                              ? entry.urls.link
                                              : entry.urls.signon_realm);
}

}
}

using ardia::Call;
using ardia::Reply;
using ardia::passwords_internal::Session;

AC_ENGINE_CALL(ac_passwords_exceptions) {
  AC_PASSWORDS_OPEN();
  base::WeakPtr<extensions::PasswordsPrivateDelegate> delegate =
      session->delegate;
  delegate->GetPasswordExceptionsList(base::BindOnce(
      [](Session, Reply reply,
         const extensions::PasswordsPrivateDelegate::ExceptionEntries&
             exceptions) {
        base::ListValue origins;
        for (const auto& exception : exceptions) {
          std::string origin = ardia::ExceptionOrigin(exception);
          if (!origin.empty()) {
            origins.Append(std::move(origin));
          }
        }
        reply.Send(base::DictValue().Set("origins", std::move(origins)));
      },
      std::move(*session), call.TakeReply()));
}

AC_ENGINE_CALL(ac_passwords_allow) {
  AC_PASSWORDS_OPEN();
  base::WeakPtr<extensions::PasswordsPrivateDelegate> delegate =
      session->delegate;
  delegate->GetPasswordExceptionsList(base::BindOnce(
      [](std::string origin, Session session, Reply reply,
         const extensions::PasswordsPrivateDelegate::ExceptionEntries&
             exceptions) {
        std::vector<int> ids;
        for (const auto& exception : exceptions) {
          if (ardia::ExceptionOrigin(exception) == origin) {
            ids.push_back(exception.id);
          }
        }
        for (int id : ids) {
          if (session.alive()) {
            session.delegate->RemovePasswordException(id);
          }
        }
        reply.Ok();
      },
      call.String("origin"), std::move(*session), call.TakeReply()));
}
