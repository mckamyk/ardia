#ifndef ARDIA_LINUX_AC_LINUX_DOWNLOADS_H_
#define ARDIA_LINUX_AC_LINUX_DOWNLOADS_H_

#include <sys/types.h>

#include <cstddef>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ardia/linux/ac_linux_profiles.h"
#include "base/values.h"

namespace ardia::linux_downloads {

inline constexpr size_t kMaxPathBytes = 4096;
inline constexpr size_t kMaxStatusPaths = 64;
inline constexpr size_t kMaxKeptDownloads = 100;
inline constexpr const char* kFileCapabilities[] = {"ArdiaSystem.fileStatus", "ArdiaFileDrop.takeDrop",
                                                     "ArdiaFileDrag.begin"};

bool ValidDownloadId(std::string_view id);
bool ValidLocalPath(std::string_view path);
std::string ValidateRequest(std::string_view name, const base::DictValue& input);
std::string FileUrl(std::string_view path);
std::string BaseName(std::string_view path);

std::optional<base::ListValue> FilterSnapshot(const base::Value& reply, bool off_the_record = false);

struct DownloadRoute {
  ardiacore::linux_profiles::ProfileError error = ardiacore::linux_profiles::ProfileError::kNone;
  bool off_the_record = false;
};

DownloadRoute RouteDownloads(ardiacore::linux_profiles::ProfileKind requested, bool page_private,
                             bool page_ending, bool hosts_requested);
bool IsDownloadControl(std::string_view name);
std::string_view DownloadControlService(std::string_view name);
std::vector<std::string> KeptIds(const base::DictValue& input);
double ClearCutoff(const base::DictValue& input);

bool IconPathAllowed(std::string_view path, const std::vector<std::string>& targets);
std::vector<bool> ListedPaths(const std::vector<std::string>& paths, const std::vector<std::string>& targets);

enum class OpenBackend { kDesktop, kUnavailable };
OpenBackend ChooseOpenBackend(bool xdg_open);

enum class DragStart { kStart, kWindowClosed, kDragActive, kButtonReleased };
DragStart ChooseDragStart(bool window, bool dragging, bool button_down);

enum class RevealBackend { kPortal, kFileManager, kOpenFolder, kUnavailable };
struct RevealProbe {
  bool portal = false;
  bool file_manager = false;
  bool xdg_open = false;
  bool folder_handler = false;
};
RevealBackend ChooseRevealBackend(const RevealProbe& probe);

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
std::string MountTop(const std::string& path);

struct FolderCheck {
  bool ok = false;
  std::string code;
  std::string message;
};
FolderCheck CheckDownloadFolder(const std::string& path);
TrashOutcome MoveToTopdirTrash(const std::string& path, const std::string& topdir, uid_t uid,
                               std::time_t now);

}

#endif
