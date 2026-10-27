#ifndef ARDIA_LINUX_AC_LINUX_SUGGEST_H_
#define ARDIA_LINUX_AC_LINUX_SUGGEST_H_

#include <optional>
#include <string>

#include "base/functional/callback.h"

class Profile;

namespace ardia::linux_suggest {

using SuggestionReply = base::OnceCallback<void(std::optional<std::string>)>;

bool CanFetchSuggestions(Profile* profile, const std::string& url);

void FetchSuggestions(Profile* profile, const std::string& url,
                      SuggestionReply reply);

}

#endif
