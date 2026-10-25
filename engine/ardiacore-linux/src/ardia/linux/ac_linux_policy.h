#ifndef ARDIA_LINUX_AC_LINUX_POLICY_H_
#define ARDIA_LINUX_AC_LINUX_POLICY_H_

#include <string_view>

namespace ardia::linux_policy {

inline bool ValidDocumentName(std::string_view name) {
  if (name.empty() || name.size() > 128 || name == "." || name == "..") {
    return false;
  }
  for (const char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
      return false;
    }
  }
  return true;
}

inline bool ValidAssetPath(std::string_view path) {
  if (path.empty() || path.size() > 1024 || path.front() == '/') {
    return false;
  }
  while (!path.empty()) {
    const auto slash = path.find('/');
    const auto component = path.substr(0, slash);
    if (!ValidDocumentName(component)) {
      return false;
    }
    if (slash == std::string_view::npos) {
      return true;
    }
    path.remove_prefix(slash + 1);
  }
  return false;
}

}

#endif
