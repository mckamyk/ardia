#include "ardia/linux/ac_linux_power.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/no_destructor.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"

namespace ardiacore {
namespace {

constexpr base::TimeDelta kPollInterval = base::Seconds(30);
constexpr int kCallTimeoutMs = 1000;
constexpr char kUPower[] = "org.freedesktop.UPower";
constexpr char kProperties[] = "org.freedesktop.DBus.Properties";

std::unique_ptr<dbus::Response> Property(dbus::Bus* bus, const char* service, const char* path,
                                         const char* interface, const char* name) {
  dbus::ObjectProxy* proxy = bus->GetObjectProxy(service, dbus::ObjectPath(path));
  if (!proxy) return nullptr;
  dbus::MethodCall call(kProperties, "Get");
  dbus::MessageWriter writer(&call);
  writer.AppendString(interface);
  writer.AppendString(name);
  auto response = proxy->CallMethodAndBlock(&call, kCallTimeoutMs);
  return response.has_value() ? std::move(response.value()) : nullptr;
}

template <class T, bool (dbus::MessageReader::*Pop)(T*)>
std::optional<T> Read(dbus::Bus* bus, const char* service, const char* path,
                      const char* interface, const char* name) {
  auto response = Property(bus, service, path, interface, name);
  if (!response) return std::nullopt;
  dbus::MessageReader reader(response.get());
  T value{};
  if (!(reader.*Pop)(&value)) return std::nullopt;
  return value;
}

dbus::Bus* SystemBus() {
  static base::NoDestructor<scoped_refptr<dbus::Bus>> bus([] {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    options.connection_type = dbus::Bus::PRIVATE;
    return base::MakeRefCounted<dbus::Bus>(options);
  }());
  return bus->get();
}

LinuxPowerSample SamplePower() {
  LinuxPowerSample sample;
  sample.physical_memory = base::SysInfo::AmountOfTotalPhysicalMemory().InBytes();
  sample.available_memory = base::SysInfo::AmountOfAvailablePhysicalMemory().InBytes();
  dbus::Bus* bus = SystemBus();
  if (!bus->Connect()) return sample;
  sample.on_battery = Read<bool, &dbus::MessageReader::PopVariantOfBool>(
      bus, kUPower, "/org/freedesktop/UPower", kUPower, "OnBattery");
  sample.upower = sample.on_battery.has_value();
  if (!sample.upower) return sample;
  constexpr char kDisplay[] = "/org/freedesktop/UPower/devices/DisplayDevice";
  constexpr char kDevice[] = "org.freedesktop.UPower.Device";
  sample.device_type = Read<uint32_t, &dbus::MessageReader::PopVariantOfUint32>(bus, kUPower, kDisplay, kDevice, "Type");
  sample.is_present = Read<bool, &dbus::MessageReader::PopVariantOfBool>(bus, kUPower, kDisplay, kDevice, "IsPresent");
  sample.percentage = Read<double, &dbus::MessageReader::PopVariantOfDouble>(bus, kUPower, kDisplay, kDevice, "Percentage");
  for (const auto& [service, path, interface] : {
           std::tuple{"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles",
                      "org.freedesktop.UPower.PowerProfiles"},
           std::tuple{"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles"}}) {
    sample.power_profile = Read<std::string, &dbus::MessageReader::PopVariantOfString>(
        bus, service, path, interface, "ActiveProfile");
    if (sample.power_profile) break;
  }
  return sample;
}

}

LinuxPowerMonitor& LinuxPowerMonitor::Get() {
  static LinuxPowerMonitor* monitor = new LinuxPowerMonitor();
  return *monitor;
}

LinuxPowerMonitor::LinuxPowerMonitor()
    : runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN})) {}

void LinuxPowerMonitor::Current(base::OnceCallback<void(base::DictValue)> done) {
  Start();
  if (state_) return std::move(done).Run(state_->Clone());
  waiting_.push_back(std::move(done));
}

base::CallbackListSubscription LinuxPowerMonitor::Subscribe(Listener listener) {
  Start();
  return listeners_.Add(std::move(listener));
}

void LinuxPowerMonitor::Start() {
  if (timer_.IsRunning()) return;
  timer_.Start(FROM_HERE, kPollInterval,
               base::BindRepeating(&LinuxPowerMonitor::Poll, weak_factory_.GetWeakPtr()));
  Poll();
}

void LinuxPowerMonitor::Poll() {
  if (polling_) return;
  polling_ = true;
  runner_->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&SamplePower),
      base::BindOnce(&LinuxPowerMonitor::Sampled, weak_factory_.GetWeakPtr()));
}

void LinuxPowerMonitor::Sampled(LinuxPowerSample sample) {
  polling_ = false;
  base::DictValue state = LinuxSystemState(sample);
  const bool changed = !state_ || *state_ != state;
  state_ = state.Clone();
  auto waiting = std::move(waiting_);
  waiting_.clear();
  for (auto& done : waiting) std::move(done).Run(state_->Clone());
  if (changed) listeners_.Notify(*state_);
}

}
