#include "ardia/linux/ac_linux_site_controls_service.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ardia/linux/ac_linux_profile_service.h"
#include "ardia/linux/ac_linux_profiles.h"
#include "ardia/linux/ac_linux_site_controls.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/one_shot_event.h"
#include "base/path_service.h"
#include "base/scoped_observation.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ardia/public/ac_engine.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/view_type_utils.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_id.h"
#include "extensions/common/mojom/view_type.mojom.h"
#include "net/base/net_errors.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

extern bool (*g_ardia_restricted_component_extension)(const extensions::ExtensionId& extension_id);

namespace ardia {
namespace {

namespace controls = linux_site_controls;

constexpr size_t kMaxArgs = 64 * 1024;
constexpr size_t kMaxBridgeReply = 1024 * 1024;
constexpr base::TimeDelta kTick = base::Milliseconds(25);
constexpr char kBlockerDirSwitch[] = "ardia-content-blocker-dir";

void Fail(SiteControlsDone done, std::string_view code, std::string_view message) {
  std::move(done).Run(false, base::Value(base::DictValue().Set("code", code).Set("message", message)));
}

void Reply(SiteControlsDone done, std::variant<base::Value, controls::Failure> result) {
  if (auto* failure = std::get_if<controls::Failure>(&result))
    return Fail(std::move(done), failure->code, failure->message);
  std::move(done).Run(true, std::move(std::get<base::Value>(result)));
}

int64_t NowMs() {
  return (base::TimeTicks::Now() - base::TimeTicks()).InMilliseconds();
}

ContentBlockerChanged& Changed() {
  static base::NoDestructor<ContentBlockerChanged> changed;
  return *changed;
}

using ProfileChosen = base::OnceCallback<void(Profile*, std::optional<controls::Failure>)>;

void SiteControlsProfile(Profile* app_profile, std::string_view name, const std::string& requested,
                         ProfileChosen chosen) {
  if (!app_profile || app_profile->IsOffTheRecord()) {
    return std::move(chosen).Run(nullptr, controls::Failure{"ERR_PROFILE_UNAVAILABLE", "The window has no regular profile"});
  }
  auto route = controls::RouteProfile(name, requested);
  if (auto* failure = std::get_if<controls::Failure>(&route)) return std::move(chosen).Run(nullptr, *failure);
  if (std::get<controls::ProfileRoute>(route) == controls::ProfileRoute::kApp) {
    return std::move(chosen).Run(app_profile, std::nullopt);
  }
  ardiacore::ResolveLinuxProfile(requested, base::BindOnce(
      [](ProfileChosen chosen, Profile* profile, ardiacore::linux_profiles::ProfileError error) {
        using ardiacore::linux_profiles::ProfileError;
        if (profile && profile->IsOffTheRecord()) error = ProfileError::kPrivateService;
        if (!profile && error == ProfileError::kNone) error = ProfileError::kUnavailable;
        if (error != ProfileError::kNone) {
          return std::move(chosen).Run(nullptr, controls::Failure{
              std::string(ardiacore::linux_profiles::ProfileErrorCode(error)),
              std::string(ardiacore::linux_profiles::ProfileErrorMessage(error))});
        }
        std::move(chosen).Run(profile, std::nullopt);
      },
      std::move(chosen)));
}

struct ServiceReply {
  std::string name;
  SiteControlsDone done;
};

void ServiceReplied(void* context, const char* json) {
  std::unique_ptr<ServiceReply> reply(static_cast<ServiceReply*>(context));
  std::optional<base::Value> value = json ? base::JSONReader::Read(json, base::JSON_PARSE_RFC) : std::nullopt;
  if (!value) return Fail(std::move(reply->done), "ERR_BAD_REPLY", "The engine reply is not JSON");
  Reply(std::move(reply->done), controls::CompleteServiceCall(reply->name, *value));
}

struct BundledBlocker {
  bool present = false;
  base::FilePath root;
  std::string version;
};

base::FilePath BlockerDirectory() {
  const base::CommandLine* command = base::CommandLine::ForCurrentProcess();
  if (command->HasSwitch(kBlockerDirSwitch)) return command->GetSwitchValuePath(kBlockerDirSwitch);
  base::FilePath directory;
  base::PathService::Get(base::DIR_EXE, &directory);
  return directory.AppendASCII("extensions").AppendASCII("ublock");
}

BundledBlocker InspectBlocker(base::FilePath root) {
  BundledBlocker bundled;
  std::string manifest;
  if (!base::PathExists(root.AppendASCII(std::string(controls::kBridgePage))) ||
      !base::ReadFileToStringWithMaxSize(root.AppendASCII("manifest.json"), &manifest, 1024 * 1024)) {
    return bundled;
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(manifest, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) return bundled;
  bundled.root = base::MakeAbsoluteFilePath(root);
  bundled.present = !bundled.root.empty();
  for (const char* key : {"version_name", "version"}) {
    const std::string* version = parsed->GetDict().FindString(key);
    if (version && controls::ValidBundledVersion(*version)) {
      bundled.version = *version;
      break;
    }
  }
  return bundled;
}

class LinuxContentBlocker : public ProfileObserver, public content::WebContentsObserver {
 public:
  static LinuxContentBlocker* For(Profile* profile) {
    auto& all = All();
    auto found = all.find(profile);
    if (found != all.end()) return found->second.get();
    auto blocker = std::make_unique<LinuxContentBlocker>(profile);
    LinuxContentBlocker* raw = blocker.get();
    all.emplace(profile, std::move(blocker));
    raw->Start();
    return raw;
  }

  explicit LinuxContentBlocker(Profile* profile) : profile_(profile) {
    profile_observation_.Observe(profile);
  }

  ~LinuxContentBlocker() override {
    Observe(nullptr);
    page_.reset();
    retired_.clear();
    for (auto& call : queue_.Drain()) Respond(call, nullptr, "profile gone");
  }

  void Call(controls::BlockerRequest request, SiteControlsDone done) {
    if (request.op == controls::BlockerOp::kFailNext) {
      queue_.FailNext();
      return std::move(done).Run(true, base::Value());
    }
    if (load_ == Load::kMissing || load_ == Load::kFailed) {
      const std::string error = load_ == Load::kMissing ? "the content blocker is not installed"
                                                        : "the content blocker didn't load";
      return Reply(std::move(done), controls::CompleteBlockerCall(request, nullptr, &error, version_));
    }
    const std::optional<int> ticket = queue_.Enqueue(std::move(request), NowMs());
    if (!ticket) return Fail(std::move(done), "ERR_CONTENT_BLOCKER_BUSY", "Too many content blocker calls are waiting");
    waiting_.emplace(*ticket, std::move(done));
    Pump();
  }

  void Unavailable(controls::BlockerRequest request, SiteControlsDone done, const std::string& error) {
    Reply(std::move(done), controls::CompleteBlockerCall(request, nullptr, &error, version_));
  }

 private:
  enum class Load { kInspecting, kWaiting, kLoaded, kMissing, kFailed };

  static std::map<Profile*, std::unique_ptr<LinuxContentBlocker>>& All() {
    static base::NoDestructor<std::map<Profile*, std::unique_ptr<LinuxContentBlocker>>> all;
    return *all;
  }

  void Start() {
    root_ = BlockerDirectory();
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE, base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
        base::BindOnce(&InspectBlocker, root_),
        base::BindOnce(&LinuxContentBlocker::Inspected, weak_factory_.GetWeakPtr()));
  }

  void Inspected(BundledBlocker bundled) {
    version_ = bundled.version;
    if (bundled.present) root_ = bundled.root;
    if (!bundled.present) {
      load_ = Load::kMissing;
      return FailAll("the content blocker is not installed");
    }
    load_ = Load::kWaiting;
    extensions::ExtensionSystem::Get(profile_)->ready().Post(
        FROM_HERE, base::BindOnce(&LinuxContentBlocker::LoadExtension, weak_factory_.GetWeakPtr()));
  }

  void LoadExtension() {
    extensions::ExtensionRegistry* registry = extensions::ExtensionRegistry::Get(profile_);
    const extensions::Extension* loaded =
        registry ? registry->enabled_extensions().GetByID(std::string(controls::kExtensionId)) : nullptr;
    if (!loaded || loaded->path() != root_) {
      extensions::ComponentLoader* loader = extensions::ComponentLoader::Get(profile_);
      const std::string id = loader ? loader->AddOrReplace(root_) : std::string();
      const extensions::Extension* enabled =
          registry ? registry->enabled_extensions().GetByID(std::string(controls::kExtensionId)) : nullptr;
      if (id != controls::kExtensionId || !enabled || enabled->path() != root_) {
        if (loader && !id.empty()) loader->Remove(id);
        load_ = Load::kFailed;
        LOG(ERROR) << "Ardia's content blocker did not load from " << root_.value();
        return FailAll("the content blocker didn't load");
      }
    }
    load_ = Load::kLoaded;
    Pump();
  }

  void FailAll(const std::string& error) {
    for (auto& call : queue_.Drain()) Respond(call, nullptr, error);
  }

  GURL BridgeUrl() const {
    return GURL("chrome-extension://" + std::string(controls::kExtensionId) + "/" +
                std::string(controls::kBridgePage));
  }

  void EnsureTicking() {
    if (!tick_.IsRunning())
      tick_.Start(FROM_HERE, kTick, base::BindRepeating(&LinuxContentBlocker::Tick, base::Unretained(this)));
  }

  void Tick() {
    const int64_t now = NowMs();
    if (auto expired = queue_.Expired(now)) Finish(std::move(*expired));
    if (queue_.in_flight()) {
      if (!awaiting_send_ && !polling_) PollReply();
      return;
    }
    if (queue_.IdleFor(now)) {
      ResetPage();
      tick_.Stop();
      return;
    }
    if (!queue_.size() && !page_) {
      tick_.Stop();
      return;
    }
    Pump();
  }

  void Pump() {
    if (queue_.size()) EnsureTicking();
    if (load_ != Load::kLoaded) return;
    switch (queue_.Next(NowMs())) {
      case controls::BlockerQueue::Step::kOpenPage: return OpenPage();
      case controls::BlockerQueue::Step::kSend: return SendCurrent();
      case controls::BlockerQueue::Step::kIdle:
      case controls::BlockerQueue::Step::kWait: return;
    }
  }

  void OpenPage() {
    ResetPage();
    content::WebContents::CreateParams params(profile_);
    params.initially_hidden = true;
    page_ = content::WebContents::Create(params);
    extensions::SetViewType(page_.get(), extensions::mojom::ViewType::kTabContents);
    extensions::ExtensionsBrowserClient::Get()->CreateExtensionWebContentsObserver(page_.get());
    Observe(page_.get());
    ++page_generation_;
    content::NavigationController::LoadURLParams load(BridgeUrl());
    load.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
    page_->GetController().LoadURLWithParams(load);
  }

  void ResetPage() {
    Observe(nullptr);
    ++page_generation_;
    awaiting_send_ = false;
    polling_ = false;
    if (!page_) return;
    retired_.push_back(std::move(page_));
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&LinuxContentBlocker::DropRetired, weak_factory_.GetWeakPtr()));
  }

  void DropRetired() { retired_.clear(); }

  content::RenderFrameHost* BridgeFrame() const {
    if (!page_) return nullptr;
    content::RenderFrameHost* frame = page_->GetPrimaryMainFrame();
    if (!frame || !frame->IsRenderFrameLive() || frame->GetLastCommittedURL() != BridgeUrl() ||
        frame->GetLastCommittedOrigin().GetURL() != BridgeUrl().DeprecatedGetOriginAsURL()) {
      return nullptr;
    }
    return frame;
  }

  void PageLost(std::string_view reason) {
    ResetPage();
    if (auto outcome = queue_.PageGone(reason, NowMs())) Finish(std::move(*outcome));
    Pump();
  }

  void SendCurrent() {
    const controls::PendingCall* call = queue_.Current();
    content::RenderFrameHost* frame = BridgeFrame();
    if (!call || !frame) return PageLost("not the extension page");
    const std::optional<std::string> json = base::WriteJson(controls::BridgeRequest(*call, NowMs()));
    const std::optional<std::string> literal = json ? base::WriteJson(base::Value(*json)) : std::nullopt;
    if (!literal) return Finish(*queue_.Fail("no answer", NowMs()));
    queue_.Sent();
    awaiting_send_ = true;
    frame->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(controls::SendScript(*literal)),
        base::BindOnce(&LinuxContentBlocker::SendAnswered, weak_factory_.GetWeakPtr(), page_generation_,
                       call->ticket),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }

  void SendAnswered(uint64_t generation, int ticket, base::Value result) {
    if (generation != page_generation_ || !queue_.Current() || queue_.Current()->ticket != ticket) return;
    awaiting_send_ = false;
    if (result.is_string() && result.GetString() == "sent") return;
    const std::string error = result.is_string() ? result.GetString() : std::string("no reply");
    if (auto outcome = queue_.Fail(error, NowMs())) Finish(std::move(*outcome));
  }

  void PollReply() {
    content::RenderFrameHost* frame = BridgeFrame();
    if (!frame) return PageLost("not the extension page");
    polling_ = true;
    frame->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(controls::PollScript()),
        base::BindOnce(&LinuxContentBlocker::Polled, weak_factory_.GetWeakPtr(), page_generation_,
                       queue_.Current()->ticket),
        ISOLATED_WORLD_ID_CHROME_INTERNAL);
  }

  void Polled(uint64_t generation, int ticket, base::Value result) {
    if (generation != page_generation_) return;
    polling_ = false;
    if (!result.is_string() || result.GetString().size() > kMaxBridgeReply) return;
    std::optional<base::Value> parsed = base::JSONReader::Read(result.GetString(), base::JSON_PARSE_RFC);
    if (!parsed) return;
    std::optional<controls::BridgeReply> reply = controls::ParseBridgeReply(*parsed, ticket);
    if (!reply) return;
    if (auto outcome = queue_.Answer(std::move(*reply), NowMs())) Finish(std::move(*outcome));
  }

  void Finish(controls::BlockerQueue::Outcome outcome) {
    if (outcome.reset_page) ResetPage();
    if (outcome.retry) return;
    if (outcome.error) LOG(WARNING) << "Ardia content blocker: " << *outcome.error;
    Respond(outcome.call, outcome.error ? nullptr : &outcome.value,
            outcome.error ? *outcome.error : std::string());
  }

  void Respond(const controls::PendingCall& call, const base::Value* value, const std::string& error) {
    auto found = waiting_.find(call.ticket);
    if (found == waiting_.end()) return;
    SiteControlsDone done = std::move(found->second);
    waiting_.erase(found);
    const bool failed = !value;
    const std::string* error_text = failed ? &error : nullptr;
    Reply(std::move(done), controls::CompleteBlockerCall(call.request, value, error_text, version_));
    if (!failed && controls::Mutates(call.request.op)) AnnounceChange();
  }

  void AnnounceChange() {
    if (load_ != Load::kLoaded) return;
    const std::optional<int> ticket = queue_.Enqueue(controls::BlockerRequest{}, NowMs());
    if (!ticket) return;
    waiting_.emplace(*ticket, base::BindOnce(
        [](base::WeakPtr<LinuxContentBlocker> blocker, bool ok, base::Value state) {
          if (!blocker || !ok || !state.is_dict() || !Changed()) return;
          const base::DictValue* stats = state.GetDict().FindDict("stats");
          if (stats) Changed().Run(blocker->profile_, base::Value(stats->Clone()));
        },
        weak_factory_.GetWeakPtr()));
    Pump();
  }

  void DidFinishLoad(content::RenderFrameHost* frame, const GURL&) override {
    if (!page_ || frame != page_->GetPrimaryMainFrame()) return;
    if (!BridgeFrame()) return PageLost("not the extension page");
    queue_.PageReady();
    Pump();
  }

  void DidFailLoad(content::RenderFrameHost* frame, const GURL&, int error_code) override {
    if (error_code == net::ERR_ABORTED || !page_ || frame != page_->GetPrimaryMainFrame()) return;
    PageLost("closed: the bridge page didn't load");
  }

  void PrimaryPageChanged(content::Page& page) override {
    const GURL& url = page.GetMainDocument().GetLastCommittedURL();
    if (!page_ || url.is_empty() || url.IsAboutBlank()) return;
    if (url != BridgeUrl()) PageLost("not the extension page");
  }

  void PrimaryMainFrameRenderProcessGone(base::TerminationStatus) override {
    PageLost("renderer gone");
  }

  void OnProfileWillBeDestroyed(Profile* profile) override {
    profile_observation_.Reset();
    tick_.Stop();
    auto& all = All();
    auto found = all.find(profile);
    if (found == all.end()) return;
    std::unique_ptr<LinuxContentBlocker> self = std::move(found->second);
    all.erase(found);
  }

  raw_ptr<Profile> profile_;
  base::FilePath root_;
  Load load_ = Load::kInspecting;
  std::string version_;
  std::unique_ptr<content::WebContents> page_;
  std::vector<std::unique_ptr<content::WebContents>> retired_;
  uint64_t page_generation_ = 0;
  bool awaiting_send_ = false;
  bool polling_ = false;
  controls::BlockerQueue queue_;
  std::map<int, SiteControlsDone> waiting_;
  base::RepeatingTimer tick_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::WeakPtrFactory<LinuxContentBlocker> weak_factory_{this};
};

std::optional<base::DictValue> ParseArgs(const base::Value* args, std::optional<controls::Failure>* failure) {
  if (args && args->is_none()) return base::DictValue();
  if (!args || !args->is_string() || args->GetString().size() > kMaxArgs ||
      args->GetString().find('\0') != std::string::npos) {
    *failure = controls::Failure{"ERR_ENGINE", "Engine arguments must be JSON text"};
    return std::nullopt;
  }
  std::optional<base::Value> parsed = base::JSONReader::Read(args->GetString(), base::JSON_PARSE_RFC, 8);
  if (!parsed || !parsed->is_dict()) {
    *failure = controls::Failure{"ERR_ENGINE", "Engine arguments must be a JSON object"};
    return std::nullopt;
  }
  return std::move(*parsed).TakeDict();
}

void RunSiteControlsCall(const std::string& name, base::DictValue input, bool page_filtered,
                         SiteControlsDone done, Profile* profile, std::optional<controls::Failure> failure) {
  if (!profile) return Fail(std::move(done), failure->code, failure->message);
  if (controls::IsContentBlockerCall(name)) {
    auto request = controls::PrepareBlockerCall(name, input);
    if (auto* invalid = std::get_if<controls::Failure>(&request))
      return Fail(std::move(done), invalid->code, invalid->message);
    auto& blocker_request = std::get<controls::BlockerRequest>(request);
    LinuxContentBlocker* blocker = LinuxContentBlocker::For(profile);
    if (!page_filtered && blocker_request.op != controls::BlockerOp::kFailNext) {
      return blocker->Unavailable(std::move(blocker_request), std::move(done),
                                  "the content blocker isn't available in this profile yet");
    }
    return blocker->Call(std::move(blocker_request), std::move(done));
  }
  auto request = controls::PrepareServiceCall(name, input);
  if (auto* invalid = std::get_if<controls::Failure>(&request))
    return Fail(std::move(done), invalid->code, invalid->message);
  auto& service = std::get<controls::ServiceRequest>(request);
  const std::optional<std::string> json = base::WriteJson(service.args);
  if (!json) return Fail(std::move(done), "ERR_ENGINE", "Engine arguments are not JSON");
  const std::string path = profile->GetPath().value();
  ac_engine_call(service.service.c_str(), path.c_str(), json->c_str(), ServiceReplied,
                 new ServiceReply{name, std::move(done)});
}

}

bool LinuxSiteControlsHandles(std::string_view name) {
  return controls::Handles(name);
}

void AppendLinuxSiteControlsCapabilities(base::ListValue& capabilities) {
  for (const std::string_view name : controls::Capabilities()) capabilities.Append(base::Value(name));
}

void LinuxSiteControlsCall(Profile* app_profile, Profile* page_profile, const std::string& name,
                           const std::string& requested_profile, const base::Value* args,
                           SiteControlsDone done) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  std::optional<controls::Failure> failure;
  std::optional<base::DictValue> input = ParseArgs(args, &failure);
  if (!input) return Fail(std::move(done), failure->code, failure->message);
  const bool page_filtered = page_profile && app_profile && page_profile->GetOriginalProfile() == app_profile;
  SiteControlsProfile(app_profile, name, requested_profile,
                      base::BindOnce(&RunSiteControlsCall, name, std::move(*input), page_filtered, std::move(done)));
}

void StartLinuxSiteControls(ContentBlockerChanged changed) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  g_ardia_restricted_component_extension = [](const extensions::ExtensionId& id) {
    return id == controls::kExtensionId;
  };
  Changed() = std::move(changed);
}

void StartLinuxContentBlocker(Profile* profile) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (!profile || profile->IsOffTheRecord()) return;
  LinuxContentBlocker::For(profile);
}

}
