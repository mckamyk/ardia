#include "ardia/linux/ac_linux_private_services.h"

#include <cassert>
#include <cstdio>

using namespace ardiacore::linux_private_services;
using ardiacore::linux_profiles::ProfileError;

int main() {
  for (const char* served : {"sites.settings", "sites.setSetting", "sites.origins", "sites.reset", "sites.clearData",
                             "sites.zoomLevels", "sites.setZoom"}) {
    assert(ServesPrivateSession(served));
  }
  for (const char* refused : {"history.query", "history.add", "bookmarks.tree", "favicons.get", "downloads.settings",
                              "profiles.clearBrowsingData", "extensions.list", "passwords.list", "documents.read", ""}) {
    assert(!ServesPrivateSession(refused));
    assert(RoutePrivateCall(refused, {true, false, true}) == ProfileError::kPrivateService);
  }
  assert(RoutePrivateCall("sites.settings", {true, false, true}) == ProfileError::kNone);
  assert(RoutePrivateCall("sites.setZoom", {true, false, true}) == ProfileError::kNone);
  assert(RoutePrivateCall("sites.settings", {false, false, true}) == ProfileError::kWindowMismatch);
  assert(RoutePrivateCall("sites.settings", {false, false, false}) == ProfileError::kWindowMismatch);
  assert(RoutePrivateCall("sites.settings", {true, false, false}) == ProfileError::kWindowMismatch);
  assert(RoutePrivateCall("sites.settings", {true, true, true}) == ProfileError::kPrivateEnding);
  assert(RoutePrivateCall("sites.zoomLevels", {true, true, false}) == ProfileError::kPrivateEnding);
  assert(RoutePrivateCall("sites.zoomLevels", {false, true, true}) == ProfileError::kWindowMismatch);
  assert(EngineFailure("no private session").code == "ERR_PROFILE_ENDING");
  assert(EngineFailure("not for a private session").code == "ERR_PROFILE_PRIVATE");
  assert(EngineFailure("private profile").code == "ERR_PROFILE_PRIVATE");
  assert(EngineFailure("profile not loaded").code == "ERR_ENGINE");
  assert(EngineFailure("profile not loaded").message == "profile not loaded");
  assert(EngineFailure("").message == "Engine service failed");
  static_assert(ServesPrivateSession("sites.origins") && !ServesPrivateSession("history.query"));
  std::puts("private services scenarios passed");
}
