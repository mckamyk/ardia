#ifndef ARDIA_LINUX_AC_LINUX_DOWNLOADS_H_
#define ARDIA_LINUX_AC_LINUX_DOWNLOADS_H_

#include <cstddef>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

namespace ardia::linux_downloads {

inline constexpr size_t kMaxPathBytes = 4096;
inline constexpr size_t kMaxStatusPaths = 64;

bool ValidDownloadId(std::string_view id);
bool ValidLocalPath(std::string_view path);
std::string ValidateRequest(std::string_view name, const base::DictValue& input);
std::string FileUrl(std::string_view path);
std::string BaseName(std::string_view path);

enum class RevealBackend { kDesktop, kUnavailable };
RevealBackend ChooseRevealBackend(bool session_bus, bool xdg_open);

bool PathListed(std::string_view path, const std::vector<std::string>& known);

struct TrashOutcome {
  bool ok = false;
  std::string code;
  std::string message;
  std::string trashed;
};

std::string TrashDirectory(const char* xdg_data_home, const char* home);
TrashOutcome MoveToTrash(const std::string& path, const std::string& trash_directory,
                         std::time_t now);

}

#endif
