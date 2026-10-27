#include "ardia/linux/ac_linux_sync_vault.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <set>
#include <utility>

namespace ardia::linux_sync {
namespace {

constexpr unsigned kRenameNoReplace = 1;
constexpr mode_t kDirectoryMode = 0700;
constexpr mode_t kFileMode = 0600;

std::string Join(const std::string& directory, std::string_view name) {
  std::string path = directory;
  if (path.empty() || path.back() != '/') path.push_back('/');
  path.append(name);
  return path;
}

std::string ErrorText(std::string_view what, int error) {
  return std::string(what) + ": " + std::strerror(error);
}

bool IsDirectoryNoFollow(const std::string& path) {
  struct stat info;
  return lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool EnsureDirectory(const std::string& path, bool follow) {
  struct stat info;
  if ((follow ? stat(path.c_str(), &info) : lstat(path.c_str(), &info)) == 0) return S_ISDIR(info.st_mode);
  if (errno != ENOENT) return false;
  if (mkdir(path.c_str(), kDirectoryMode) == 0) return true;
  return errno == EEXIST && (follow ? DirectoryExists(path) : IsDirectoryNoFollow(path));
}

bool EnsureFolder(const std::string& folder) {
  std::string partial;
  size_t at = 0;
  while (at < folder.size()) {
    const size_t next = folder.find('/', at + 1);
    partial = folder.substr(0, next == std::string::npos ? folder.size() : next);
    at = next == std::string::npos ? folder.size() : next;
    if (partial.empty() || partial == "/") continue;
    if (!EnsureDirectory(partial, true)) return false;
  }
  return DirectoryExists(folder);
}

bool SyncDirectory(const std::string& path) {
  const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return false;
  const bool ok = fsync(fd) == 0 || errno == EINVAL || errno == EROFS;
  close(fd);
  return ok;
}

bool WriteAll(int fd, const Bytes& data) {
  size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = write(fd, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    written += static_cast<size_t>(n);
  }
  return true;
}

std::optional<Bytes> ReadRegularFile(const std::string& path, size_t limit) {
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return std::nullopt;
  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || static_cast<uint64_t>(info.st_size) > limit) {
    close(fd);
    return std::nullopt;
  }
  Bytes data(static_cast<size_t>(info.st_size));
  size_t got = 0;
  while (got < data.size()) {
    const ssize_t n = read(fd, data.data() + got, data.size() - got);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    got += static_cast<size_t>(n);
  }
  close(fd);
  if (got != data.size()) return std::nullopt;
  return data;
}

std::vector<std::string> Names(const std::string& directory) {
  std::vector<std::string> names;
  DIR* dir = opendir(directory.c_str());
  if (!dir) return names;
  while (const dirent* entry = readdir(dir)) {
    const std::string_view name(entry->d_name);
    if (name != "." && name != "..") names.emplace_back(name);
  }
  closedir(dir);
  std::sort(names.begin(), names.end());
  return names;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

void RemoveStaleStaging(const std::string& directory, int64_t now) {
  for (const auto& name : Names(directory)) {
    if (!StartsWith(name, kStagingPrefix)) continue;
    const std::string path = Join(directory, name);
    struct stat info;
    if (lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && now - info.st_mtime >= kStaleStagingSeconds)
      unlink(path.c_str());
  }
}

int RenameNoReplace(const std::string& from, const std::string& to) {
  if (syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), kRenameNoReplace) == 0) return 0;
  if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP && errno != EOPNOTSUPP) return errno;
  if (link(from.c_str(), to.c_str()) == 0) {
    unlink(from.c_str());
    return 0;
  }
  if (errno == EEXIST) return EEXIST;
  struct stat info;
  if (lstat(to.c_str(), &info) == 0) return EEXIST;
  return rename(from.c_str(), to.c_str()) == 0 ? 0 : errno;
}

bool RemoveTree(const std::string& path) {
  struct stat info;
  if (lstat(path.c_str(), &info) != 0) return errno == ENOENT;
  if (!S_ISDIR(info.st_mode)) return unlink(path.c_str()) == 0 || errno == ENOENT;
  bool ok = true;
  for (const auto& name : Names(path)) ok = RemoveTree(Join(path, name)) && ok;
  return (rmdir(path.c_str()) == 0 || errno == ENOENT) && ok;
}

}

int64_t NowSeconds() {
  return static_cast<int64_t>(std::time(nullptr));
}

bool ValidFolderPath(std::string_view path) {
  return !path.empty() && path.size() <= kMaxFolderPathBytes && path.front() == '/' &&
         path.find('\0') == std::string_view::npos;
}

bool DirectoryExists(const std::string& path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool HasSyncData(const std::string& folder) {
  const auto names = Names(folder);
  return std::any_of(names.begin(), names.end(), [](const std::string& name) { return IsTag(name); });
}

FolderFacts InspectFolder(const std::string& path) {
  FolderFacts facts;
  facts.exists = ValidFolderPath(path) && DirectoryExists(path);
  facts.writable = facts.exists && access(path.c_str(), W_OK | X_OK) == 0;
  facts.has_sync_data = facts.exists && HasSyncData(path);
  return facts;
}

Vault::Vault(std::string folder, const SyncKeys& keys) : folder_(std::move(folder)), keys_(keys) {}

std::string Vault::ChainPath() const {
  return Join(folder_, keys_.chain_tag());
}

std::string Vault::ScopePath(std::string_view scope) const {
  return Join(ChainPath(), keys_.ScopeTag(scope));
}

bool Vault::ChainExists() const {
  return DirectoryExists(ChainPath());
}

VaultError Vault::CreateChain() const {
  if (!ValidFolderPath(folder_)) return {"The sync folder path isn't valid"};
  if (!EnsureFolder(folder_)) return {ErrorText("Couldn't create the sync folder", errno)};
  if (!EnsureDirectory(ChainPath(), false)) return {ErrorText("Couldn't create the sync data folder", errno)};
  SyncDirectory(folder_);
  return {};
}

VaultError Vault::Write(std::string_view scope, const Bytes& payload, std::string* id) const {
  *id = NewFileId();
  if (!IsTag(*id)) return {"Couldn't make a file name"};
  return WriteAs(scope, payload, *id);
}

VaultError Vault::WriteAs(std::string_view scope, const Bytes& payload, const std::string& id) const {
  if (!IsTag(id)) return {"Invalid sync file name"};
  if (!ValidFolderPath(folder_) || !DirectoryExists(folder_)) return {"The sync folder isn't available"};
  const std::string scope_tag = keys_.ScopeTag(scope);
  auto sealed = keys_.Seal(payload, scope_tag, id);
  if (!sealed) return {"Couldn't seal the sync file"};
  const std::string directory = ScopePath(scope);
  if (!EnsureDirectory(ChainPath(), false) || !EnsureDirectory(directory, false))
    return {ErrorText("Couldn't create the sync data folder", errno)};
  RemoveStaleStaging(directory, NowSeconds());
  uint8_t random[10];
  if (!RandomBytes(random, sizeof(random))) return {"Couldn't make a staging name"};
  const std::string staging = Join(directory, kStagingPrefix + Base32(random, sizeof(random)));
  const std::string destination = Join(directory, id + kFileExtension);
  const int fd = open(staging.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, kFileMode);
  if (fd < 0) return {ErrorText("Couldn't write a sync file", errno)};
  const bool written = WriteAll(fd, *sealed) && fsync(fd) == 0;
  const int write_error = errno;
  const bool closed = close(fd) == 0;
  if (!written || !closed) {
    unlink(staging.c_str());
    return {ErrorText("Couldn't write a sync file", written ? errno : write_error)};
  }
  if (const int error = RenameNoReplace(staging, destination); error != 0) {
    unlink(staging.c_str());
    return {ErrorText("Couldn't move a sync file into place", error)};
  }
  SyncDirectory(directory);
  return {};
}

Listing Vault::Read(std::string_view scope, const std::vector<std::string>& known_ids, int64_t now_seconds) const {
  Listing listing;
  const std::string directory = ScopePath(scope);
  if (!IsDirectoryNoFollow(directory)) return listing;
  const std::set<std::string, std::less<>> known(known_ids.begin(), known_ids.end());
  const std::string scope_tag = keys_.ScopeTag(scope);
  const auto names = Names(directory);
  std::set<std::string, std::less<>> whole;
  for (const auto& name : names) {
    if (EndsWith(name, kFileExtension)) whole.insert(name.substr(0, name.size() - (sizeof(kFileExtension) - 1)));
  }
  std::set<std::string, std::less<>> present;
  auto mark_present = [&](const std::string& id) {
    if (present.insert(id).second) listing.present.push_back(id);
  };
  const std::string syncthing_tail = std::string(kFileExtension) + kSyncthingSuffix;
  for (const auto& name : names) {
    if (StartsWith(name, kSyncthingPrefix) && EndsWith(name, syncthing_tail)) {
      const std::string id = name.substr(sizeof(kSyncthingPrefix) - 1,
                                         name.size() - (sizeof(kSyncthingPrefix) - 1) - syncthing_tail.size());
      if (!IsTag(id) || whole.contains(id) || present.contains(id)) continue;
      mark_present(id);
      if (!known.contains(id)) listing.pending.push_back(id);
      continue;
    }
    if (!EndsWith(name, kFileExtension)) continue;
    const std::string id = name.substr(0, name.size() - (sizeof(kFileExtension) - 1));
    if (!IsTag(id)) continue;
    mark_present(id);
    if (known.contains(id)) continue;
    const std::string path = Join(directory, name);
    struct stat info;
    const bool stated = lstat(path.c_str(), &info) == 0;
    auto data = stated && S_ISREG(info.st_mode) ? ReadRegularFile(path, kMaxSyncFileBytes) : std::nullopt;
    Bytes payload;
    if (data && keys_.Open(*data, scope_tag, id, &payload) == OpenError::kNone) {
      listing.files.push_back({id, std::move(payload)});
      continue;
    }
    const int64_t modified = stated ? static_cast<int64_t>(info.st_mtime) : now_seconds;
    listing.damaged.push_back({id, static_cast<double>(std::max<int64_t>(0, now_seconds - modified))});
  }
  return listing;
}

void Vault::Remove(std::string_view scope, const std::vector<std::string>& ids) const {
  const std::string directory = ScopePath(scope);
  if (!IsDirectoryNoFollow(directory)) return;
  for (const auto& id : ids) {
    if (!IsTag(id)) continue;
    unlink(Join(directory, id + kFileExtension).c_str());
  }
}

VaultError Vault::DeleteChain() const {
  const std::string chain = ChainPath();
  struct stat info;
  if (lstat(chain.c_str(), &info) != 0) return {};
  if (!RemoveTree(chain)) return {ErrorText("Couldn't delete the sync data", errno)};
  SyncDirectory(folder_);
  return {};
}

}
