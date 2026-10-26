#ifndef ARDIA_LINUX_AC_LINUX_DOCUMENTS_H_
#define ARDIA_LINUX_AC_LINUX_DOCUMENTS_H_

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "base/values.h"

namespace ardia {

inline constexpr char kLinuxLiveDocument[] = "live.json";
inline constexpr char kLinuxLiveEnvelope[] = "live.canonical.json";
inline constexpr size_t kLinuxLiveMaxBytes = 8 * 1024 * 1024;
inline constexpr size_t kLinuxLiveMaxOperations = 2048;

struct LinuxDocumentRead {
  enum class State { kMissing, kFound, kError };
  State state = State::kMissing;
  base::DictValue value;
};

struct LinuxDocumentResult {
  std::string code;
  std::string message;
  base::DictValue value;
  bool ok() const { return code.empty(); }
};

struct LinuxDocumentClient {
  std::string id;
  std::string scope;
  bool privileged = false;
  bool regular = false;
};

class LinuxDocuments {
 public:
  using Read = std::function<LinuxDocumentRead(std::string_view)>;
  using Write = std::function<bool(const base::DictValue&)>;
  using Changed = std::function<void(base::DictValue)>;
  LinuxDocuments(std::string scope, std::string epoch, Read read, Write write);
  LinuxDocumentResult Watch(const LinuxDocumentClient& client, Changed changed);
  LinuxDocumentResult Commit(const LinuxDocumentClient& client,
                             const base::DictValue& request);
  LinuxDocumentResult Flush(const LinuxDocumentClient& client,
                            const base::DictValue& request);
  void Unwatch(const LinuxDocumentClient& client);
  LinuxDocumentResult Seal(const LinuxDocumentClient& client,
                           const std::string& close_generation, int watermark);
  LinuxDocumentResult CancelSeal(const LinuxDocumentClient& client,
                                 const std::string& close_generation);
  static bool Reserved(std::string_view name);

 private:
  LinuxDocumentResult Load();
  LinuxDocumentResult ReadCurrent();
  bool Authorized(const LinuxDocumentClient& client) const;
  base::DictValue Snapshot(const std::string& client) const;
  std::string scope_;
  std::string epoch_;
  Read read_;
  Write write_;
  base::DictValue envelope_;
  bool loaded_ = false;
  std::map<std::string, Changed> watchers_;
  std::map<std::string, std::pair<std::string, int>> seals_;
};

}

#endif
