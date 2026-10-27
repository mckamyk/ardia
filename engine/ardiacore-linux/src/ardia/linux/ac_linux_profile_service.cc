#include "ardia/linux/ac_linux_profile_service.h"

#include <functional>
#include <memory>
#include <utility>

#include "base/check.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "content/public/browser/browser_thread.h"

namespace ardiacore {
namespace {

using linux_profiles::ProfileError;
using Registry = linux_profiles::ProfileRegistry<Profile>;

class ChromeProfiles : public Registry::Backend, public ProfileObserver {
 public:
  Registry& registry() { return registry_; }

  void Watch(Profile* profile) {
    if (profile && !observations_.IsObservingSource(profile)) observations_.AddObservation(profile);
  }

  void Load(const std::string& directory, std::function<void(Profile*)> done) override {
    ProfileManager* manager = g_browser_process ? g_browser_process->profile_manager() : nullptr;
    Profile* app = registry_.Default();
    if (!manager || !app || directory.empty()) return done(nullptr);
    const base::FilePath user_data = app->GetPath().DirName();
    const base::FilePath path = user_data.AppendASCII(directory);
    if (user_data != manager->user_data_dir() || path.DirName() != user_data) return done(nullptr);
    manager->CreateProfileAsync(
        path, base::BindOnce(
                  [](ChromeProfiles* self, std::function<void(Profile*)> done, Profile* profile) {
                    if (profile) self->Watch(profile);
                    done(profile);
                  },
                  base::Unretained(this), std::move(done)));
  }

  Profile* OffTheRecord(Profile* original) override {
    if (!Usable(original) || original->IsOffTheRecord()) return nullptr;
    Profile* otr = original->GetPrimaryOTRProfile(true);
    if (otr) Watch(otr);
    return otr;
  }

  bool Usable(Profile* profile) const override {
    return profile && !profile->ShutdownStarted();
  }

  bool OffTheRecordProfile(Profile* profile) const override {
    return profile && profile->IsOffTheRecord();
  }

 private:
  void OnProfileWillBeDestroyed(Profile* profile) override {
    registry_.Forget(profile);
    if (observations_.IsObservingSource(profile)) observations_.RemoveObservation(profile);
  }

  Registry registry_{*this};
  base::ScopedMultiSourceObservation<Profile, ProfileObserver> observations_{this};
};

ChromeProfiles& Profiles() {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  static base::NoDestructor<ChromeProfiles> profiles;
  return *profiles;
}

}

bool SetLinuxAppProfile(Profile* profile) {
  if (!Profiles().registry().SetDefault(profile)) return false;
  Profiles().Watch(profile);
  return true;
}

Profile* LinuxProfile(std::string_view id) {
  return Profiles().registry().Lookup(id);
}

bool LinuxProfileHosts(std::string_view id, const Profile* window_profile) {
  return Profiles().registry().Hosts(id, window_profile);
}

void ResolveLinuxProfile(std::string_view id, LinuxProfileDone done) {
  auto once = std::make_shared<LinuxProfileDone>(std::move(done));
  Profiles().registry().Resolve(id, [once](Profile* profile, ProfileError error) {
    if (*once) std::move(*once).Run(profile, error);
  });
}

std::optional<std::string> LinuxProfileName(const Profile* profile) {
  return Profiles().registry().NameOf(profile);
}

std::optional<std::string> LinuxProfileNameForPath(std::string_view path) {
  ProfileManager* manager = g_browser_process ? g_browser_process->profile_manager() : nullptr;
  if (!manager || path.empty()) return std::nullopt;
  Profile* profile = manager->GetProfileByPath(base::FilePath(path));
  if (!profile || profile->IsOffTheRecord()) return std::nullopt;
  return LinuxProfileName(profile);
}

bool ReleaseLinuxProfile(std::string_view id) {
  return Profiles().registry().Release(id);
}

}
