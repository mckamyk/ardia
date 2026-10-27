#include "ardia/linux/ac_linux_downloads.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace ardia::linux_downloads {
namespace {

bool Hex(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool Unreserved(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
}

std::string Escape(std::string_view path) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string escaped;
  escaped.reserve(path.size());
  for (const unsigned char c : path) {
    if (Unreserved(c)) {
      escaped.push_back(static_cast<char>(c));
    } else {
      escaped.push_back('%');
      escaped.push_back(kDigits[c >> 4]);
      escaped.push_back(kDigits[c & 15]);
    }
  }
  return escaped;
}

bool ControlRequest(std::string_view verb) {
  return verb == "cancel" || verb == "pause" || verb == "resume" || verb == "keep" ||
         verb == "remove";
}

bool MakeDirectories(const std::string& path) {
  if (path.empty() || path[0] != '/') return false;
  for (size_t next = path.find('/', 1);; next = path.find('/', next + 1)) {
    const std::string prefix = next == std::string::npos ? path : path.substr(0, next);
    if (mkdir(prefix.c_str(), 0700) != 0 && errno != EEXIST) return false;
    struct stat info;
    if (stat(prefix.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return false;
    if (next == std::string::npos) return true;
  }
}

TrashOutcome Failure(std::string code, std::string message) {
  return {false, std::move(code), std::move(message), {}};
}

std::string Candidate(const std::string& name, int attempt) {
  if (attempt == 1) return name;
  const size_t dot = name.rfind('.');
  const bool extension = dot != std::string::npos && dot > 0;
  const std::string stem = extension ? name.substr(0, dot) : name;
  const std::string suffix = extension ? name.substr(dot) : std::string();
  return stem + "." + std::to_string(attempt) + suffix;
}

bool WriteAll(int fd, const std::string& text) {
  size_t written = 0;
  while (written < text.size()) {
    const ssize_t count = write(fd, text.data() + written, text.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    written += static_cast<size_t>(count);
  }
  return true;
}

}

bool ValidDownloadId(std::string_view id) {
  if (id.size() != 36) return false;
  for (size_t index = 0; index < id.size(); ++index) {
    const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
    if (dash ? id[index] != '-' : !Hex(id[index])) return false;
  }
  return true;
}

bool ValidLocalPath(std::string_view path) {
  if (path.empty() || path.size() > kMaxPathBytes || path[0] != '/') return false;
  if (path.find('\0') != std::string_view::npos) return false;
  if (path == "/") return true;
  if (path.back() == '/') return false;
  size_t start = 1;
  while (start <= path.size()) {
    const size_t end = std::min(path.find('/', start), path.size());
    const std::string_view component = path.substr(start, end - start);
    if (component.empty() || component == "." || component == "..") return false;
    start = end + 1;
  }
  return true;
}

std::string ValidateRequest(std::string_view name, const base::DictValue& input) {
  if (!name.starts_with("downloads.")) return {};
  const std::string_view verb = name.substr(10);
  if (ControlRequest(verb)) {
    const std::string* id = input.FindString("id");
    if (input.size() != 1 || !id || !ValidDownloadId(*id)) return "A valid download id is required";
    return {};
  }
  if (verb == "list" || verb == "settings") {
    return input.size() == 0 ? std::string() : "This download request takes no arguments";
  }
  if (verb == "clear") {
    const base::Value* keep = input.Find("keep");
    const base::Value* before = input.Find("before");
    if (input.size() != 2 || !keep || !before || !keep->is_list() || !(before->is_int() || before->is_double()))
      return "Clearing downloads requires the kept ids and a cutoff";
    if (keep->GetList().size() > kMaxKeptDownloads) return "Too many kept downloads";
    for (const auto& id : keep->GetList()) {
      if (!id.is_string() || !ValidDownloadId(id.GetString())) return "Kept downloads need valid ids";
    }
    const double cutoff = before->GetDouble();
    if (!std::isfinite(cutoff) || cutoff < 0) return "The clearing cutoff must be a time";
    return {};
  }
  if (verb == "setSettings") {
    const base::Value* folder = input.Find("folder");
    const base::Value* ask = input.Find("askWhereToSave");
    if (input.size() != static_cast<size_t>(folder != nullptr) + static_cast<size_t>(ask != nullptr))
      return "Unsupported download setting";
    if (folder && (!folder->is_string() || !ValidLocalPath(folder->GetString())))
      return "The download folder must be an absolute local path";
    if (ask && !ask->is_bool()) return "askWhereToSave must be a boolean";
    return {};
  }
  return {};
}

std::string FileUrl(std::string_view path) {
  return "file://" + Escape(path);
}

std::string BaseName(std::string_view path) {
  const size_t slash = path.rfind('/');
  return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::optional<base::ListValue> FilterSnapshot(const base::Value& reply) {
  if (!reply.is_dict()) return std::nullopt;
  const base::Value* downloads = reply.GetDict().Find("downloads");
  if (!downloads || !downloads->is_list()) return std::nullopt;
  base::ListValue kept;
  for (const auto& entry : downloads->GetList()) {
    if (!entry.is_dict()) continue;
    const std::string* id = entry.GetDict().FindString("id");
    const base::Value* off_the_record = entry.GetDict().Find("offTheRecord");
    if (!id || !ValidDownloadId(*id)) continue;
    if (off_the_record && (!off_the_record->is_bool() || off_the_record->GetBool())) continue;
    kept.Append(entry.Clone());
  }
  return kept;
}

std::vector<std::string> KeptIds(const base::DictValue& input) {
  std::vector<std::string> ids;
  const base::Value* keep = input.Find("keep");
  if (!keep || !keep->is_list()) return ids;
  for (const auto& id : keep->GetList()) {
    if (id.is_string() && ValidDownloadId(id.GetString())) ids.push_back(id.GetString());
  }
  return ids;
}

double ClearCutoff(const base::DictValue& input) {
  const base::Value* before = input.Find("before");
  return before && (before->is_int() || before->is_double()) ? before->GetDouble() : 0;
}

bool IconPathAllowed(std::string_view path, const std::vector<std::string>& targets) {
  if (path.empty() || path.size() > kMaxPathBytes || path.find('\0') != std::string_view::npos) return false;
  if (path.find('/') == std::string_view::npos) return true;
  return PathListed(path, targets);
}

std::vector<bool> ListedPaths(const std::vector<std::string>& paths, const std::vector<std::string>& targets) {
  std::vector<bool> listed;
  listed.reserve(paths.size());
  for (const auto& path : paths) listed.push_back(PathListed(path, targets));
  return listed;
}

OpenBackend ChooseOpenBackend(bool xdg_open) {
  return xdg_open ? OpenBackend::kDesktop : OpenBackend::kUnavailable;
}

DragStart ChooseDragStart(bool window, bool dragging, bool button_down) {
  if (!window) return DragStart::kWindowClosed;
  if (dragging) return DragStart::kDragActive;
  return button_down ? DragStart::kStart : DragStart::kButtonReleased;
}

RevealBackend ChooseRevealBackend(bool session_bus, bool xdg_open) {
  return session_bus || xdg_open ? RevealBackend::kDesktop : RevealBackend::kUnavailable;
}

bool PathListed(std::string_view path, const std::vector<std::string>& known) {
  if (!ValidLocalPath(path)) return false;
  for (const auto& candidate : known) {
    if (candidate == path) return true;
  }
  return false;
}

std::string TrashDirectory(const char* xdg_data_home, const char* home) {
  if (xdg_data_home && ValidLocalPath(xdg_data_home)) return std::string(xdg_data_home) + "/Trash";
  if (home && ValidLocalPath(home)) {
    return std::string(home == std::string_view("/") ? "" : home) + "/.local/share/Trash";
  }
  return {};
}

TrashOutcome MoveToTrash(const std::string& path, const std::string& trash_directory,
                         std::time_t now) {
  if (!ValidLocalPath(path) || path == "/") return Failure("ERR_FILE_PATH", "The file path is invalid");
  struct stat source;
  if (lstat(path.c_str(), &source) != 0) {
    return errno == ENOENT ? Failure("ERR_FILE_MISSING", "The file no longer exists")
                           : Failure("ERR_FILE", "The file cannot be read");
  }
  if (!S_ISREG(source.st_mode)) return Failure("ERR_FILE_TYPE", "Only regular files can be moved to the trash");
  if (trash_directory.empty() || !ValidLocalPath(trash_directory))
    return Failure("ERR_TRASH_UNAVAILABLE", "No desktop trash directory is configured");
  const std::string files = trash_directory + "/files";
  const std::string info = trash_directory + "/info";
  if (!MakeDirectories(files) || !MakeDirectories(info))
    return Failure("ERR_TRASH_UNAVAILABLE", "The desktop trash directory cannot be created");
  struct stat target;
  if (stat(files.c_str(), &target) != 0)
    return Failure("ERR_TRASH_UNAVAILABLE", "The desktop trash directory cannot be read");
  if (target.st_dev != source.st_dev)
    return Failure("ERR_TRASH_UNAVAILABLE", "The file is on a different file system than the desktop trash");
  struct tm local;
  char date[32];
  if (!localtime_r(&now, &local) || !std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &local))
    return Failure("ERR_TRASH", "The deletion date cannot be recorded");
  const std::string record = "[Trash Info]\nPath=" + Escape(path) + "\nDeletionDate=" + date + "\n";
  const std::string name = BaseName(path);
  for (int attempt = 1; attempt <= 1000; ++attempt) {
    const std::string candidate = Candidate(name, attempt);
    if (candidate.size() + 10 > 255) return Failure("ERR_TRASH", "The file name is too long for the trash");
    const std::string info_path = info + "/" + candidate + ".trashinfo";
    const int fd = open(info_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
      if (errno == EEXIST) continue;
      return Failure("ERR_TRASH", "The trash record cannot be created");
    }
    const bool recorded = WriteAll(fd, record) && fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    if (!recorded || !closed) {
      unlink(info_path.c_str());
      return Failure("ERR_TRASH", "The trash record cannot be written");
    }
    const std::string destination = files + "/" + candidate;
    if (link(path.c_str(), destination.c_str()) != 0) {
      const int error = errno;
      unlink(info_path.c_str());
      if (error == EEXIST) continue;
      return error == EXDEV ? Failure("ERR_TRASH_UNAVAILABLE", "The file is on a different file system than the desktop trash")
                            : Failure("ERR_TRASH", "The file cannot be moved to the trash");
    }
    if (unlink(path.c_str()) != 0) {
      unlink(destination.c_str());
      unlink(info_path.c_str());
      return Failure("ERR_TRASH", "The original file cannot be removed");
    }
    return {true, {}, {}, destination};
  }
  return Failure("ERR_TRASH", "The trash has too many files with this name");
}

}
