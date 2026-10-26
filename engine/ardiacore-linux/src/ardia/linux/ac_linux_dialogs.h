#ifndef ARDIA_LINUX_AC_LINUX_DIALOGS_H_
#define ARDIA_LINUX_AC_LINUX_DIALOGS_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"

namespace ardia {

struct LinuxDialogClient {
  std::string window_id;
  std::string profile_identity;
  std::string renderer_generation;
  bool authenticated_shell = false;
  bool default_regular_profile = false;
};

struct LinuxDialogSpec {
  std::string dialog_id;
  bool prompt = false;
  std::string title;
  std::string message;
  std::string confirm_title = "OK";
  std::string cancel_title = "Cancel";
  bool destructive = false;
  bool has_suppression = false;
  std::string suppression;
  std::string value;
  std::string placeholder;
};

struct LinuxDialogResponse {
  bool accepted = false;
  bool suppressed = false;
  std::string text;
  bool forced = false;
};

class LinuxDialogSurface {
 public:
  virtual ~LinuxDialogSurface() = default;
};

using LinuxDialogResponder = base::OnceCallback<void(LinuxDialogResponse)>;
using LinuxDialogPresenter = base::RepeatingCallback<std::unique_ptr<LinuxDialogSurface>(
    const LinuxDialogSpec&, LinuxDialogResponder)>;
using LinuxDialogCompletion = base::OnceCallback<void(base::DictValue)>;

class LinuxDialogController {
 public:
  static constexpr size_t kMaxPending = 16;

  LinuxDialogController(std::string window_id,
                        std::string profile_identity,
                        LinuxDialogPresenter presenter);
  ~LinuxDialogController();
  LinuxDialogController(const LinuxDialogController&) = delete;
  LinuxDialogController& operator=(const LinuxDialogController&) = delete;

  bool ReplaceRenderer(const std::string& generation);
  bool Begin(const LinuxDialogClient& client,
             const base::DictValue& request,
             LinuxDialogCompletion completion,
             std::string* error);
  bool Cancel(const LinuxDialogClient& client, const std::string& dialog_id);
  void RetireRenderer(const std::string& generation);
  void ParentDestroyed();
  size_t pending() const { return entries_.size(); }
  base::WeakPtr<LinuxDialogController> GetWeakPtr();

 private:
  struct Entry {
    LinuxDialogSpec spec;
    std::string generation;
    uint64_t sequence;
    LinuxDialogCompletion completion;
  };

  bool Authorized(const LinuxDialogClient& client) const;
  void Pump();
  void Complete(std::string id, uint64_t sequence, LinuxDialogResponse response);
  void CancelAll();

  const std::string window_id_;
  const std::string profile_identity_;
  LinuxDialogPresenter presenter_;
  std::string generation_;
  std::string last_generation_;
  bool parent_alive_ = true;
  bool canceling_ = false;
  bool pumping_ = false;
  uint64_t sequence_ = 0;
  uint64_t active_sequence_ = 0;
  std::map<std::string, Entry> entries_;
  std::unique_ptr<LinuxDialogSurface> surface_;
  base::WeakPtrFactory<LinuxDialogController> weak_factory_{this};
};

}

#endif
