#include "ardia/linux/ac_linux_host.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ardia/linux/ac_linux_policy.h"
#include "ardia/linux/ac_linux_session.h"
#include "ardia/linux/host_bootstrap.h"
#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "base/scoped_observation.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/system/sys_info.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "chrome/browser/ardia/public/ac_engine.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "components/version_info/version_info.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/common/url_constants.h"
#include "net/base/net_errors.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/mojom/favicon/favicon_url.mojom.h"
#include "ui/aura/window.h"
#include "ui/base/base_window.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include "url/origin.h"

extern bool (*g_ardia_keeps_window_without_tabs)(
    const BrowserWindowInterface* browser);

namespace ardia {
namespace {

constexpr size_t kDocumentLimit = 8 * 1024 * 1024;
constexpr size_t kAssetLimit = 32 * 1024 * 1024;
constexpr char kSessionName[] = "linux-session.json";

class ShellHostView;
class HostMessageHandler;
std::map<int, std::string>& RequestedWindows();

std::map<int, ShellHostView*>& Hosts() {
  static base::NoDestructor<std::map<int, ShellHostView*>> hosts;
  return *hosts;
}

int& Revision() {
  static int rev = 0;
  return rev;
}

int TabId(content::WebContents* contents) {
  return sessions::SessionTabHelper::IdForTab(contents).id();
}

std::string String(const base::DictValue& value, std::string_view key) {
  const std::string* text = value.FindString(key);
  return text ? *text : std::string();
}

const base::ListValue& Arguments(const base::DictValue& value) {
  static const base::NoDestructor<base::ListValue> empty;
  const base::ListValue* args = value.FindList("args");
  return args ? *args : *empty;
}

std::string ArgumentString(const base::ListValue& args, size_t index) {
  return index < args.size() && args[index].is_string()
             ? args[index].GetString()
             : std::string();
}

bool DefaultProfileId(std::string_view id) {
  return id.empty() || id == "default";
}

base::FilePath DocumentDirectory(Profile* profile) {
  return profile->GetPath().AppendASCII("Ardia");
}

bool PrepareDocuments(const base::FilePath& directory) {
  if (base::IsLink(directory) || !base::CreateDirectory(directory)) {
    return false;
  }
  const auto parent = base::MakeAbsoluteFilePath(directory.DirName());
  const auto resolved = base::MakeAbsoluteFilePath(directory);
  return !parent.empty() && parent.IsParent(resolved);
}

std::optional<std::string> ReadDocument(const base::FilePath& directory,
                                      const std::string& name) {
  if (!linux_policy::ValidDocumentName(name) ||
      !PrepareDocuments(directory)) {
    return std::nullopt;
  }
  const base::FilePath file = directory.AppendASCII(name);
  if (base::IsLink(file)) {
    return std::nullopt;
  }
  std::string contents;
  return base::ReadFileToStringWithMaxSize(file, &contents, kDocumentLimit)
             ? std::make_optional(std::move(contents))
             : std::nullopt;
}

bool WriteDocument(const base::FilePath& directory,
                   const std::string& name,
                   const std::optional<std::string>& contents) {
  if (!linux_policy::ValidDocumentName(name) ||
      (contents && contents->size() > kDocumentLimit) ||
      !PrepareDocuments(directory)) {
    return false;
  }
  const base::FilePath file = directory.AppendASCII(name);
  if (base::IsLink(file)) {
    return false;
  }
  return contents ? base::ImportantFileWriter::WriteFileAtomically(file, *contents)
                  : !base::PathExists(file) || base::DeleteFile(file);
}

std::optional<base::DictValue> ReadSession(const base::FilePath& directory) {
  const auto text = ReadDocument(directory, kSessionName);
  if (!text) return std::nullopt;
  auto value = base::JSONReader::Read(*text, base::JSON_PARSE_RFC,
                                     kLinuxSessionMaxDepth);
  if (!value || !value->is_dict()) return std::nullopt;
  return MergeLinuxSession(base::DictValue(), base::DictValue(), value->GetDict());
}

struct SessionWriteResult {
  bool ok = false;
  base::DictValue previous;
  base::DictValue session;
};

SessionWriteResult MergeAndWriteSession(const base::FilePath& directory,
                                       base::DictValue base,
                                       base::DictValue local) {
  SessionWriteResult result;
  auto previous = ReadSession(directory);
  if (previous) result.previous = std::move(*previous);
  auto merged = MergeLinuxSession(base, local, result.previous);
  if (!merged) return result;
  const auto json = base::WriteJsonWithOptions(
      *merged, base::OPTIONS_OMIT_DOUBLE_TYPE_PRESERVATION,
      kLinuxSessionMaxDepth);
  if (!json || json->size() > kLinuxSessionMaxBytes ||
      !WriteDocument(directory, kSessionName, json)) return result;
  result.session = std::move(*merged);
  result.ok = true;
  return result;
}

base::DictValue ReadDocuments(const base::FilePath& directory) {
  base::DictValue documents;
  if (!PrepareDocuments(directory)) {
    return documents;
  }
  base::FileEnumerator files(directory, false, base::FileEnumerator::FILES);
  size_t bytes = 0;
  for (auto file = files.Next(); !file.empty(); file = files.Next()) {
    const std::string name = file.BaseName().value();
    if (name == kSessionName) continue;
    auto contents = ReadDocument(directory, name);
    if (contents && documents.size() < 128 &&
        bytes + contents->size() <= kDocumentLimit) {
      bytes += contents->size();
      documents.Set(name, std::move(*contents));
    }
  }
  return documents;
}

scoped_refptr<base::SequencedTaskRunner> FileRunner() {
  static const base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN}));
  return *runner;
}

std::string AssetPath(const GURL& url) {
  const std::string path(url.path());
  return path == "/" || path.empty() ? "index.html" : path.substr(1);
}

std::string MimeType(std::string_view path) {
  const auto dot = path.rfind('.');
  const auto ext = dot == std::string_view::npos ? "" : path.substr(dot);
  if (ext == ".html") return "text/html";
  if (ext == ".js" || ext == ".mjs") return "application/javascript";
  if (ext == ".css") return "text/css";
  if (ext == ".json") return "application/json";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".png") return "image/png";
  if (ext == ".ico") return "image/x-icon";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".webp") return "image/webp";
  if (ext == ".gif") return "image/gif";
  if (ext == ".avif") return "image/avif";
  if (ext == ".woff") return "font/woff";
  if (ext == ".woff2") return "font/woff2";
  if (ext == ".ttf") return "font/ttf";
  return {};
}

base::FilePath AssetDirectory() {
  const base::CommandLine* command = base::CommandLine::ForCurrentProcess();
  if (command->HasSwitch("ardia-ui-dir")) {
    return command->GetSwitchValuePath("ardia-ui-dir");
  }
  base::FilePath directory;
  base::PathService::Get(base::DIR_EXE, &directory);
  return directory.AppendASCII("ardia-ui");
}

std::optional<std::string> ReadAsset(base::FilePath directory,
                                   std::string path,
                                   base::FilePath documents,
                                   base::DictValue bootstrap) {
  if (!linux_policy::ValidAssetPath(path) || MimeType(path).empty()) {
    return std::nullopt;
  }
  if (path == "host-bootstrap.js") {
    base::DictValue saved = ReadDocuments(documents);
    if (auto session = ReadSession(documents)) {
      bootstrap.Set("session", std::move(*session));
    }
    bootstrap.Set("documents", std::move(saved));
    const auto json = base::WriteJson(bootstrap);
    if (!json) return std::nullopt;
    return "globalThis.__ardiaBootstrap=" + *json + ";\n" + kArdiaHostBootstrap;
  }
  const auto root = base::MakeAbsoluteFilePath(directory);
  const auto file = base::MakeAbsoluteFilePath(directory.AppendASCII(path));
  if (root.empty() || file.empty() || !root.IsParent(file)) {
    return std::nullopt;
  }
  std::string contents;
  if (!base::ReadFileToStringWithMaxSize(file, &contents, kAssetLimit)) {
    return std::nullopt;
  }
  if (path == "index.html") {
    const size_t head = contents.find("<head>");
    if (head == std::string::npos) return std::nullopt;
    contents.insert(head + 6, "<script src=\"/host-bootstrap.js\"></script>");
  }
  return contents;
}

class TabBinding : public content::WebContentsObserver {
 public:
  TabBinding(ShellHostView* owner, content::WebContents* contents,
             std::string key);
  ~TabBinding() override;
  const std::string& key() const { return key_; }
  content::WebContents* contents() const { return contents_; }
  views::WebView* viewport() const { return viewport_; }
  base::DictValue Navigation() const;
  void ReportNavigation();
  void MoveTo(ShellHostView* owner);
  void Rebind(content::WebContents* contents);
  void SetViewport(views::WebView* view) { viewport_ = view; }

 private:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void LoadProgressChanged(double progress) override;
  void TitleWasSet(content::NavigationEntry*) override;
  void DidFailLoad(content::RenderFrameHost* frame, const GURL& url,
                   int error_code) override;
  void DidUpdateFaviconURL(
      content::RenderFrameHost*,
      const std::vector<blink::mojom::FaviconURLPtr>& candidates,
      blink::mojom::FaviconUpdateReason) override;
  void WebContentsDestroyed() override;

  void DidOpenRequestedURL(content::WebContents* new_contents,
                            content::RenderFrameHost*, const GURL& url,
                            const content::Referrer&, WindowOpenDisposition disposition,
                            ui::PageTransition, bool, bool) override;
  raw_ptr<ShellHostView> owner_;
  raw_ptr<content::WebContents> contents_;
  raw_ptr<views::WebView> viewport_ = nullptr;
  std::string key_;
};

class ShellHostView : public views::View,
                      public views::ViewObserver,
                      public TabStripModelObserver {
 public:
  METADATA_HEADER(ShellHostView, views::View)

 public:
  ShellHostView(BrowserWindowInterface* browser, BrowserView* parent, bool startup);
  ~ShellHostView() override;
  void Initialize();
  void DetachBrowser();
  void DetachViewports();
  int id() const { return id_; }
  const std::string& window_id() const { return window_id_; }
  void set_window_id(std::string id) { window_id_ = std::move(id); }
  BrowserWindowInterface* browser() const { return browser_; }
  content::WebContents* shell() const { return shell_->GetWebContents(); }
  Profile* profile() const { return browser_ ? browser_->GetProfile() : nullptr; }
  bool Trusted(content::WebUI* ui) const;
  base::DictValue Bootstrap() const;
  base::DictValue Strip() const;
  base::DictValue Transaction(std::optional<int> command, bool advance);
  void SetHandler(HostMessageHandler* handler) { handler_ = handler; }
  void ClearHandler(HostMessageHandler* handler) {
    if (handler_ == handler) handler_ = nullptr;
  }
  void Event(std::string_view module, std::string_view event,
             base::Value payload);
  void QueueTransaction(std::optional<int> command);
  void EngineEvent(std::string_view topic, const base::DictValue& payload);
  void Invoke(std::string method, const base::DictValue& args,
              base::OnceCallback<void(bool, base::Value)> done);
  void ReportTab(int id, base::DictValue event);
  base::WeakPtr<ShellHostView> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  TabBinding* Binding(int id);
  TabBinding* BindingForKey(std::string_view key);
  void CreateTab(const base::DictValue& args,
                 base::OnceCallback<void(bool, base::Value)> done);
  void TabCommand(const base::DictValue& args,
                  base::OnceCallback<void(bool, base::Value)> done);
  void SetViewport(const base::DictValue& args,
                   base::OnceCallback<void(bool, base::Value)> done);
  void StripCommand(int command, const base::DictValue& args,
                    base::OnceCallback<void(bool, base::Value)> done);
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  void UnregisterAccelerators();
  void LayoutOverlay();
  void ReattachViewports();
  void OnViewBoundsChanged(views::View*) override { LayoutOverlay(); }
  void OnViewIsDeleting(views::View*) override;
  void OnTabStripModelChanged(TabStripModel*, const TabStripModelChange& change,
                              const TabStripSelectionChange& selection) override;

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<BrowserView> browser_view_;
  raw_ptr<views::WebView> shell_ = nullptr;
  raw_ptr<HostMessageHandler> handler_ = nullptr;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  int id_;
  bool startup_;
  std::string window_id_;
  std::map<int, std::unique_ptr<TabBinding>> bindings_;
  bool app_operation_ = false;
  base::ScopedObservation<views::View, views::ViewObserver> view_observation_{this};
  base::WeakPtrFactory<ShellHostView> weak_factory_{this};
};

BEGIN_METADATA(ShellHostView)
END_METADATA

ShellHostView* HostFor(content::WebContents* contents) {
  for (const auto& [id, host] : Hosts()) {
    if (host->shell() == contents) return host;
  }
  return nullptr;
}

class HostMessageHandler : public content::WebUIMessageHandler {
 public:
  explicit HostMessageHandler(base::WeakPtr<ShellHostView> host)
      : host_(std::move(host)) {}
  ~HostMessageHandler() override {
    if (host_) host_->ClearHandler(this);
  }
  void SendEvent(std::string_view module, std::string_view event,
                 const base::Value& payload) {
    if (!IsJavascriptAllowed() || !host_ || !host_->Trusted(web_ui())) return;
    CallJavascriptFunction("__ardiaEvent", base::Value(module),
                           base::Value(event), payload);
  }

 private:
  struct EngineReply {
    base::WeakPtr<HostMessageHandler> handler;
    std::string id;
  };
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "ardiaRequest", base::BindRepeating(&HostMessageHandler::Request,
                                            base::Unretained(this)));
  }
  void OnJavascriptDisallowed() override {
    weak_factory_.InvalidateWeakPtrs();
    if (host_) {
      host_->ClearHandler(this);
      host_->DetachViewports();
    }
  }
  void Reply(std::string id, bool success, base::Value response) {
    if (!host_ || !host_->Trusted(web_ui()) || !IsJavascriptAllowed()) return;
    CallJavascriptFunction("__ardiaReply", base::Value(id), base::Value(success),
                           response);
  }
  static void Replied(void* context, const char* json) {
    std::unique_ptr<EngineReply> reply(static_cast<EngineReply*>(context));
    if (reply->handler) {
      reply->handler->Reply(std::move(reply->id), true,
                             base::Value(json ? json : "{\"error\":\"empty engine reply\"}"));
    }
  }
  void Request(const base::ListValue& arguments) {
    CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
    if (!host_ || !host_->Trusted(web_ui()) || arguments.size() != 3 ||
        !arguments[0].is_string() || !arguments[1].is_string() ||
        !arguments[2].is_dict()) return;
    const std::string id = arguments[0].GetString();
    if (id.empty() || id.size() > 256) return;
    AllowJavascript();
    host_->SetHandler(this);
    const auto& args = arguments[2].GetDict();
    if (arguments[1].GetString() == "engine.call") {
      const std::string name = String(args, "name");
      const std::string profile = String(args, "profile");
      if (!DefaultProfileId(profile) || !host_->profile() ||
          host_->profile()->IsOffTheRecord()) {
        return Reply(id, false, base::Value("Only the default regular profile is supported"));
      }
      const base::Value* value = args.Find("args");
      if (!value || (!value->is_none() && !value->is_string())) {
        return Reply(id, false, base::Value("Engine arguments must be JSON text or null"));
      }
      const std::string json = value->is_string() ? value->GetString() : "{}";
      if (name.size() > 128 || name.find('\0') != std::string::npos ||
          json.size() > kDocumentLimit || json.find('\0') != std::string::npos) {
        return Reply(id, false, base::Value("Invalid or oversized engine request"));
      }
      const std::string path = host_->profile()->GetPath().value();
      ac_engine_call(name.c_str(), path.c_str(), json.c_str(), Replied,
                     new EngineReply{weak_factory_.GetWeakPtr(), id});
      return;
    }
    if (arguments[1].GetString() != "host.invoke") {
      return Reply(id, false, base::Value("Unknown native transport operation"));
    }
    const base::DictValue* payload = args.FindDict("args");
    if (!payload) return Reply(id, false, base::Value("Host arguments must be an object"));
    host_->Invoke(String(args, "method"), *payload,
                  base::BindOnce(&HostMessageHandler::Reply,
                                  weak_factory_.GetWeakPtr(), id));
  }

  base::WeakPtr<ShellHostView> host_;
  base::WeakPtrFactory<HostMessageHandler> weak_factory_{this};
};

class ShellDataSource : public content::URLDataSource {
 public:
  std::string GetSource() override { return "ardia"; }
  bool AllowCaching() override { return false; }
  bool ShouldServeMimeTypeAsContentTypeHeader() override { return true; }
  std::string GetMimeType(const GURL& url) override {
    return MimeType(AssetPath(url));
  }
  std::string GetContentSecurityPolicy(
      network::mojom::CSPDirectiveName directive) override {
    using Directive = network::mojom::CSPDirectiveName;
    switch (directive) {
      case Directive::DefaultSrc: return "default-src 'none';";
      case Directive::ScriptSrc: return "script-src 'self';";
      case Directive::TrustedTypes: return "trusted-types ardia-symbols;";
      case Directive::StyleSrc: return "style-src 'self' 'unsafe-inline';";
      case Directive::ImgSrc: return "img-src 'self' data: blob:;";
      case Directive::FontSrc: return "font-src 'self' data:;";
      case Directive::ConnectSrc: return "connect-src 'none';";
      case Directive::FrameSrc: return "frame-src 'none';";
      case Directive::ChildSrc: return "child-src 'none';";
      case Directive::ObjectSrc: return "object-src 'none';";
      case Directive::BaseURI: return "base-uri 'none';";
      case Directive::FormAction: return "form-action 'none';";
      default: return content::URLDataSource::GetContentSecurityPolicy(directive);
    }
  }
  void StartDataRequest(const GURL& url,
                        const content::WebContents::Getter& getter,
                        GotDataCallback callback) override {
    CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
    ShellHostView* host = HostFor(getter.Run());
    if (!host || !host->profile() || url.scheme() != "chrome" ||
        url.host() != "ardia") {
      return std::move(callback).Run(nullptr);
    }
    FileRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(&ReadAsset, AssetDirectory(), AssetPath(url),
                       DocumentDirectory(host->profile()), host->Bootstrap()),
        base::BindOnce(
            [](GotDataCallback callback, std::optional<std::string> contents) {
              std::move(callback).Run(
                  contents ? base::MakeRefCounted<base::RefCountedString>(
                                 std::move(*contents))
                           : nullptr);
            },
            std::move(callback)));
  }
};

class ShellController : public content::WebUIController {
 public:
  explicit ShellController(content::WebUI* ui) : WebUIController(ui) {
    ShellHostView* host = HostFor(ui->GetWebContents());
    if (!host || !host->profile()) return;
    content::URLDataSource::Add(host->profile(),
                                std::make_unique<ShellDataSource>());
    ui->AddMessageHandler(
        std::make_unique<HostMessageHandler>(host->GetWeakPtr()));
  }
};

class ShellConfig : public content::DefaultWebUIConfig<ShellController> {
 public:
  ShellConfig() : DefaultWebUIConfig("chrome", "ardia") {}
  bool IsWebUIEnabled(content::BrowserContext* context) override {
    return !Profile::FromBrowserContext(context)->IsOffTheRecord();
  }
};

ShellHostView::ShellHostView(BrowserWindowInterface* browser, BrowserView* parent,
                             bool startup)
    : browser_(browser), browser_view_(parent),
      id_(browser->GetSessionID().id()), startup_(startup),
      window_id_(Hosts().empty() ? "main" : "window-" + base::NumberToString(id_)) {
  Hosts()[id_] = this;
  shell_ = AddChildView(std::make_unique<views::WebView>(browser->GetProfile()));
  view_observation_.Observe(parent);
  browser->GetTabStripModel()->AddObserver(this);
}

ShellHostView::~ShellHostView() {
  weak_factory_.InvalidateWeakPtrs();
  UnregisterAccelerators();
  Hosts().erase(id_);
  bindings_.clear();
  TabStripModelObserver::StopObservingAll(this);
  view_observation_.Reset();
}

void ShellHostView::Initialize() {
  focus_manager_ = GetFocusManager();
  if (focus_manager_) {
    for (auto key : {ui::VKEY_T, ui::VKEY_L, ui::VKEY_W, ui::VKEY_R}) {
      focus_manager_->RegisterAccelerator(
          ui::Accelerator(key, ui::EF_CONTROL_DOWN),
          ui::AcceleratorManager::kHighPriority, this);
    }
    focus_manager_->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_T, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN),
        ui::AcceleratorManager::kHighPriority, this);
  }
  LayoutOverlay();
  shell_->LoadInitialURL(GURL("chrome://ardia/"));
}

void ShellHostView::DetachViewports() {
  for (auto& [id, binding] : bindings_) {
    if (!binding->viewport()) continue;
    binding->viewport()->SetVisible(false);
    binding->viewport()->SetWebContents(nullptr);
  }
}

void ShellHostView::DetachBrowser() {
  weak_factory_.InvalidateWeakPtrs();
  UnregisterAccelerators();
  TabStripModelObserver::StopObservingAll(this);
  view_observation_.Reset();
  bindings_.clear();
  browser_ = nullptr;
  browser_view_ = nullptr;
  Hosts().erase(id_);
}

bool ShellHostView::Trusted(content::WebUI* ui) const {
  if (!browser_ || ui->GetWebContents() != shell()) return false;
  const auto* frame = shell()->GetPrimaryMainFrame();
  return frame && frame->GetLastCommittedOrigin() ==
                      url::Origin::Create(GURL("chrome://ardia/")) &&
         shell()->GetLastCommittedURL().host() == "ardia";
}

base::DictValue ShellHostView::Bootstrap() const {
  base::DictValue info = base::DictValue().Set("appName", "Ardia")
      .Set("appVersion", version_info::GetVersionNumber())
      .Set("appBuild", "linux-host")
      .Set("bundleId", "dev.ardia.linux")
      .Set("configuration", "Release")
      .Set("osVersion", base::SysInfo::OperatingSystemVersion())
      .Set("osBuild", "Linux")
      .Set("arch", base::SysInfo::OperatingSystemArchitecture())
      .Set("model", "Linux")
      .Set("memoryGB", base::SysInfo::AmountOfTotalPhysicalMemory().InBytes() /
                           (1024.0 * 1024 * 1024))
      .Set("locale", "en-US")
      .Set("updates", false);
  return base::DictValue()
      .Set("windowId", window_id_)
      .Set("startup", startup_)
      .Set("profileId", "default")
      .Set("nativeModules", base::DictValue().Set(
                                "ArdiaApp", base::DictValue().Set(
                                                "systemInfo", std::move(info))))
      .Set("environment", base::DictValue())
      .Set("capabilities", base::ListValue());
}

void ShellHostView::LayoutOverlay() {
  if (!browser_view_) return;
  SetBoundsRect(browser_view_->GetLocalBounds());
  shell_->SetBoundsRect(GetLocalBounds());
  browser_view_->contents_web_view()->SetVisible(false);
}

void ShellHostView::OnViewIsDeleting(views::View*) {
  UnregisterAccelerators();
  view_observation_.Reset();
  browser_view_ = nullptr;
}

void ShellHostView::UnregisterAccelerators() {
  if (focus_manager_) focus_manager_->UnregisterAccelerators(this);
  focus_manager_ = nullptr;
}

bool ShellHostView::CanHandleAccelerators() const {
  return browser_ && handler_;
}

bool ShellHostView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  std::string command;
  switch (accelerator.key_code()) {
    case ui::VKEY_T:
      command = accelerator.IsShiftDown() ? "reopenClosedTab" : "newTab";
      break;
    case ui::VKEY_L: command = "focusCommandBar"; break;
    case ui::VKEY_W: command = "closeTab"; break;
    case ui::VKEY_R: command = "reload"; break;
    default: return false;
  }
  Event("ArdiaShell", "onCommand", base::Value(base::DictValue()
      .Set("command", command).Set("arg", base::Value()).Set("windowId", window_id_)));
  return true;
}

TabBinding* ShellHostView::Binding(int id) {
  const auto found = bindings_.find(id);
  return found == bindings_.end() ? nullptr : found->second.get();
}

TabBinding* ShellHostView::BindingForKey(std::string_view key) {
  for (auto& [id, binding] : bindings_) {
    if (binding->key() == key && binding->contents()) return binding.get();
  }
  return nullptr;
}

void ShellHostView::Event(std::string_view module, std::string_view event,
                          base::Value payload) {
  if (handler_) handler_->SendEvent(module, event, payload);
}

void ShellHostView::ReportTab(int id, base::DictValue event) {
  Event("ArdiaWebView", base::NumberToString(id), base::Value(std::move(event)));
}

base::DictValue ShellHostView::Strip() const {
  base::ListValue tabs;
  if (browser_) {
    TabStripModel* model = browser_->GetTabStripModel();
    for (int index = 0; index < model->count(); ++index) {
      content::WebContents* contents = model->GetWebContentsAt(index);
      const int id = TabId(contents);
      const auto found = bindings_.find(id);
      base::DictValue tab = base::DictValue().Set("key", found != bindings_.end()
                         ? base::Value(found->second->key()) : base::Value())
          .Set("browser", id)
          .Set("index", index)
          .Set("active", contents == model->GetActiveWebContents())
          .Set("pinned", model->IsTabPinned(index));
      tabs.Append(std::move(tab));
    }
  }
  return base::DictValue()
      .Set("strip", id_)
      .Set("window", id_)
      .Set("appWindow", window_id_)
      .Set("profile", "")
      .Set("tabs", std::move(tabs));
}

base::DictValue ShellHostView::Transaction(std::optional<int> command,
                                          bool advance) {
  base::ListValue strips;
  for (const auto& [id, host] : Hosts()) strips.Append(host->Strip());
  if (advance) ++Revision();
  return base::DictValue()
      .Set("rev", Revision())
      .Set("cmd", command ? base::Value(*command) : base::Value())
      .Set("strips", std::move(strips));
}

void ShellHostView::QueueTransaction(std::optional<int> command) {
  if (app_operation_) return;
  base::DictValue transaction = Transaction(command, true);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
          [](base::WeakPtr<ShellHostView> host, base::DictValue transaction) {
            if (!host) return;
            for (const auto& [id, target] : Hosts()) {
              target->Event("ArdiaCEF", "onTabStrip",
                             base::Value(transaction.Clone()));
            }
          },
          GetWeakPtr(), std::move(transaction)));
}

void ShellHostView::EngineEvent(std::string_view topic,
                                const base::DictValue& payload) {
  if (!profile()) return;
  base::DictValue translated = payload.Clone();
  if (translated.FindBool("offTheRecord").value_or(false)) return;
  if (const std::string* path = translated.FindString("profile")) {
    if (*path != profile()->GetPath().value() ||
        translated.FindBool("offTheRecord").value_or(false)) return;
    translated.Set("profile", "");
  }
  const auto json = base::WriteJson(translated);
  if (json) {
    Event("ArdiaCEF", "onEngineEvent", base::Value(
        base::DictValue().Set("topic", topic).Set("payload", *json)));
  }
}

void ShellHostView::ReattachViewports() {
  if (!browser_view_) return;
  browser_view_->contents_web_view()->SetWebContents(nullptr);
  browser_view_->contents_web_view()->SetVisible(false);
  for (auto& [id, binding] : bindings_) {
    if (binding->viewport() && binding->viewport()->GetVisible() &&
        binding->contents()) {
      binding->viewport()->SetWebContents(nullptr);
      binding->viewport()->SetWebContents(binding->contents());
    }
  }
}

void ShellHostView::OnTabStripModelChanged(
    TabStripModel*, const TabStripModelChange&,
    const TabStripSelectionChange&) {
  if (!browser_) return;
  TabStripModel* model = browser_->GetTabStripModel();
  for (int index = 0; index < model->count(); ++index) {
    content::WebContents* contents = model->GetWebContentsAt(index);
    TabBinding* binding = Binding(TabId(contents));
    if (binding && binding->contents() != contents) binding->Rebind(contents);
  }
  ReattachViewports();
  QueueTransaction(std::nullopt);
}

void ShellHostView::CreateTab(
    const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  const std::string key = String(args, "key");
  if (!browser_ || key.empty() || key.size() > 256 ||
      !DefaultProfileId(String(args, "profile"))) {
    return std::move(done).Run(false, base::Value("A key and the default regular profile are required"));
  }
  TabStripModel* model = browser_->GetTabStripModel();
  std::string text = String(args, "url");
  if (text.starts_with("ardia://")) text.replace(0, 8, "chrome://");
  const GURL url(text.empty() ? "about:blank" : text);
  if (!url.is_valid() || url.SchemeIs("javascript") ||
      (url.SchemeIs("chrome") && url.host() == "ardia")) {
    return std::move(done).Run(false, base::Value("Invalid or reserved tab URL"));
  }
  TabBinding* binding = BindingForKey(key);
  if (!binding) {
    for (const auto& [window, source] : Hosts()) {
      if (source == this) continue;
      TabBinding* existing = source->BindingForKey(key);
      if (!existing || !existing->contents()) continue;
      const int id = TabId(existing->contents());
      TabStripModel* source_model = source->browser()->GetTabStripModel();
      const int index = source_model->GetIndexOfWebContents(existing->contents());
      if (index < 0 || source->profile() != profile()) {
        return std::move(done).Run(false, base::Value("The transfer source is unavailable"));
      }
      source->app_operation_ = true;
      app_operation_ = true;
      auto moved = std::move(source->bindings_[id]);
      source->bindings_.erase(id);
      moved->MoveTo(this);
      auto contents = source_model->DetachWebContentsAtForInsertion(index);
      model->InsertWebContentsAt(model->count(), std::move(contents), AddTabTypes::ADD_NONE);
      bindings_[id] = std::move(moved);
      binding = bindings_[id].get();
      source->app_operation_ = false;
      app_operation_ = false;
      break;
    }
  }
  if (!binding) {
    content::WebContents* contents = nullptr;
    BrowserWindowInterface* source_browser = browser_;
    const std::string adopt = String(args, "adoptId");
    bool navigate_initial = false;
    if (!adopt.empty()) {
      int wanted = 0;
      const size_t colon = adopt.find(':');
      if (colon == std::string::npos ||
          (adopt.substr(0, colon) != "tab" && adopt.substr(0, colon) != "ardiacore") ||
          !base::StringToInt(adopt.substr(colon + 1), &wanted)) {
        return std::move(done).Run(false, base::Value("Invalid tab adoption id"));
      }
      GlobalBrowserCollection::GetInstance()->ForEach(
          [&](BrowserWindowInterface* candidate) {
            if (candidate->GetProfile() != profile() || candidate->GetProfile()->IsOffTheRecord()) return true;
            TabStripModel* strip = candidate->GetTabStripModel();
            for (int index = 0; index < strip->count(); ++index) {
              if (TabId(strip->GetWebContentsAt(index)) == wanted) {
                source_browser = candidate;
                contents = strip->GetWebContentsAt(index);
                return false;
              }
            }
            return true;
          });
      if (!contents) {
        return std::move(done).Run(false, base::Value("The offered tab is no longer available"));
      }
      for (const auto& [window, host] : Hosts()) {
        if (host->Binding(wanted) && host->Binding(wanted)->contents()) {
          return std::move(done).Run(false, base::Value("The offered tab is already claimed"));
        }
      }
    } else {
      for (int index = 0; index < model->count(); ++index) {
        content::WebContents* candidate = model->GetWebContentsAt(index);
        if (Binding(TabId(candidate)) && Binding(TabId(candidate))->contents()) continue;
        const GURL& initial = candidate->GetVisibleURL();
        const bool blank = initial.is_empty() || initial == GURL("about:blank") ||
            (initial.SchemeIs("chrome") && (initial.host() == "newtab" || initial.host() == "new-tab-page"));
        if (initial == url || (bindings_.empty() && blank)) {
          contents = candidate;
          navigate_initial = initial != url;
          break;
        }
      }
    }
    app_operation_ = true;
    if (contents && source_browser != browser_) {
      TabStripModel* source = source_browser->GetTabStripModel();
      ShellHostView* source_host = nullptr;
      const auto found = Hosts().find(source_browser->GetSessionID().id());
      if (found != Hosts().end()) {
        source_host = found->second;
        source_host->app_operation_ = true;
      }
      auto moving = source->DetachWebContentsAtForInsertion(source->GetIndexOfWebContents(contents));
      model->InsertWebContentsAt(model->count(), std::move(moving), AddTabTypes::ADD_NONE);
      if (source_host) source_host->app_operation_ = false;
    }
    if (!contents) {
      NavigateParams navigation(browser_, url, ui::PAGE_TRANSITION_TYPED);
      navigation.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
      navigation.window_action = NavigateParams::WindowAction::kNoAction;
      Navigate(&navigation);
      contents = navigation.navigated_or_inserted_contents;
    } else if (navigate_initial) {
      content::NavigationController::LoadURLParams params(url);
      params.transition_type = ui::PAGE_TRANSITION_TYPED;
      contents->GetController().LoadURLWithParams(params);
    }
    if (!contents || TabId(contents) <= 0) {
      app_operation_ = false;
      return std::move(done).Run(false, base::Value("Chrome could not create a tab with a valid id"));
    }
    const int id = TabId(contents);
    bindings_[id] = std::make_unique<TabBinding>(this, contents, key);
    binding = bindings_[id].get();
    app_operation_ = false;
  }
  const int id = TabId(binding->contents());
  base::DictValue result = base::DictValue()
      .Set("browserId", id).Set("chromeTabId", id).Set("state", binding->Navigation());
  std::move(done).Run(true, base::Value(std::move(result)));
  QueueTransaction(-1);
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
          [](base::WeakPtr<ShellHostView> host, int id) {
            if (host && host->Binding(id)) host->Binding(id)->ReportNavigation();
          }, GetWeakPtr(), id));
}

void ShellHostView::TabCommand(
    const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  const int id = args.FindInt("id").value_or(0);
  TabBinding* binding = Binding(id);
  if (!browser_ || !binding || !binding->contents()) {
    return std::move(done).Run(false, base::Value("No such live tab"));
  }
  content::WebContents* contents = binding->contents();
  auto& controller = contents->GetController();
  const std::string command = String(args, "command");
  const base::ListValue& values = Arguments(args);
  if (command == "loadUrl") {
    std::string text = ArgumentString(values, 0);
    if (text.starts_with("ardia://")) text.replace(0, 8, "chrome://");
    const GURL url(text);
    if (!url.is_valid() || url.SchemeIs("javascript") ||
        (url.SchemeIs("chrome") && url.host() == "ardia")) {
      return std::move(done).Run(false, base::Value("Invalid or reserved tab URL"));
    }
    content::NavigationController::LoadURLParams params(url);
    params.transition_type = ui::PAGE_TRANSITION_TYPED;
    controller.LoadURLWithParams(params);
  } else if (command == "goBack") {
    if (controller.CanGoBack()) controller.GoBack();
  } else if (command == "goForward") {
    if (controller.CanGoForward()) controller.GoForward();
  } else if (command == "reload" || command == "forceReload") {
    controller.Reload(command == "forceReload" ? content::ReloadType::BYPASSING_CACHE
                                                : content::ReloadType::NORMAL,
                       true);
  } else if (command == "stopLoading") {
    contents->Stop();
  } else if (command == "focus") {
    contents->Focus();
  } else if (command == "setMuted") {
    if (values.empty() || !values[0].is_bool()) {
      return std::move(done).Run(false, base::Value("setMuted requires a boolean"));
    }
    contents->SetAudioMuted(values[0].GetBool());
  } else if (command == "zoomStep") {
    if (values.empty() || !values[0].is_int() ||
        values[0].GetInt() < -1 || values[0].GetInt() > 1) {
      return std::move(done).Run(false, base::Value("zoomStep requires -1, 0 or 1"));
    }
    auto* zoom_controller = zoom::ZoomController::FromWebContents(contents);
    if (!zoom_controller) return std::move(done).Run(false, base::Value("No tab zoom controller"));
    const int step = values[0].GetInt();
    zoom_controller->SetZoomLevel(step == 0 ? 0 : zoom_controller->GetZoomLevel() + step);
  } else if (command == "navigationEntries") {
    base::ListValue entries;
    for (int index = 0; index < controller.GetEntryCount(); ++index) {
      const auto* entry = controller.GetEntryAtIndex(index);
      entries.Append(base::DictValue()
                         .Set("url", entry->GetVirtualURL().spec())
                         .Set("title", entry->GetTitle())
                         .Set("current", index == controller.GetCurrentEntryIndex()));
    }
    return std::move(done).Run(true, base::Value(std::move(entries)));
  } else if (command == "close") {
    const int index = browser_->GetTabStripModel()->GetIndexOfWebContents(contents);
    if (index < 0) return std::move(done).Run(false, base::Value("Tab is no longer in this window"));
    app_operation_ = true;
    browser_->GetTabStripModel()->CloseWebContentsAt(
        index, TabCloseTypes::CLOSE_USER_GESTURE | TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    app_operation_ = false;
    QueueTransaction(-1);
  } else {
    return std::move(done).Run(false, base::Value("Unsupported tab command: " + command));
  }
  std::move(done).Run(true, base::Value());
}

void ShellHostView::SetViewport(
    const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  TabBinding* binding = Binding(args.FindInt("id").value_or(0));
  const base::DictValue* rect = args.FindDict("rect");
  if (!binding || !binding->contents() || !rect) {
    return std::move(done).Run(false, base::Value("A live tab and viewport rectangle are required"));
  }
  const double x = rect->FindDouble("x").value_or(NAN);
  const double y = rect->FindDouble("y").value_or(NAN);
  const double width = rect->FindDouble("width").value_or(NAN);
  const double height = rect->FindDouble("height").value_or(NAN);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height) || width < 0 || height < 0) {
    return std::move(done).Run(false, base::Value("Viewport coordinates must be finite and nonnegative in size"));
  }
  const double scale = blink::ZoomLevelToZoomFactor(
      zoom::ZoomController::GetZoomLevelForWebContents(shell()));
  const double left = std::clamp(x * scale, 0.0, static_cast<double>(this->width()));
  const double top = std::clamp(y * scale, 0.0, static_cast<double>(this->height()));
  const double right = std::clamp((x + width) * scale, left, static_cast<double>(this->width()));
  const double bottom = std::clamp((y + height) * scale, top, static_cast<double>(this->height()));
  const gfx::Rect bounds = gfx::ToEnclosingRect(gfx::RectF(left, top, right - left, bottom - top));
  if (!binding->viewport()) {
    binding->SetViewport(AddChildView(std::make_unique<views::WebView>(profile())));
  }
  views::WebView* view = binding->viewport();
  const bool visible = args.FindBool("visible").value_or(false) && !bounds.IsEmpty();
  view->SetBoundsRect(bounds);
  view->SetVisible(visible);
  if (visible) {
    browser_view_->contents_web_view()->SetWebContents(nullptr);
    view->SetWebContents(binding->contents());
    view->holder()->SetNativeViewCornerRadii(gfx::RoundedCornersF(12.0f));
  } else {
    view->SetWebContents(nullptr);
  }
  std::move(done).Run(true, base::Value());
}

void ShellHostView::StripCommand(
    int command, const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  if (!browser_ || args.FindInt("strip").value_or(0) != id_) {
    return std::move(done).Run(false, base::Value("No such tab strip in this window"));
  }
  TabStripModel* model = browser_->GetTabStripModel();
  const std::string op = String(args, "op");
  bool rejected = false;
  app_operation_ = true;
  if (op == "activate") {
    TabBinding* binding = BindingForKey(String(args, "key"));
    const int index = binding ? model->GetIndexOfWebContents(binding->contents()) : -1;
    if (index < 0) rejected = true;
    else model->ActivateTabAt(index);
  } else if (op == "arrange") {
    const base::ListValue* keys = args.FindList("keys");
    if (!keys) {
      app_operation_ = false;
      return std::move(done).Run(false, base::Value("arrange requires a list of keys"));
    }
    const int pinned = std::max(0, args.FindInt("pinned").value_or(0));
    std::vector<content::WebContents*> listed;
    std::set<int> seen;
    for (const auto& key : *keys) {
      if (!key.is_string()) continue;
      TabBinding* binding = BindingForKey(key.GetString());
      if (!binding || !binding->contents()) continue;
      const int index = model->GetIndexOfWebContents(binding->contents());
      if (index >= 0 && seen.insert(TabId(binding->contents())).second) {
        listed.push_back(binding->contents());
      }
    }
    rejected = listed.empty() && !keys->empty();
    for (size_t index = 0; index < listed.size(); ++index) {
      const int at = model->GetIndexOfWebContents(listed[index]);
      model->SetTabPinned(at, static_cast<int>(index) < pinned);
    }
    for (size_t index = 0; index < listed.size(); ++index) {
      const int at = model->GetIndexOfWebContents(listed[index]);
      model->MoveWebContentsAt(at, static_cast<int>(index), false);
    }
  } else {
    app_operation_ = false;
    return std::move(done).Run(false, base::Value("Unsupported tab strip operation: " + op));
  }
  app_operation_ = false;
  base::DictValue transaction = Transaction(command, true);
  if (rejected) transaction.Set("rejected", true);
  std::move(done).Run(true, base::Value());
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
          [](base::WeakPtr<ShellHostView> host, base::DictValue transaction) {
            if (!host) return;
            for (const auto& [id, target] : Hosts()) {
              target->Event("ArdiaCEF", "onTabStrip", base::Value(transaction.Clone()));
            }
          }, GetWeakPtr(), std::move(transaction)));
}

void ShellHostView::Invoke(
    std::string method, const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  if (!browser_) return std::move(done).Run(false, base::Value("The native browser window has closed"));
  const base::ListValue& values = Arguments(args);
  if (method == "tab.create") return CreateTab(args, std::move(done));
  if (method == "tab.command") return TabCommand(args, std::move(done));
  if (method == "tab.viewport") return SetViewport(args, std::move(done));
  if (method == "tab.close") {
    TabBinding* binding = BindingForKey(String(args, "key"));
    if (!binding || !binding->contents()) {
      return std::move(done).Run(false, base::Value("No such claimed live tab"));
    }
    base::DictValue close = base::DictValue()
        .Set("id", TabId(binding->contents())).Set("command", "close");
    return TabCommand(close, std::move(done));
  }
  if (method == "tab.release") {
    TabBinding* binding = Binding(args.FindInt("id").value_or(0));
    if (!binding) return std::move(done).Run(false, base::Value("No such live tab"));
    if (binding->viewport()) {
      binding->viewport()->SetVisible(false);
      binding->viewport()->SetWebContents(nullptr);
    }
    return std::move(done).Run(true, base::Value());
  }
  if (method == "session.read") {
    FileRunner()->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&ReadSession, DocumentDirectory(profile())),
        base::BindOnce(
            [](base::OnceCallback<void(bool, base::Value)> done,
               std::optional<base::DictValue> session) {
              std::move(done).Run(true, session ? base::Value(std::move(*session))
                                                : base::Value());
            }, std::move(done)));
    return;
  }
  if (method == "session.write") {
    const base::DictValue* base = args.FindDict("base");
    const base::DictValue* local = args.FindDict("session");
    if (!base || !local || !ValidLinuxSession(*base) || !ValidLinuxSession(*local)) {
      return std::move(done).Run(false, base::Value("session.write requires bounded base and session objects"));
    }
    const base::FilePath profile_path = profile()->GetPath();
    const std::string source_window = window_id_;
    FileRunner()->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&MergeAndWriteSession,
                                  DocumentDirectory(profile()),
                                  base->Clone(), local->Clone()),
        base::BindOnce(
            [](base::FilePath profile_path, std::string source_window,
               base::OnceCallback<void(bool, base::Value)> done,
               SessionWriteResult result) {
              if (!result.ok) {
                return std::move(done).Run(false, base::Value("Could not merge and persist the session"));
              }
              std::move(done).Run(true, base::Value(result.session.Clone()));
              if (result.previous == result.session) return;
              base::DictValue event = base::DictValue()
                  .Set("base", std::move(result.previous))
                  .Set("session", std::move(result.session))
                  .Set("sourceWindowId", source_window);
              for (const auto& [id, host] : Hosts()) {
                Profile* profile = host->profile();
                if (profile && !profile->IsOffTheRecord() &&
                    profile->GetPath() == profile_path) {
                  host->Event("ArdiaHost", "session", base::Value(event.Clone()));
                }
              }
            }, profile_path, source_window, std::move(done)));
    return;
  }
  if (method == "ArdiaShell.readDocument") {
    const std::string name = ArgumentString(values, 0);
    if (!linux_policy::ValidDocumentName(name) || name == kSessionName) {
      return std::move(done).Run(false, base::Value("Invalid or reserved document name"));
    }
    FileRunner()->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&ReadDocument, DocumentDirectory(profile()), name),
        base::BindOnce(
            [](base::OnceCallback<void(bool, base::Value)> done,
               std::optional<std::string> contents) {
              std::move(done).Run(true, contents ? base::Value(*contents) : base::Value());
            }, std::move(done)));
    return;
  }
  if (method == "ArdiaShell.writeDocument" || method == "ArdiaShell.saveDocument" ||
      method == "ArdiaShell.removeDocument") {
    const std::string name = ArgumentString(values, 0);
    if (!linux_policy::ValidDocumentName(name) || name == kSessionName) {
      return std::move(done).Run(false, base::Value("Invalid or reserved document name"));
    }
    std::optional<std::string> contents;
    if (method != "ArdiaShell.removeDocument") {
      if (values.size() < 2 || !values[1].is_string()) {
        return std::move(done).Run(false, base::Value("Document text is required"));
      }
      contents = values[1].GetString();
    }
    FileRunner()->PostTaskAndReplyWithResult(
        FROM_HERE, base::BindOnce(&WriteDocument, DocumentDirectory(profile()), name, std::move(contents)),
        base::BindOnce(
            [](base::OnceCallback<void(bool, base::Value)> done, bool ok) {
              std::move(done).Run(ok, ok ? base::Value() : base::Value("Could not persist the document"));
            }, std::move(done)));
    return;
  }

  if (method == "ArdiaCEF.tabStrips") {
    return std::move(done).Run(true, base::Value(Transaction(std::nullopt, false)));
  }
  if (method == "ArdiaCEF.tabStripCommand") {
    if (values.size() < 2 || !values[0].is_int() || !values[1].is_dict()) {
      return std::move(done).Run(false, base::Value("A command id and tab strip command are required"));
    }
    ShellHostView* target = this;
    const int strip = values[1].GetDict().FindInt("strip").value_or(0);
    const auto found = Hosts().find(strip);
    if (found != Hosts().end()) target = found->second;
    return target->StripCommand(values[0].GetInt(), values[1].GetDict(), std::move(done));
  }
  if (method == "ArdiaCEF.engineInfo") {
    int tabs = 0;
    int windows = 0;
    int popups = 0;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* browser) {
          if (browser->GetProfile() != profile()) return true;
          tabs += browser->GetTabStripModel()->count();
          ++windows;
          if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL) ++popups;
          return true;
        });
    return std::move(done).Run(true, base::Value(base::DictValue()
        .Set("pid", static_cast<int>(base::GetCurrentProcId()))
        .Set("dataDirectory", profile()->GetPath().value())
        .Set("engine", "Chromium")
        .Set("chromiumVersion", version_info::GetVersionNumber())
        .Set("liveBrowsers", tabs).Set("popupWindows", popups)
        .Set("chromeWindows", windows)));
  }
  if (method == "ArdiaCEF.chromeWindows") {
    base::ListValue windows;
    for (const auto& [id, host] : Hosts()) {
      const auto* window = host->browser()->GetWindow();
      TabStripModel* strip = host->browser()->GetTabStripModel();
      const int active_id = strip->GetActiveWebContents() ? TabId(strip->GetActiveWebContents()) : 0;
      const int first_id = strip->count() ? TabId(strip->GetWebContentsAt(0)) : 0;
      windows.Append(base::DictValue()
          .Set("profile", "").Set("window", id)
          .Set("frame", window->GetBounds().ToString())
          .Set("alpha", 1.0).Set("key", window->IsActive())
          .Set("canBecomeKey", true).Set("visible", window->IsVisible())
          .Set("parentWindow", 0).Set("chromeWindows", 1)
          .Set("anchorBrowserId", active_id).Set("anyTabBrowserId", first_id)
          .Set("ready", true).Set("active", window->IsActive())
          .Set("pageInsets", base::ListValue().Append(0).Append(0).Append(0).Append(0))
          .Set("hosting", true).Set("group", host->window_id())
          .Set("hasRoot", true).Set("translucent", false));
    }
    return std::move(done).Run(true, base::Value(std::move(windows)));
  }
  if (method == "ArdiaShell.windowIds") {
    base::ListValue ids;
    for (const auto& [id, host] : Hosts()) ids.Append(host->window_id());
    return std::move(done).Run(true, base::Value(std::move(ids)));
  }
  if (method == "ArdiaShell.windowFrame" || method == "ArdiaShell.closeWindow" ||
      method == "ArdiaShell.setWindowTitle") {
    ShellHostView* target = nullptr;
    for (const auto& [id, host] : Hosts()) {
      if (host->window_id() == ArgumentString(values, 0)) target = host;
    }
    if (!target) return std::move(done).Run(false, base::Value("No such native window"));
    if (method == "ArdiaShell.windowFrame") {
      const gfx::Rect bounds = target->browser()->GetWindow()->GetBounds();
      return std::move(done).Run(true, base::Value(base::ListValue()
          .Append(bounds.x()).Append(bounds.y()).Append(bounds.width()).Append(bounds.height())));
    }
    if (method == "ArdiaShell.setWindowTitle") {
      target->browser()->GetWindow()->GetNativeWindow()->SetTitle(
          base::UTF8ToUTF16(ArgumentString(values, 1)));
    } else {
      const auto weak = target->browser()->GetWeakPtr();
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce([](base::WeakPtr<BrowserWindowInterface> browser) {
            if (browser) browser->GetWindow()->Close();
          }, weak));
    }
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaShell.openWindow") {
    const std::string id = ArgumentString(values, 0);
    const base::DictValue* options = values.size() > 1 && values[1].is_dict()
                                       ? &values[1].GetDict() : nullptr;
    if (id.empty() || id.size() > 256 ||
        (options && (options->FindBool("incognito").value_or(false) ||
                     !DefaultProfileId(String(*options, "profile"))))) {
      return std::move(done).Run(false, base::Value("Only regular default-profile windows are supported"));
    }
    for (const auto& [native_id, host] : Hosts()) {
      if (host->window_id() == id) return std::move(done).Run(true, base::Value());
    }
    if (window_id_ == "main") {
      window_id_ = id;
      if (options && options->FindList("frame") && options->FindList("frame")->size() == 4) {
        const auto& frame = *options->FindList("frame");
        if (std::ranges::all_of(frame, [](const base::Value& value) { return value.is_int(); }) &&
            frame[2].GetInt() > 0 && frame[3].GetInt() > 0) {
          browser_->GetWindow()->SetBounds(gfx::Rect(frame[0].GetInt(), frame[1].GetInt(),
                                                    frame[2].GetInt(), frame[3].GetInt()));
        }
      }
      return std::move(done).Run(true, base::Value());
    }
    BrowserWindowCreateParams params(profile(), true);
    BrowserWindowInterface* created = CreateBrowserWindow(std::move(params));
    if (!created) return std::move(done).Run(false, base::Value("Chrome could not create a window"));
    RequestedWindows()[created->GetSessionID().id()] = id;
    created->GetWindow()->ShowInactive();
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaShell.isDarkAppearance") {
    return std::move(done).Run(true, base::Value(
        ui::NativeTheme::GetInstanceForNativeUi()->preferred_color_scheme() ==
        ui::NativeTheme::PreferredColorScheme::kDark));
  }
  if (method == "ArdiaShell.copyText") {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(base::UTF8ToUTF16(ArgumentString(values, 0)));
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaShell.setMenuState" || method == "ArdiaShell.setMenuBookmarks" ||
      method == "ArdiaCEF.setDisplayMediaPicker" || method == "ArdiaChromeUI.watchCastRoutes" ||
      method == "ArdiaCEF.prepareTransfer" || method == "ArdiaCEF.releaseTransfer") {
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaCEF.releaseProfile" && DefaultProfileId(ArgumentString(values, 0))) {
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaCEF.setSearchEngineName") {
    const std::string name = ArgumentString(values, 0);
    if (name.empty()) return std::move(done).Run(true, base::Value());
    TemplateURLService* service = TemplateURLServiceFactory::GetForProfile(profile());
    if (!service) return std::move(done).Run(false, base::Value("No search engine service"));
    service->Load();
    if (!service->loaded()) return std::move(done).Run(false, base::Value("Search engines have not loaded yet"));
    for (TemplateURL* engine : service->GetTemplateURLs()) {
      if (engine->type() == TemplateURL::NORMAL &&
          base::EqualsCaseInsensitiveASCII(base::UTF16ToUTF8(engine->short_name()), name)) {
        service->SetUserSelectedDefaultSearchProvider(engine);
        return std::move(done).Run(true, base::Value());
      }
    }
    return std::move(done).Run(false, base::Value("No installed search engine named " + name));
  }
  if (method == "ArdiaExtensions.list") {
    if (!DefaultProfileId(ArgumentString(values, 0))) {
      return std::move(done).Run(false, base::Value("Only the default profile is supported"));
    }
    struct Pending {
      base::OnceCallback<void(bool, base::Value)> done;
    };
    ac_engine_call("ac_extensions_list", profile()->GetPath().value().c_str(), "{}",
                   [](void* context, const char* json) {
                     std::unique_ptr<Pending> pending(static_cast<Pending*>(context));
                     auto value = base::JSONReader::Read(json ? json : "{}", base::JSON_PARSE_RFC);
                     if (!value || !value->is_dict()) {
                       return std::move(pending->done).Run(false, base::Value("Invalid extensions service response"));
                     }
                     const std::string* error = value->GetDict().FindString("error");
                     if (error) return std::move(pending->done).Run(false, base::Value(*error));
                     std::move(pending->done).Run(true, std::move(*value));
                   }, new Pending{std::move(done)});
    return;
  }
  std::move(done).Run(false, base::Value("Unsupported Linux host method: " + method));
}

TabBinding::TabBinding(ShellHostView* owner, content::WebContents* contents,
                       std::string key)
    : WebContentsObserver(contents), owner_(owner), contents_(contents),
      key_(std::move(key)) {}

TabBinding::~TabBinding() {
  if (viewport_) {
    views::WebView* view = viewport_.get();
    viewport_ = nullptr;
    view->SetWebContents(nullptr);
    owner_->RemoveChildViewT(view);
  }
}

void TabBinding::Rebind(content::WebContents* contents) {
  Observe(contents);
  contents_ = contents;
  if (viewport_ && viewport_->GetVisible()) viewport_->SetWebContents(contents);
}

void TabBinding::MoveTo(ShellHostView* owner) {
  if (viewport_) {
    views::WebView* view = viewport_.get();
    viewport_ = nullptr;
    view->SetWebContents(nullptr);
    owner_->RemoveChildViewT(view);
  }
  owner_ = owner;
}

void TabBinding::DidOpenRequestedURL(
    content::WebContents* opened, content::RenderFrameHost*, const GURL& url,
    const content::Referrer&, WindowOpenDisposition disposition,
    ui::PageTransition, bool, bool) {
  if (!contents_ || !opened) return;
  std::string_view target;
  switch (disposition) {
    case WindowOpenDisposition::NEW_FOREGROUND_TAB: target = "foreground"; break;
    case WindowOpenDisposition::NEW_BACKGROUND_TAB: target = "background"; break;
    case WindowOpenDisposition::NEW_POPUP: target = "popup"; break;
    case WindowOpenDisposition::NEW_WINDOW: target = "window"; break;
    default: return;
  }
  const int id = TabId(opened);
  if (id <= 0 || opened->GetBrowserContext() != contents_->GetBrowserContext()) return;
  owner_->ReportTab(TabId(contents_), base::DictValue()
      .Set("type", "openWindow").Set("value", base::DictValue()
          .Set("url", url.spec()).Set("disposition", target)
          .Set("adoptId", "tab:" + base::NumberToString(id)).Set("profile", "")));
}

base::DictValue TabBinding::Navigation() const {
  if (!contents_) return {};
  auto& controller = contents_->GetController();
  return base::DictValue()
      .Set("url", contents_->GetVisibleURL().spec())
      .Set("title", contents_->GetTitle())
      .Set("canGoBack", controller.CanGoBack())
      .Set("canGoForward", controller.CanGoForward())
      .Set("isLoading", contents_->IsLoading());
}

void TabBinding::ReportNavigation() {
  if (contents_) {
    owner_->ReportTab(TabId(contents_), base::DictValue()
        .Set("type", "navigation").Set("state", Navigation()));
  }
}

void TabBinding::DidFinishNavigation(content::NavigationHandle* handle) {
  if (handle->IsInPrimaryMainFrame()) ReportNavigation();
}
void TabBinding::DidStartLoading() { ReportNavigation(); }
void TabBinding::DidStopLoading() { ReportNavigation(); }
void TabBinding::LoadProgressChanged(double progress) {
  if (contents_) owner_->ReportTab(TabId(contents_), base::DictValue()
      .Set("type", "progress").Set("progress", std::clamp(progress, 0.0, 1.0)));
}
void TabBinding::TitleWasSet(content::NavigationEntry*) {
  if (contents_) owner_->ReportTab(TabId(contents_), base::DictValue()
      .Set("type", "title").Set("title", contents_->GetTitle()));
}
void TabBinding::DidFailLoad(content::RenderFrameHost* frame, const GURL& url,
                            int error_code) {
  if (!contents_ || frame != contents_->GetPrimaryMainFrame()) return;
  owner_->ReportTab(TabId(contents_), base::DictValue()
      .Set("type", "loadError").Set("error", base::DictValue()
          .Set("url", url.spec()).Set("code", error_code)
          .Set("text", net::ErrorToShortString(error_code))));
}
void TabBinding::DidUpdateFaviconURL(
    content::RenderFrameHost* frame,
    const std::vector<blink::mojom::FaviconURLPtr>& candidates,
    blink::mojom::FaviconUpdateReason) {
  if (!contents_ || frame != contents_->GetPrimaryMainFrame()) return;
  base::ListValue urls;
  for (const auto& candidate : candidates) urls.Append(candidate->icon_url.spec());
  owner_->ReportTab(TabId(contents_), base::DictValue()
      .Set("type", "favicon")
      .Set("url", candidates.empty() ? "" : candidates.front()->icon_url.spec())
      .Set("candidates", std::move(urls)));
}
void TabBinding::WebContentsDestroyed() {
  const int id = contents_ ? TabId(contents_) : 0;
  contents_ = nullptr;
  if (viewport_) viewport_->SetWebContents(nullptr);
  if (id > 0) {
    owner_->ReportTab(id, base::DictValue().Set("type", "windowClose"));
  }
}

std::map<int, std::string>& RequestedWindows() {
  static base::NoDestructor<std::map<int, std::string>> ids;
  return *ids;
}

class HostRegistry : public BrowserCollectionObserver {
 public:
  void Start() {
    observation_.Observe(GlobalBrowserCollection::GetInstance());
    GlobalBrowserCollection::GetInstance()->ForEach(
        [this](BrowserWindowInterface* browser) {
          Attach(browser->GetWeakPtr(), true);
          return true;
        });
  }

 private:
  void Attach(base::WeakPtr<BrowserWindowInterface> browser, bool startup) {
    if (!browser || browser->GetProfile()->IsOffTheRecord() ||
        browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
        Hosts().contains(browser->GetSessionID().id())) return;
    if (profile_path_.empty()) profile_path_ = browser->GetProfile()->GetPath();
    if (browser->GetProfile()->GetPath() != profile_path_) return;
    BrowserView* view = BrowserView::GetBrowserViewForBrowser(browser.get());
    if (!view || !view->GetWidget()) {
      LOG(WARNING) << "Ardia Linux host could not attach to a Chrome BrowserView";
      return;
    }
    auto overlay = std::make_unique<ShellHostView>(browser.get(), view, startup);
    auto* shell = overlay.get();
    if (const auto found = RequestedWindows().find(shell->id());
        found != RequestedWindows().end()) {
      shell->set_window_id(std::move(found->second));
      RequestedWindows().erase(found);
    }
    view->AddChildView(std::move(overlay));
    shell->Initialize();
  }
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&HostRegistry::Attach, base::Unretained(this),
                                  browser->GetWeakPtr(), false));
  }
  void OnBrowserClosed(BrowserWindowInterface* browser) override {
    const auto found = Hosts().find(browser->GetSessionID().id());
    if (found == Hosts().end()) return;
    base::DictValue strip = found->second->Strip();
    strip.Set("closed", true);
    strip.Set("tabs", base::ListValue());
    found->second->DetachBrowser();
    base::DictValue tx = base::DictValue()
        .Set("rev", ++Revision()).Set("cmd", base::Value())
        .Set("strips", base::ListValue().Append(std::move(strip)));
    for (const auto& [id, host] : Hosts()) {
      host->Event("ArdiaCEF", "onTabStrip", base::Value(tx.Clone()));
    }
  }

  base::FilePath profile_path_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation_{this};
};

void EngineEvent(void*, const char* topic, const char* json) {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  if (!topic || !json) return;
  const auto payload = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!payload || !payload->is_dict()) return;
  for (const auto& [id, host] : Hosts()) host->EngineEvent(topic, payload->GetDict());
}

}

void StartLinuxHost() {
  CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
  static bool started = false;
  if (std::exchange(started, true)) return;
  content::WebUIConfigMap::GetInstance().AddWebUIConfig(std::make_unique<ShellConfig>());
  g_ardia_keeps_window_without_tabs = [](const BrowserWindowInterface* browser) {
    return browser && Hosts().contains(browser->GetSessionID().id());
  };
  ac_engine_set_event_sink(EngineEvent, nullptr);
  static base::NoDestructor<HostRegistry> registry;
  registry->Start();
}

}
