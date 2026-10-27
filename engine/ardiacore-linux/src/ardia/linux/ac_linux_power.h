#ifndef ARDIA_LINUX_AC_LINUX_POWER_H_
#define ARDIA_LINUX_AC_LINUX_POWER_H_

#include <optional>
#include <vector>

#include "ardia/linux/ac_linux_tab_policy.h"
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "base/values.h"

namespace ardiacore {

class LinuxPowerMonitor {
 public:
  using Listener = base::RepeatingCallback<void(const base::DictValue&)>;
  static LinuxPowerMonitor& Get();
  LinuxPowerMonitor(const LinuxPowerMonitor&) = delete;
  LinuxPowerMonitor& operator=(const LinuxPowerMonitor&) = delete;
  void Current(base::OnceCallback<void(base::DictValue)> done);
  base::CallbackListSubscription Subscribe(Listener listener);

 private:
  LinuxPowerMonitor();
  ~LinuxPowerMonitor() = default;
  void Start();
  void Poll();
  void Sampled(LinuxPowerSample sample);

  scoped_refptr<base::SequencedTaskRunner> runner_;
  std::optional<base::DictValue> state_;
  std::vector<base::OnceCallback<void(base::DictValue)>> waiting_;
  base::RepeatingCallbackList<void(const base::DictValue&)> listeners_;
  base::RepeatingTimer timer_;
  bool polling_ = false;
  base::WeakPtrFactory<LinuxPowerMonitor> weak_factory_{this};
};

}

#endif
