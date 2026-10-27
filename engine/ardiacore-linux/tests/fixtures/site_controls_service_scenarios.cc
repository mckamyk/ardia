#include "ardia/linux/ac_linux_site_controls_service.h"

#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

bool (*g_ardia_restricted_component_extension)(const extensions::ExtensionId& extension_id) = nullptr;

namespace {

struct EngineCall {
  std::string service;
  std::string profile;
  std::string args;
};

std::vector<EngineCall>& EngineCalls() {
  static std::vector<EngineCall> calls;
  return calls;
}

std::map<std::pair<std::string, std::string>, std::string>& Exceptions() {
  static std::map<std::pair<std::string, std::string>, std::string> exceptions;
  return exceptions;
}

bool& ClearFails() {
  static bool fails = false;
  return fails;
}

std::string Reply(base::DictValue value) { return *base::WriteJson(base::Value(std::move(value))); }

}

void ac_engine_call(const char* name, const char* profile_dir, const char* args_json, ac_engine_reply_t reply, void* context) {
  EngineCalls().push_back({name, profile_dir, args_json});
  auto args = base::JSONReader::Read(args_json, 0);
  assert(args && args->is_dict());
  const base::DictValue& input = args->GetDict();
  const std::string service = name;
  const std::string origin = input.FindString("origin") ? *input.FindString("origin") : "";
  std::string json;
  if (service == "ac_site_settings_get") {
    base::DictValue settings;
    for (const char* type : {"camera", "microphone", "location", "notifications"}) {
      auto found = Exceptions().find({origin, type});
      settings.Set(type, base::DictValue().Set("value", found == Exceptions().end() ? "ask" : found->second)
                             .Set("isDefault", base::Value(found == Exceptions().end())));
    }
    json = Reply(base::DictValue().Set("settings", std::move(settings)));
  } else if (service == "ac_site_settings_set") {
    const std::string type = *input.FindString("type");
    const std::string value = *input.FindString("value");
    if (value == "default") Exceptions().erase({origin, type});
    else Exceptions()[{origin, type}] = value;
    json = Reply(base::DictValue());
  } else if (service == "ac_site_settings_reset") {
    std::erase_if(Exceptions(), [&](const auto& entry) { return entry.first.first == origin; });
    json = Reply(base::DictValue());
  } else if (service == "ac_site_settings_origins") {
    std::set<std::string> origins;
    for (const auto& [key, value] : Exceptions()) origins.insert(key.first);
    base::ListValue list;
    for (const auto& o : origins) list.Append(base::Value(o));
    json = Reply(base::DictValue().Set("origins", base::Value(std::move(list))));
  } else if (service == "ac_site_data_clear") {
    json = Reply(base::DictValue().Set("cookies", base::Value(2)).Set("storage", base::Value(true)));
  } else if (service == "ac_browsing_data_clear") {
    json = Reply(base::DictValue().Set("ok", base::Value(!ClearFails())));
  } else {
    json = Reply(base::DictValue().Set("error", "unknown service"));
  }
  base::PostTask(base::OnceClosure([reply, context, json]() { reply(context, json.c_str()); }));
}

namespace {

struct Result {
  bool done = false;
  bool ok = false;
  base::Value value;
  std::string code;
  std::string message;
};

std::shared_ptr<Result> Call(Profile* profile, const std::string& name, const std::string& args,
                             const std::string& requested = "default") {
  auto result = std::make_shared<Result>();
  base::Value transported(args);
  ardia::LinuxSiteControlsCall(profile, name, requested, &transported,
      ardia::SiteControlsDone([result](bool ok, base::Value value) {
        assert(!result->done);
        result->done = true;
        result->ok = ok;
        if (!ok) {
          result->code = *value.GetDict().FindString("code");
          result->message = *value.GetDict().FindString("message");
        }
        result->value = std::move(value);
      }));
  return result;
}

void Advance(int64_t ms) {
  for (int64_t step = 0; step < ms; step += 25) {
    base::FakeNowMs() += 25;
    base::RunTasks();
    base::FireTimers();
    base::RunTasks();
  }
}

std::shared_ptr<Result> Settle(std::shared_ptr<Result> result, int64_t limit_ms = 3000) {
  for (int64_t waited = 0; !result->done && waited < limit_ms; waited += 25) Advance(25);
  assert(result->done);
  return result;
}

const base::DictValue& Dict(const std::shared_ptr<Result>& result) {
  assert(result->ok && result->value.is_dict());
  return result->value.GetDict();
}

std::string ValueOf(const std::shared_ptr<Result>& settings, const char* type) {
  return *Dict(settings).FindDict(type)->FindString("value");
}

bool IsDefault(const std::shared_ptr<Result>& settings, const char* type) {
  return Dict(settings).FindDict(type)->Find("isDefault")->GetBool();
}

std::filesystem::path MakeBlocker(const std::filesystem::path& root, bool with_bridge = true) {
  const auto dir = root / "extensions" / "ublock";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "manifest.json") << R"({"manifest_version":2,"version":"1.75.0.22","version_name":"1.75.0","key":"x"})";
  if (with_bridge) std::ofstream(dir / "ardia-linux-bridge.html") << "<html></html>";
  return dir;
}

std::vector<base::Value>& Events() {
  static std::vector<base::Value> events;
  return events;
}

void SitePermissionsThroughTheService(Profile& profile, Profile& incognito) {
  const std::string origin = R"("origin":"https://camera.ardia.test")";
  auto before = Settle(Call(&profile, "sites.settings", "{" + origin + R"(,"engine":"chromium"})"));
  assert(ValueOf(before, "camera") == "ask" && IsDefault(before, "camera"));
  EngineCalls().clear();
  auto grant = Settle(Call(&profile, "sites.setSetting", "{" + origin + R"(,"type":"camera","value":"allow","engine":"chromium"})"));
  assert(grant->ok && grant->value.is_none());
  assert(EngineCalls().size() == 1 && EngineCalls()[0].service == "ac_site_settings_set");
  assert(EngineCalls()[0].profile == "/profiles/Default");
  assert(EngineCalls()[0].args.find("engine") == std::string::npos);
  auto granted = Settle(Call(&profile, "sites.settings", "{" + origin + "}"));
  assert(ValueOf(granted, "camera") == "allow" && !IsDefault(granted, "camera"));
  Settle(Call(&profile, "sites.setSetting", "{" + origin + R"(,"type":"notifications","value":"block"})"));
  auto origins = Settle(Call(&profile, "sites.origins", "{}"));
  assert(origins->ok && origins->value.GetList().size() == 1);
  auto revoke = Settle(Call(&profile, "sites.setSetting", "{" + origin + R"(,"type":"camera","value":"default"})"));
  assert(revoke->ok);
  auto revoked = Settle(Call(&profile, "sites.settings", "{" + origin + "}"));
  assert(IsDefault(revoked, "camera") && ValueOf(revoked, "notifications") == "block");
  Settle(Call(&profile, "sites.reset", "{" + origin + "}"));
  assert(IsDefault(Settle(Call(&profile, "sites.settings", "{" + origin + "}")), "notifications"));
  assert(Settle(Call(&profile, "sites.origins", "{}"))->value.GetList().size() == 0);
  auto cleared = Settle(Call(&profile, "sites.clearData", "{" + origin + "}"));
  assert(cleared->ok && Dict(cleared).Find("cookies")->GetInt() == 2);

  EngineCalls().clear();
  auto from_private = Settle(Call(&incognito, "sites.setSetting", "{" + origin + R"(,"type":"camera","value":"allow"})"));
  assert(!from_private->ok && from_private->code == "ERR_PROFILE_PRIVATE");
  auto private_id = Settle(Call(&profile, "sites.settings", "{" + origin + "}", "incognito:w1@default"));
  assert(private_id->code == "ERR_PROFILE_PRIVATE");
  auto isolated = Settle(Call(&profile, "sites.origins", "{}", "work"));
  assert(isolated->code == "ERR_PROFILE_UNAVAILABLE");
  auto bad = Settle(Call(&profile, "sites.setSetting", "{" + origin + R"(,"type":"camera","value":"forever"})"));
  assert(bad->code == "ERR_SITE_REQUEST");
  auto webkit = Settle(Call(&profile, "sites.settings", "{" + origin + R"(,"engine":"webkit"})"));
  assert(webkit->code == "ERR_UNKNOWN_CALL");
  auto not_json = Settle(Call(&profile, "sites.origins", "{"));
  assert(not_json->code == "ERR_ENGINE");
  auto none = std::make_shared<Result>();
  ardia::LinuxSiteControlsCall(nullptr, "sites.origins", "", nullptr, ardia::SiteControlsDone([none](bool ok, base::Value value) {
    none->done = true;
    none->ok = ok;
    none->code = *value.GetDict().FindString("code");
  }));
  assert(none->done && none->code == "ERR_PROFILE_UNAVAILABLE");
  assert(EngineCalls().empty());
}

void BrowsingDataThroughTheService(Profile& profile, Profile& incognito) {
  EngineCalls().clear();
  auto hour = Settle(Call(&profile, "profiles.clearBrowsingData", R"({"types":["siteData","cache"],"since":1761500000000})"));
  assert(hour->ok && hour->value.is_none());
  assert(EngineCalls().size() == 1 && EngineCalls()[0].service == "ac_browsing_data_clear");
  auto sent = base::JSONReader::Read(EngineCalls()[0].args, 0);
  assert(sent->GetDict().Find("types")->GetList().size() == 2 && sent->GetDict().Find("since")->GetDouble() == 1761500000000.0);
  auto all = Settle(Call(&profile, "profiles.clearBrowsingData", R"({"types":["history","siteData","cache","downloads"],"since":null})"));
  assert(all->ok);
  assert(!base::JSONReader::Read(EngineCalls()[1].args, 0)->GetDict().Find("since"));
  ClearFails() = true;
  auto partial = Settle(Call(&profile, "profiles.clearBrowsingData", R"({"types":["cache"],"since":null})"));
  assert(partial->code == "ERR_CLEAR_INCOMPLETE");
  ClearFails() = false;
  EngineCalls().clear();
  assert(Settle(Call(&profile, "profiles.clearBrowsingData", R"({"types":["passwords"]})"))->code == "ERR_SITE_REQUEST");
  assert(Settle(Call(&incognito, "profiles.clearBrowsingData", R"({"types":["cache"]})"))->code == "ERR_PROFILE_PRIVATE");
  assert(EngineCalls().empty());
}

void MissingBlocker(const std::filesystem::path& scratch) {
  base::FakeExeDir() = base::FilePath((scratch / "empty").string());
  Profile profile("/profiles/Missing");
  ardia::StartLinuxContentBlocker(&profile);
  base::RunTasks();
  auto state = Settle(Call(&profile, "contentBlocker.state", "{}"));
  assert(state->ok);
  const base::DictValue& stats = *Dict(state).FindDict("stats");
  assert(!stats.Find("ready")->GetBool() && *stats.FindString("error") == "the content blocker is not installed");
  auto set = Settle(Call(&profile, "contentBlocker.setEnabled", R"({"enabled":false})"));
  assert(!set->ok && set->code == "ERR_CONTENT_BLOCKER_UNAVAILABLE");
  assert(profile.loader.added.empty() && Loads().pages_created == 0);

  base::FakeExeDir() = base::FilePath(scratch.string());
  MakeBlocker(scratch / "nobridge", false);
  base::FakeExeDir() = base::FilePath((scratch / "nobridge").string());
  Profile stale("/profiles/NoBridge");
  ardia::StartLinuxContentBlocker(&stale);
  base::RunTasks();
  assert(Settle(Call(&stale, "contentBlocker.setListEnabled", R"({"id":"easylist","enabled":true})"))->code == "ERR_CONTENT_BLOCKER_UNAVAILABLE");
}

void WrongComponent(const std::filesystem::path& scratch) {
  MakeBlocker(scratch / "wrong");
  base::FakeExeDir() = base::FilePath((scratch / "wrong").string());
  Profile profile("/profiles/Wrong");
  profile.loader.id_for_path = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  auto pending = Call(&profile, "contentBlocker.isAllowed", R"({"host":"example.com"})");
  base::RunTasks();
  profile.system.ready_event.Signal();
  Settle(pending);
  assert(pending->code == "ERR_CONTENT_BLOCKER_UNAVAILABLE" && pending->message == "the content blocker didn't load");
  assert(profile.loader.removed.size() == 1 && profile.loader.removed[0] == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  assert(Loads().pages_created == 0);
}

void WorkingBlocker(const std::filesystem::path& scratch) {
  const auto dir = MakeBlocker(scratch / "working");
  base::FakeExeDir() = base::FilePath((scratch / "working").string());
  auto profile = std::make_unique<Profile>("/profiles/Default2");
  Profile incognito("/profiles/Default2", true, profile.get());
  ardia::StartLinuxContentBlocker(&incognito);
  ardia::StartLinuxContentBlocker(profile.get());
  ardia::StartLinuxContentBlocker(profile.get());
  base::RunTasks();
  assert(profile->loader.added.empty());
  auto early = Call(profile.get(), "contentBlocker.state", "{}");
  Advance(100);
  assert(!early->done && Loads().pages_created == 0);
  profile->system.ready_event.Signal();
  Settle(early);
  assert(profile->loader.added.size() == 1 && profile->loader.added[0] == std::filesystem::canonical(dir).string());
  assert(g_ardia_restricted_component_extension("fajbdaomknpkicfjcpipdmcpoamdmilh"));
  assert(!g_ardia_restricted_component_extension("mhjfbmdgcfjbbpaeojofohoefgiehjai"));
  assert(Loads().pages_created == 1 && Loads().live.size() == 1);
  content::WebContents* page = Loads().live[0];
  assert(page->hidden && page->view_type == 9 && page->extension_observer && page->profile == profile.get());
  assert(page->requested.spec() == "chrome-extension://fajbdaomknpkicfjcpipdmcpoamdmilh/ardia-linux-bridge.html");
  const base::DictValue& state = Dict(early);
  assert(state.Find("enabled")->GetBool() && *state.FindString("version") == "1.75.0");
  assert(state.FindDict("stats")->Find("ready")->GetBool() && state.FindDict("stats")->Find("networkFilters")->GetInt() == 300);
  assert(state.Find("lists")->GetList().size() == 3);

  Events().clear();
  auto allow = Settle(Call(profile.get(), "contentBlocker.setAllowed", R"({"host":"Example.COM","allowed":true})"));
  assert(allow->ok && allow->value.is_none());
  Advance(200);
  assert(Events().size() == 1 && Events()[0].GetDict().Find("ready")->GetBool());
  assert(Settle(Call(profile.get(), "contentBlocker.isAllowed", R"({"host":"news.example.com"})"))->value.GetBool());
  assert(!Settle(Call(&incognito, "contentBlocker.isAllowed", R"({"host":"other.test"})"))->value.GetBool());
  auto hosts = Settle(Call(profile.get(), "contentBlocker.state", "{}"));
  assert(Dict(hosts).Find("allowedHosts")->GetList().size() == 1);
  assert(Dict(hosts).Find("allowedHosts")->GetList()[0].GetString() == "example.com");
  auto bad_host = Settle(Call(profile.get(), "contentBlocker.setAllowed", R"({"host":"/^(?:ardia-blocking-off|)/","allowed":true})"));
  assert(bad_host->code == "ERR_CONTENT_BLOCKER_REQUEST");
  auto off = Settle(Call(&incognito, "contentBlocker.setEnabled", R"({"enabled":false})"));
  assert(off->ok && !Bridge().auto_update);
  Advance(200);
  assert(Events().size() == 2 && !Events()[1].GetDict().Find("enabled")->GetBool());
  auto missing_list = Settle(Call(profile.get(), "contentBlocker.setListEnabled", R"({"id":"nope","enabled":true})"));
  assert(missing_list->code == "ERR_CONTENT_BLOCKER_UNAVAILABLE" && missing_list->message == "no list nope");
  Advance(200);
  assert(Events().size() == 2);
  assert(Settle(Call(profile.get(), "contentBlocker.setListEnabled", R"({"id":"fanboy-cookiemonster","enabled":true})"))->ok);
  assert(Bridge().lists["fanboy-cookiemonster"]);
  assert(Loads().pages_created == 1);

  Bridge().mode = FakeBridge::Mode::kCrashOnSend;
  auto crashed = Settle(Call(profile.get(), "contentBlocker.isAllowed", R"({"host":"example.com"})"));
  assert(crashed->ok && crashed->value.GetBool());
  assert(Loads().pages_created == 2 && Loads().live.size() == 1);

  Bridge().mode = FakeBridge::Mode::kDropFirstReply;
  auto dropped = Call(profile.get(), "contentBlocker.state", "{}");
  Advance(1000);
  assert(!dropped->done);
  Advance(22000);
  assert(dropped->done && dropped->ok && !Dict(dropped).FindDict("stats")->Find("ready")->GetBool());
  assert(*Dict(dropped).FindDict("stats")->FindString("error") == "the call timed out");
  assert(Loads().live.empty());
  assert(Settle(Call(profile.get(), "contentBlocker.state", "{}"))->ok);
  assert(Loads().live.size() == 1);

  Bridge().mode = FakeBridge::Mode::kNotReady;
  auto unready = Call(profile.get(), "contentBlocker.isAllowed", R"({"host":"example.com"})");
  Advance(600);
  assert(!unready->done && Loads().pages_created >= 5);
  Bridge().mode = FakeBridge::Mode::kAnswer;
  Settle(unready);
  assert(unready->ok && unready->value.GetBool());

  Advance(31000);
  assert(Loads().live.empty());
  Loads().commit_instead = "https://evil.test/";
  auto foreign = Call(profile.get(), "contentBlocker.state", "{}");
  const int requests_before = Bridge().requests;
  Advance(25);
  base::RunTasks();
  for (auto* contents : std::vector<content::WebContents*>(Loads().live)) contents->FailLoad(-2);
  Advance(1500);
  assert(!foreign->done && Bridge().requests == requests_before);
  Loads().commit_instead.clear();
  Settle(foreign);
  assert(foreign->ok && Dict(foreign).FindDict("stats")->Find("ready")->GetBool());

  assert(Settle(Call(profile.get(), "contentBlocker.devFailNextMessage", "{}"))->ok);
  auto dev = Settle(Call(profile.get(), "contentBlocker.setEnabled", R"({"enabled":true})"));
  assert(dev->code == "ERR_CONTENT_BLOCKER_UNAVAILABLE" && dev->message == "getWhitelist: failed (dev)");
  assert(Settle(Call(profile.get(), "contentBlocker.setEnabled", R"({"enabled":true})"))->ok);
  assert(Settle(Call(profile.get(), "contentBlocker.devDelayNextLoad", R"({"ms":5})"))->code == "ERR_CONTENT_BLOCKER_UNAVAILABLE");

  Advance(1000);
  assert(Loads().live.size() == 1);
  Advance(31000);
  assert(Loads().live.empty());

  Bridge().mode = FakeBridge::Mode::kSilent;
  std::vector<std::shared_ptr<Result>> pending;
  for (size_t i = 0; i < 64; ++i) pending.push_back(Call(profile.get(), "contentBlocker.state", "{}"));
  auto busy = Call(profile.get(), "contentBlocker.state", "{}");
  assert(busy->done && busy->code == "ERR_CONTENT_BLOCKER_BUSY");
  Advance(200);
  assert(Loads().live.size() == 1);
  profile.reset();
  base::RunTasks();
  for (auto& result : pending) {
    assert(result->done && result->ok);
    assert(*result->value.GetDict().FindDict("stats")->FindString("error") == "profile gone");
  }
  assert(Loads().live.empty());
  Advance(500);
  Bridge().mode = FakeBridge::Mode::kAnswer;
}

void DestroyedBeforeLoad(const std::filesystem::path& scratch) {
  MakeBlocker(scratch / "early");
  base::FakeExeDir() = base::FilePath((scratch / "early").string());
  auto profile = std::make_unique<Profile>("/profiles/Early");
  auto pending = Call(profile.get(), "contentBlocker.setEnabled", R"({"enabled":true})");
  profile.reset();
  base::RunTasks();
  assert(pending->done && pending->code == "ERR_CONTENT_BLOCKER_UNAVAILABLE" && pending->message == "profile gone");
}

}

int main() {
  const auto scratch = std::filesystem::temp_directory_path() / ("ardia-site-controls-" + std::to_string(::getpid()));
  std::filesystem::create_directories(scratch / "empty");
  ardia::StartLinuxSiteControls(ardia::ContentBlockerChanged([](Profile*, base::Value stats) { Events().push_back(std::move(stats)); }));
  Profile profile("/profiles/Default");
  Profile incognito("/profiles/Default", true, &profile);
  SitePermissionsThroughTheService(profile, incognito);
  BrowsingDataThroughTheService(profile, incognito);
  MissingBlocker(scratch);
  WrongComponent(scratch);
  WorkingBlocker(scratch);
  DestroyedBeforeLoad(scratch);
  std::filesystem::remove_all(scratch);
  std::puts("site controls service scenarios passed");
}
