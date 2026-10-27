#include "ardia/linux/ac_linux_power.h"

#include <string>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "components/dbus/thread_linux/dbus_thread_linux.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "dbus/property.h"

namespace ardiacore {
namespace {

constexpr base::TimeDelta kMemoryInterval = base::Seconds(30);
constexpr base::TimeDelta kAnswerDeadline = base::Seconds(2);
constexpr char kUPower[] = "org.freedesktop.UPower";
constexpr char kUPowerPath[] = "/org/freedesktop/UPower";
constexpr char kDisplayPath[] = "/org/freedesktop/UPower/devices/DisplayDevice";
constexpr char kDevice[] = "org.freedesktop.UPower.Device";

std::pair<uint64_t, uint64_t> ReadMemory() {
  return {base::SysInfo::AmountOfTotalPhysicalMemory().InBytes(),
          base::SysInfo::AmountOfAvailablePhysicalMemory().InBytes()};
}

}

class LinuxPowerMonitor::Properties : public dbus::PropertySet {
 public:
  Properties(dbus::ObjectProxy* proxy, const std::string& interface, base::RepeatingClosure changed,
             base::OnceClosure answered)
      : dbus::PropertySet(proxy, interface,
                          base::BindRepeating(&Properties::PropertyChanged, base::Unretained(this))),
        changed_(std::move(changed)),
        answered_(std::move(answered)) {
    RegisterProperty("OnBattery", &on_battery);
    RegisterProperty("Type", &type);
    RegisterProperty("IsPresent", &is_present);
    RegisterProperty("Percentage", &percentage);
    RegisterProperty("ActiveProfile", &active_profile);
    proxy->SetNameOwnerChangedCallback(base::BindRepeating(&Properties::OwnerChanged, base::Unretained(this)));
    ConnectSignals();
    GetAll();
  }

  void OnGetAll(dbus::Response* response) override {
    dbus::PropertySet::OnGetAll(response);
    changed_.Run();
    if (answered_) std::move(answered_).Run();
  }

  dbus::Property<bool> on_battery;
  dbus::Property<uint32_t> type;
  dbus::Property<bool> is_present;
  dbus::Property<double> percentage;
  dbus::Property<std::string> active_profile;

 private:
  void PropertyChanged(const std::string&) { changed_.Run(); }

  void OwnerChanged(const std::string&, const std::string& owner) {
    if (!owner.empty()) return GetAll();
    on_battery.set_valid(false);
    type.set_valid(false);
    is_present.set_valid(false);
    percentage.set_valid(false);
    active_profile.set_valid(false);
    changed_.Run();
  }

  base::RepeatingClosure changed_;
  base::OnceClosure answered_;
};

LinuxPowerMonitor& LinuxPowerMonitor::Get() {
  static LinuxPowerMonitor* monitor = new LinuxPowerMonitor();
  return *monitor;
}

LinuxPowerMonitor::LinuxPowerMonitor()
    : runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})) {}

LinuxPowerMonitor::~LinuxPowerMonitor() = default;

void LinuxPowerMonitor::Current(base::OnceCallback<void(base::DictValue)> done) {
  Start();
  if (tracker_.Ready() && tracker_.state()) return std::move(done).Run(tracker_.state()->Clone());
  waiting_.push_back(std::move(done));
}

base::CallbackListSubscription LinuxPowerMonitor::Subscribe(Listener listener) {
  Start();
  return listeners_.Add(std::move(listener));
}

void LinuxPowerMonitor::Start() {
  if (std::exchange(started_, true)) return;
  memory_timer_.Start(FROM_HERE, kMemoryInterval,
                      base::BindRepeating(&LinuxPowerMonitor::SampleMemory, weak_factory_.GetWeakPtr()));
  answer_deadline_.Start(FROM_HERE, kAnswerDeadline, base::BindOnce([](base::WeakPtr<LinuxPowerMonitor> monitor) {
    if (!monitor) return;
    monitor->tracker_.PowerAnswered();
    monitor->Publish();
  }, weak_factory_.GetWeakPtr()));
  SampleMemory();
  Watch();
}

void LinuxPowerMonitor::Watch() {
  bus_ = dbus_thread_linux::GetSharedSystemBus();
  auto changed = base::BindRepeating(&LinuxPowerMonitor::PropertiesChanged, weak_factory_.GetWeakPtr());
  auto answered = [this] { return base::BindOnce(&LinuxPowerMonitor::PropertiesAnswered, weak_factory_.GetWeakPtr()); };
  auto watch = [&](const char* service, const char* path, const char* interface) {
    ++unanswered_;
    return std::make_unique<Properties>(bus_->GetObjectProxy(service, dbus::ObjectPath(path)), interface, changed, answered());
  };
  upower_ = watch(kUPower, kUPowerPath, kUPower);
  display_ = watch(kUPower, kDisplayPath, kDevice);
  for (const auto& [service, path, interface] : {
           std::tuple{"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles",
                      "org.freedesktop.UPower.PowerProfiles"},
           std::tuple{"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles"}}) {
    profiles_.push_back(watch(service, path, interface));
  }
}

void LinuxPowerMonitor::SampleMemory() {
  if (std::exchange(sampling_, true)) return;
  runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&ReadMemory),
      base::BindOnce(&LinuxPowerMonitor::MemorySampled, weak_factory_.GetWeakPtr()));
}

void LinuxPowerMonitor::MemorySampled(std::pair<uint64_t, uint64_t> memory) {
  sampling_ = false;
  tracker_.SetMemory(memory.first, memory.second);
  Publish();
}

void LinuxPowerMonitor::PropertiesChanged() {
  if (!upower_ || !display_) return;
  LinuxPowerSample power;
  if (upower_->on_battery.is_valid()) {
    power.upower = true;
    power.on_battery = upower_->on_battery.value();
    if (display_->type.is_valid()) power.device_type = display_->type.value();
    if (display_->is_present.is_valid()) power.is_present = display_->is_present.value();
    if (display_->percentage.is_valid()) power.percentage = display_->percentage.value();
  }
  for (const auto& profile : profiles_) {
    if (!profile->active_profile.is_valid()) continue;
    power.power_profile = profile->active_profile.value();
    break;
  }
  tracker_.SetPower(power);
  Publish();
}

void LinuxPowerMonitor::PropertiesAnswered() {
  if (unanswered_ && --unanswered_) return;
  answer_deadline_.Stop();
  tracker_.PowerAnswered();
  Publish();
}

void LinuxPowerMonitor::Publish() {
  if (!tracker_.Ready()) return;
  auto changed = tracker_.Take();
  if (!tracker_.state()) return;
  auto waiting = std::move(waiting_);
  waiting_.clear();
  for (auto& done : waiting) std::move(done).Run(tracker_.state()->Clone());
  if (changed) listeners_.Notify(*changed);
}

}
