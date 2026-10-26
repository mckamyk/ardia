#include "ardia/linux/ac_linux_host.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ardia/linux/ac_linux_policy.h"
#include "ardia/linux/ac_linux_contract.h"
#include "ardia/linux/ac_linux_launch_config.h"
#include "ardia/core/ac_launch_switches.h"
#include "ardia/linux/ac_linux_session.h"
#include "ardia/linux/host_bootstrap.h"
#include "base/base_paths.h"
#include "base/base64.h"
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
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ardia/public/ac_engine.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/picture_in_picture/auto_picture_in_picture_tab_helper.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_removed_reason.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window_state.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "components/search_engines/template_url.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/sessions/core/session_id.h"
#include "components/version_info/version_info.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/page.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
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
#include "third_party/blink/public/common/frame/user_activation_state.h"
#include "third_party/blink/public/mojom/favicon/favicon_url.mojom.h"
#include "ui/aura/window.h"
#include "ui/aura/window_targeter.h"
#include "ui/compositor/layer.h"
#include "ui/aura/window_observer.h"
#include "ui/base/base_window.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/events/event_constants.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/view_observer.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/window_modality_controller.h"
#include "url/gurl.h"
#include "url/origin.h"

extern bool (*g_ardia_keeps_window_without_tabs)(
    const BrowserWindowInterface* browser);
extern bool g_ardia_auto_picture_in_picture_off;
extern int (*g_ardia_linux_tab_shown)(content::WebContents*);
extern void (*g_ardia_linux_auto_pip_requested)(content::WebContents*);

namespace ardia {
namespace {

constexpr size_t kDocumentLimit = 8 * 1024 * 1024;
constexpr size_t kAssetLimit = 32 * 1024 * 1024;
constexpr char kSessionName[] = "linux-session.json";

class ShellHostView;
class HostMessageHandler;

class ViewportTargeter : public aura::WindowTargeter {
 public:
  explicit ViewportTargeter(std::vector<gfx::Rect> shape)
      : shape_(std::move(shape)) {}

  std::unique_ptr<HitTestRects> GetExtraHitTestShapeRects(
      aura::Window* target) const override {
    if (target != window()) return nullptr;
    return std::make_unique<HitTestRects>(shape_);
  }

 private:
  const std::vector<gfx::Rect> shape_;
};

class ViewportView : public views::WebView,
                     public views::ViewTargeterDelegate {
 public:
  METADATA_HEADER(ViewportView, views::WebView)

 public:
  explicit ViewportView(Profile* profile) : views::WebView(profile) {
    SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
  }

  ~ViewportView() override { SetWebContents(nullptr); }

  void SetShape(std::vector<gfx::Rect> paint_shape,
                std::vector<gfx::Rect> shape) {
    shape_ = std::move(shape);
    paint_shape_ = std::move(paint_shape);
    ApplyTargeter();
  }

  void SetWebContents(content::WebContents* contents) override {
    if (auto* previous = web_contents(); previous && previous != contents) {
      if (auto* native = previous->GetNativeView()) native->layer()->SetAlphaShape(nullptr);
    }
    views::WebView::SetWebContents(contents);
    ApplyTargeter();
  }

  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    return target == this && std::ranges::any_of(
        shape_, [&](const gfx::Rect& part) { return part.Intersects(rect); });
  }

 private:
  void ApplyTargeter() {
    if (auto* contents = web_contents()) {
      aura::Window* native = contents->GetNativeView();
      if (!native) return;
      native->layer()->SetAlphaShape(
          std::make_unique<ui::Layer::ShapeRects>(paint_shape_));
      native->SetEventTargeter(
          std::make_unique<ViewportTargeter>(shape_));
    }
  }

  std::vector<gfx::Rect> shape_;
  std::vector<gfx::Rect> paint_shape_;
};

BEGIN_METADATA(ViewportView)
END_METADATA

struct RequestedWindow {
  std::string id;
  std::string kind;
  std::string title;
  base::DictValue utility;
  base::WeakPtr<BrowserWindowInterface> browser;
};
std::map<int, RequestedWindow>& RequestedWindows();

void CloseNativeWindow(BrowserWindowInterface* browser) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce([](base::WeakPtr<BrowserWindowInterface> target) {
        if (target) target->GetWindow()->Close();
      }, browser->GetWeakPtr()));
}

void FocusNativeWindow(BrowserWindowInterface* browser) {
  const auto* command = base::CommandLine::ForCurrentProcess();
  if (ardiacore::BackgroundInstance() ||
      command->HasSwitch("headless") ||
      command->GetSwitchValueASCII("ozone-platform") == "headless") {
    browser->GetWindow()->ShowInactive();
  } else {
    browser->GetWindow()->Show();
    browser->GetWindow()->Activate();
  }
}

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

bool ValidUtilityPlacement(const base::DictValue* placement) {
  if (!placement || !placement->FindBool("maximized")) return false;
  const auto left = placement->FindInt("left");
  const auto top = placement->FindInt("top");
  const auto right = placement->FindInt("right");
  const auto bottom = placement->FindInt("bottom");
  if (!left || !top || !right || !bottom ||
      *left < -1000000 || *left > 1000000 ||
      *top < -1000000 || *top > 1000000) return false;
  const int64_t width = static_cast<int64_t>(*right) - *left;
  const int64_t height = static_cast<int64_t>(*bottom) - *top;
  return width >= 720 && height >= 480 && width <= 16384 && height <= 16384;
}

std::optional<base::DictValue> ExtensionConfiguration(const base::DictValue& input) {
  const auto* id = input.FindString("id");
  const auto* options = input.FindDict("options");
  if (input.size() != 2 || !id || id->empty() || !options || options->empty()) return std::nullopt;
  base::DictValue result;
  result.Set("id", *id);
  for (const auto& [key, value] : *options) {
    if (key == "pinned" || key == "fileAccess" || key == "incognito") {
      if (!value.is_bool()) return std::nullopt;
    } else if (key == "siteAccess") {
      if (!value.is_string() || (value.GetString() != "allSites" && value.GetString() != "specificSites" && value.GetString() != "onClick")) return std::nullopt;
    } else {
      return std::nullopt;
    }
    result.Set(key, value.Clone());
  }
  return result;
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
  std::string claim;
  uint64_t renderer_generation = 0;
  bool transferable = false;
  bool temporary = false;
  content::WebContents* contents() const { return contents_; }
  views::WebView* viewport() const { return viewport_; }
  base::DictValue Navigation() const;
  void ReportNavigation();
  void MoveTo(ShellHostView* owner);
  void Rebind(content::WebContents* contents);
  void SetViewport(views::WebView* view) { viewport_ = view; }
  bool presented = false;
  bool presentation_applied = false;
  bool presentation_attached = false;
  bool presentation_departed = false;
  base::TimeTicks cancelled_auto_pip_until;
  base::TimeTicks requested_auto_pip_until;
  void RememberPendingAutoPictureInPicture() {
    auto* helper = contents_ ? AutoPictureInPictureTabHelper::FromWebContents(contents_) : nullptr;
    if (base::TimeTicks::Now() < requested_auto_pip_until ||
        (helper && helper->AreAutoPictureInPicturePreconditionsMet())) {
      cancelled_auto_pip_until = base::TimeTicks::Now() + blink::kActivationLifespan;
    }
  }

 private:
  void PrimaryPageChanged(content::Page&) override {
    cancelled_auto_pip_until = {};
    requested_auto_pip_until = {};
  }
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

class WindowKeyboardHandler : public ui::EventHandler {
 public:
  explicit WindowKeyboardHandler(base::RepeatingCallback<void(ui::KeyEvent*)> handle)
      : handle_(std::move(handle)) {}
  void OnKeyEvent(ui::KeyEvent* event) override { handle_.Run(event); }

 private:
  base::RepeatingCallback<void(ui::KeyEvent*)> handle_;
};

class ShellHostView : public views::View,
                      public views::ViewObserver,
                      public aura::WindowObserver,
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
  bool is_utility() const { return !kind_.empty(); }
  void ConfigureWindow(RequestedWindow request) {
    window_id_ = std::move(request.id);
    kind_ = std::move(request.kind);
    title_ = std::move(request.title);
    utility_ = std::move(request.utility);
  }
  void NavigateUtility(base::DictValue utility) {
    utility_ = std::move(utility);
    if (utility_ready_) {
      Event("ArdiaShell", "onUtilityNavigation", base::Value(utility_.Clone()));
    }
  }
  BrowserWindowInterface* browser() const { return browser_; }
  content::WebContents* shell() const { return shell_->GetWebContents(); }
  Profile* profile() const { return browser_ ? browser_->GetProfile() : nullptr; }
  bool Trusted(content::WebUI* ui) const;
  base::DictValue Bootstrap() const;
  base::DictValue Strip() const;
  base::DictValue Transaction(std::optional<int> command, bool advance);
  uint64_t SetHandler(HostMessageHandler* handler, content::WebUI* ui) {
    if (!Trusted(ui)) return 0;
    auto* frame = ui->GetRenderFrameHost();
    if (handler_ == handler && renderer_document_.AsRenderFrameHostIfValid() == frame) return renderer_generation_;
    auto document = frame->GetWeakDocumentPtr();
    handler_ = nullptr;
    auto weak = GetWeakPtr();
    RetireRenderer();
    auto* committed = document.AsRenderFrameHostIfValid();
    if (!weak || !committed || committed != weak->shell()->GetPrimaryMainFrame() || committed->GetWebUI() != ui) return 0;
    weak->handler_ = handler;
    weak->renderer_document_ = std::move(document);
    return weak->renderer_generation_;
  }
  bool OwnsHandler(HostMessageHandler* handler, uint64_t generation) const {
    auto* frame = renderer_document_.AsRenderFrameHostIfValid();
    return handler && handler_ == handler && renderer_generation_ == generation && frame && frame == shell()->GetPrimaryMainFrame();
  }
  bool ClearHandler(HostMessageHandler* handler) {
    if (!handler || handler_ != handler) return false;
    handler_ = nullptr;
    RetireRenderer();
    return true;
  }
  void RetireRenderer() {
    ++renderer_generation_;
    renderer_document_ = content::WeakDocumentPtr();
    std::vector<int> temporary;
    for (auto& [id, binding] : bindings_) {
      binding->claim.clear();
      binding->renderer_generation = 0;
      binding->transferable = false;
      if (binding->temporary && binding->contents()) temporary.push_back(id);
    }
    utility_ready_ = false;
    switcher_capture_ = false;
    switcher_source_.reset();
    if (focus_manager_ && shortcut_recording_) {
      focus_manager_->set_shortcut_handling_suspended(false);
      shortcut_recording_ = false;
    }
    DetachViewports();
    auto weak = GetWeakPtr();
    for (const int id : temporary) {
      if (!weak) return;
      auto* binding = weak->Binding(id);
      if (binding && binding->temporary && binding->contents()) weak->DisposeTemporary(binding);
    }
  }
  void Event(std::string_view module, std::string_view event,
             base::Value payload);
  void QueueTransaction(std::optional<int> command);
  void EngineEvent(std::string_view topic, const base::DictValue& payload);
  void Invoke(std::string method, const base::DictValue& args,
              base::OnceCallback<void(bool, base::Value)> done);
  void ReportTab(int id, base::DictValue event);
  void OfferTab(content::WebContents* contents) {
    if (contents && contents->GetBrowserContext() == profile()) offered_[TabId(contents)] = contents->GetWeakPtr();
  }
  int TabShown(content::WebContents* contents) const {
    for (const auto& [id, binding] : bindings_) {
      if (binding->contents() == contents) {
        if (binding->presented) return 1;
        return binding->presentation_attached && binding->presentation_departed ? 0 : 2;
      }
    }
    return -1;
  }
  bool AutoPictureInPictureCancelled(content::WebContents* contents) const {
    for (const auto& [id, binding] : bindings_) {
      if (binding->contents() == contents) {
        return base::TimeTicks::Now() < binding->cancelled_auto_pip_until;
      }
    }
    return false;
  }
  void NoteAutoPictureInPictureRequested(content::WebContents* contents) {
    for (const auto& [id, binding] : bindings_) {
      if (binding->contents() != contents) continue;
      binding->requested_auto_pip_until = base::TimeTicks::Now() + blink::kActivationLifespan;
      binding->cancelled_auto_pip_until = {};
    }
  }
  void ConsumeAutoPictureInPictureCancellation(content::WebContents* contents) {
    for (const auto& [id, binding] : bindings_) {
      if (binding->contents() != contents) continue;
      binding->requested_auto_pip_until = {};
      binding->cancelled_auto_pip_until = {};
    }
  }
  base::WeakPtr<ShellHostView> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  std::map<int, base::WeakPtr<content::WebContents>> offered_;
  friend class HostMessageHandler;
  TabBinding* Binding(int id);
  TabBinding* ClaimedBinding(const base::DictValue& args);
  bool CloseTab(TabBinding* binding);
  bool DisposeTemporary(TabBinding* binding);
  void ClaimLaunchTab(const base::ListValue& values, base::OnceCallback<void(bool, base::Value)> done);
  bool AdoptLaunchOffer(int tab, std::string_view key, std::string_view url);
  ardiacore::linux_contract::LaunchDecision launch_decision_;
  TabBinding* BindingForKey(std::string_view key);
  void CreateTab(const base::DictValue& args,
                 base::OnceCallback<void(bool, base::Value)> done);
  void TabCommand(const base::DictValue& args,
                  base::OnceCallback<void(bool, base::Value)> done);
  void SetViewport(const base::DictValue& args,
                   base::OnceCallback<void(bool, base::Value)> done);
  void SetPresentations(const base::DictValue& args,
                       base::OnceCallback<void(bool, base::Value)> done);
  void StripCommand(int command, const base::DictValue& args,
                    base::OnceCallback<void(bool, base::Value)> done);
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  bool CanHandleAccelerators() const override;
  void UnregisterAccelerators();
  void FocusShell();
  void OnKeyEvent(ui::KeyEvent* event) override;
  bool OwnsKeyboardEvent(const ui::KeyEvent& event) const;
  void OnWindowDestroying(aura::Window* window) override;
  void ArmUtilityClose();
  void RequestUtilityClose();
  void CloseUtilityAfterSave();
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
  uint64_t renderer_generation_ = 0;
  content::WeakDocumentPtr renderer_document_;
  raw_ptr<views::FocusManager> focus_manager_ = nullptr;
  std::unique_ptr<WindowKeyboardHandler> input_handler_;
  std::map<ui::Accelerator, base::DictValue> shortcuts_;
  bool shortcut_recording_ = false;
  bool switcher_capture_ = false;
  base::WeakPtr<content::WebContents> switcher_source_;
  int id_;
  bool startup_;
  std::string window_id_;
  std::string kind_;
  std::string title_;
  base::DictValue utility_;
  bool utility_ready_ = false;
  bool utility_close_pending_ = false;
  std::map<int, std::unique_ptr<TabBinding>> bindings_;
  bool app_operation_ = false;
  base::ScopedObservation<views::View, views::ViewObserver> view_observation_{this};
  base::ScopedObservation<aura::Window, aura::WindowObserver> input_observation_{this};
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

class HostMessageHandler : public content::WebUIMessageHandler,
                           public content::WebContentsObserver {
 public:
  explicit HostMessageHandler(base::WeakPtr<ShellHostView> host)
      : content::WebContentsObserver(host ? host->shell() : nullptr),
        host_(std::move(host)) {}
  ~HostMessageHandler() override {
    if (host_) host_->ClearHandler(this);
  }
  void SendEvent(std::string_view module, std::string_view event,
                 const base::Value& payload) {
    if (!IsJavascriptAllowed() || !host_ || !host_->OwnsHandler(this, generation_) || !host_->Trusted(web_ui())) return;
    CallJavascriptFunction("__ardiaEvent", base::Value(module),
                           base::Value(event), payload);
  }

 private:
  struct EngineReply {
    std::string name;
    base::WeakPtr<HostMessageHandler> handler;
    std::string id;
  };
  void RegisterMessages() override {
    web_ui()->RegisterMessageCallback(
        "ardiaRequest", base::BindRepeating(&HostMessageHandler::Request,
                                            base::Unretained(this)));
  }
  void ActivateCommittedRenderer() {
    if (!host_ || !host_->Trusted(web_ui())) return;
    auto weak = weak_factory_.GetWeakPtr();
    const uint64_t generation = host_->SetHandler(this, web_ui());
    if (weak) weak->generation_ = generation;
  }
  void PrimaryPageChanged(content::Page& page) override {
    if (&page.GetMainDocument() == web_ui()->GetRenderFrameHost()) ActivateCommittedRenderer();
  }
  void PrimaryMainFrameRenderProcessGone(base::TerminationStatus) override {
    if (host_) host_->ClearHandler(this);
  }
  void OnJavascriptDisallowed() override {
    weak_factory_.InvalidateWeakPtrs();
    if (host_ && !host_->OwnsHandler(this, generation_)) host_->ClearHandler(this);
  }
  void Reply(std::string id, bool success, base::Value response) {
    if (!host_ || !host_->OwnsHandler(this, generation_) || !host_->Trusted(web_ui()) || !IsJavascriptAllowed()) return;
    CallJavascriptFunction("__ardiaReply", base::Value(id), base::Value(success),
                           response);
  }
  static void Replied(void* context, const char* json) {
    std::unique_ptr<EngineReply> reply(static_cast<EngineReply*>(context));
    if (!reply->handler) return;
    auto value = json ? base::JSONReader::Read(json, base::JSON_PARSE_RFC) : std::nullopt;
    if (!value) return reply->handler->Fail(reply->id, "ERR_BAD_REPLY", "The engine reply is not JSON");
    if (value->is_dict() && value->GetDict().Find("error")) {
      const auto& error = *value->GetDict().Find("error");
      return reply->handler->Fail(reply->id, "ERR_ENGINE", error.is_string() ? error.GetString() : "Engine service failed");
    }
    if (reply->name == "search.engines" && value->is_dict()) {
      auto* engines = value->GetDict().FindList("engines");
      if (!engines) return reply->handler->Fail(reply->id, "ERR_BAD_REPLY", "Missing search engines");
      value = base::Value(base::DictValue().Set("list", base::DictValue().Set("engines", engines->Clone())));
    }
    const auto* route = ardiacore::linux_contract::FindRoute(reply->name);
    if (route && route->field == "null") value = base::Value();
    else if (route && !route->field.empty()) {
      const auto* field = value->is_dict() ? value->GetDict().Find(route->field) : nullptr;
      if (!field) return reply->handler->Fail(reply->id, "ERR_BAD_REPLY", "Missing engine response field");
      value = field->Clone();
    }
    if (reply->name == "engine.tasks" && value->is_list()) {
      for (auto& task : value->GetList()) {
        if (!task.is_dict()) continue;
        base::ListValue translated;
        if (const auto* ids = task.GetDict().FindList("tabIds")) {
          for (const auto& id : *ids) {
            if (!id.is_int()) continue;
            auto* binding = reply->handler->host_ ? reply->handler->host_->Binding(id.GetInt()) : nullptr;
            if (binding && binding->contents()) translated.Append(binding->key());
          }
        }
        task.GetDict().Set("tabIds", std::move(translated));
        task.GetDict().Remove("browserIds");
      }
    }
    reply->handler->Resolve(reply->id, std::move(*value));
  }
  void Resolve(const std::string& id, base::Value value) {
    const auto json = base::WriteJson(value);
    if (!json) return Fail(id, "ERR_BAD_REPLY", "The native reply is not JSON");
    Reply(id, true, base::Value(*json));
  }
  void Fail(const std::string& id, std::string_view code, std::string_view message) {
    Reply(id, false, base::Value(base::DictValue().Set("code", code).Set("message", message)));
  }
  void Request(const base::ListValue& arguments) {
    CHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
    if (!host_ || !host_->OwnsHandler(this, generation_) || !host_->Trusted(web_ui()) || arguments.size() != 3 ||
        !arguments[0].is_string() || !arguments[1].is_string() ||
        !arguments[2].is_dict()) return;
    const std::string id = arguments[0].GetString();
    if (id.empty() || id.size() > 256) return;
    AllowJavascript();
    const auto& args = arguments[2].GetDict();
    if (arguments[1].GetString() == "engine.call") {
      const std::string name = String(args, "name");
      if (!args.FindString("profile")) return Fail(id, "ERR_ENGINE", "Profile must be a string");
      const std::string profile = String(args, "profile");
      if (!DefaultProfileId(profile) || !host_->profile() ||
          host_->profile()->IsOffTheRecord()) {
        return Reply(id, false, base::Value(base::DictValue().Set("code", "ERR_ENGINE").Set("message", "Only the default regular profile is supported")));
      }
      const base::Value* value = args.Find("args");
      if (!value || (!value->is_none() && !value->is_string())) {
        return Reply(id, false, base::Value("Engine arguments must be JSON text or null"));
      }
      std::string json = value->is_string() ? value->GetString() : "{}";
      if (name.size() > 128 || name.find('\0') != std::string::npos ||
          json.size() > kDocumentLimit || json.find('\0') != std::string::npos) {
        return Reply(id, false, base::Value("Invalid or oversized engine request"));
      }
      auto parsed = base::JSONReader::Read(json, base::JSON_PARSE_RFC);
      if (!parsed || !parsed->is_dict() || parsed->GetDict().contains("profile") ||
          parsed->GetDict().contains("offTheRecord") || parsed->GetDict().contains("incognito")) {
        return Fail(id, "ERR_ENGINE", "Engine arguments require a regular-profile JSON object");
      }
      const auto& input = parsed->GetDict();
      if (const auto* engine = input.FindString("engine"); engine && *engine != "chromium") {
        return Fail(id, "ERR_UNKNOWN_CALL", "This page engine is unavailable on Linux");
      }
      if (name == "tabs.devBrowserId" || name == "tabs.devWindowNumber" || name == "tabs.devFocusPage") {
        auto* binding = host_->BindingForKey(String(input, "tabId"));
        if (!binding || !binding->contents()) return Fail(id, "ERR_ENGINE", "The tab is not owned by this window");
        if (name == "tabs.devFocusPage") binding->contents()->Focus();
        base::Value value = name == "tabs.devFocusPage" ? base::Value(true) :
            base::Value(name == "tabs.devBrowserId" ? TabId(binding->contents()) : host_->id());
        return Resolve(id, std::move(value));
      }
      if (name == "engine.info" || name == "tabs.strips" || name == "tabs.stripCommand" ||
          name == "windows.list" || name == "search.setEngineName" || name == "webView.setAutoPictureInPicture") {
        base::ListValue values;
        if (name == "search.setEngineName") values.Append(String(input, "name"));
        if (name == "tabs.stripCommand") {
          const auto* command = input.FindDict("command");
          const auto command_id = input.FindInt("id");
          if (!command || !command_id) return Fail(id, "ERR_ENGINE", "A strip command and id are required");
          values.Append(*command_id).Append(command->Clone());
        }
        if (name == "webView.setAutoPictureInPicture") {
          const auto enabled = input.FindBool("enabled");
          if (!enabled) return Fail(id, "ERR_ENGINE", "A boolean auto-PiP setting is required");
          values.Append(*enabled);
        }
        host_->Invoke(name, base::DictValue().Set("args", std::move(values)),
            base::BindOnce([](base::WeakPtr<HostMessageHandler> handler, std::string id,
                              bool success, base::Value result) {
              if (!handler) return;
              if (!success) return handler->Fail(id, "ERR_ENGINE", result.is_string() ? result.GetString() : "Native call failed");
              handler->Resolve(id, std::move(result));
            }, weak_factory_.GetWeakPtr(), id));
        return;
      }
      const auto* route = ardiacore::linux_contract::FindRoute(name);
      if (!route) return Fail(id, "ERR_UNKNOWN_CALL", "This call is unavailable on Linux");
      const std::string path = host_->profile()->GetPath().value();
      if (name == "extensions.configure") {
        const auto configured = ExtensionConfiguration(input);
        if (!configured) return Fail(id, "ERR_ENGINE", "Unsupported or invalid extension options");
        const auto serialized = base::WriteJson(*configured);
        if (!serialized) return Fail(id, "ERR_BAD_REPLY", "Extension options are not JSON");
        json = *serialized;
      }
      const std::string service(route->service);
      ac_engine_call(service.c_str(), path.c_str(), json.c_str(), Replied,
                     new EngineReply{name, weak_factory_.GetWeakPtr(), id});
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
  uint64_t generation_ = 0;
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
  aura::Window* input_window = GetWidget()->GetNativeWindow()->GetRootWindow();
  input_observation_.Observe(input_window);
  input_handler_ = std::make_unique<WindowKeyboardHandler>(
      base::BindRepeating(&ShellHostView::OnKeyEvent, GetWeakPtr()));
  input_window->AddPreTargetHandler(input_handler_.get(), ui::EventTarget::Priority::kSystem);
  if (is_utility()) {
    ArmUtilityClose();
    browser_view_->SetPreferredSize(kind_ == "settings" ? gfx::Size(720, 480)
                                                       : gfx::Size(640, 600));
    GetWidget()->OnSizeConstraintsChanged();
  }
  LayoutOverlay();
  if (!title_.empty()) {
    browser_->GetWindow()->GetNativeWindow()->SetTitle(base::UTF8ToUTF16(title_));
  }
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
  if (!browser_ || !ui || ui->GetWebContents() != shell()) return false;
  auto* frame = shell()->GetPrimaryMainFrame();
  return frame && frame->IsRenderFrameLive() && ui->GetRenderFrameHost() == frame && frame->GetWebUI() == ui && frame->GetLastCommittedOrigin() ==
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
  base::FilePath user_data_directory;
  base::PathService::Get(chrome::DIR_USER_DATA, &user_data_directory);
  auto bootstrap = base::DictValue()
      .Set("windowId", window_id_)
      .Set("startup", startup_)
      .Set("profileId", "default")
      .Set("nativeModules", base::DictValue().Set(
                                "ArdiaApp", base::DictValue().Set(
                                                "systemInfo", std::move(info))))
      .Set("launchConfig", ardiacore::LinuxLaunchConfig(DocumentDirectory(profile()), user_data_directory))
      .Set("capabilities", base::ListValue().Append("ArdiaWebView.call"));
  if (is_utility()) bootstrap.Set("utility", utility_.Clone());
  return bootstrap;
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
  if (input_observation_.IsObserving()) {
    input_observation_.GetSource()->RemovePreTargetHandler(input_handler_.get());
    input_observation_.Reset();
  }
  input_handler_.reset();
  if (focus_manager_) {
    if (shortcut_recording_) focus_manager_->set_shortcut_handling_suspended(false);
  }
  shortcut_recording_ = false;
  shortcuts_.clear();
  focus_manager_ = nullptr;
}

void ShellHostView::FocusShell() {
  shell_->RequestFocus();
  if (shell()) shell()->Focus();
}

void ShellHostView::OnWindowDestroying(aura::Window*) {
  UnregisterAccelerators();
}

bool ShellHostView::OwnsKeyboardEvent(const ui::KeyEvent& event) const {
  const views::Widget* widget = GetWidget();
  if (!widget) return false;
  for (auto* target = static_cast<aura::Window*>(event.target()); target; target = target->parent()) {
    if (const auto* owner = views::Widget::GetWidgetForNativeView(target)) {
      if (owner != widget) return false;
      break;
    }
  }
  if (wm::GetModalTransient(widget->GetNativeWindow())) return false;
  for (const auto& [id, tab] : bindings_) {
    if (tab->viewport() && tab->viewport()->GetVisible() && tab->contents() &&
        wm::GetModalTransient(tab->contents()->GetNativeView())) return false;
  }
  return true;
}

void ShellHostView::OnKeyEvent(ui::KeyEvent* event) {
  if (!CanHandleAccelerators() || !OwnsKeyboardEvent(*event) || !focus_manager_ || shortcut_recording_ ||
      focus_manager_->shortcut_handling_suspended() ||
      event->type() != ui::EventType::kKeyPressed) return;
  const ui::KeyboardCode physical = ui::DomCodeToUsLayoutKeyboardCode(event->code());
  const ui::Accelerator accelerator(physical == ui::VKEY_UNKNOWN ? event->key_code() : physical,
                                    event->flags());
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("ardia-keyboard-diagnostics") &&
      (event->IsControlDown() || shortcuts_.contains(accelerator))) {
    LOG(INFO) << "Ardia key window=" << window_id_
              << " key=" << event->key_code() << " code=" << static_cast<int>(event->code())
              << " flags=" << event->flags()
              << " view=" << static_cast<bool>(focus_manager_->GetFocusedView())
              << " shell_view=" << shell_->HasFocus()
              << " binding=" << shortcuts_.contains(accelerator);
  }
  if (AcceleratorPressed(accelerator)) event->StopPropagation();
}

bool ShellHostView::CanHandleAccelerators() const {
  return browser_ && (is_utility() || handler_);
}

void ShellHostView::ArmUtilityClose() {
  if (!browser_ || !browser_view_ || !GetWidget() || GetWidget()->IsClosed()) return;
  GetWidget()->MakeCloseSynchronous(base::BindOnce(
      [](base::WeakPtr<ShellHostView> host, base::WeakPtr<views::Widget> widget,
         views::Widget::ClosedReason) {
        if (!host || !widget || widget->IsClosed()) return;
        host->ArmUtilityClose();
        host->RequestUtilityClose();
      }, GetWeakPtr(), GetWidget()->GetWeakPtr()));
}

void ShellHostView::RequestUtilityClose() {
  if (!browser_ || !browser_view_ || !GetWidget() || GetWidget()->IsClosed()) return;
  utility_close_pending_ = true;
  if (!utility_ready_ || !handler_) return;
  Event("ArdiaShell", "onCommand", base::Value(base::DictValue()
      .Set("command", "closeTab").Set("arg", base::Value()).Set("windowId", window_id_)));
}

void ShellHostView::CloseUtilityAfterSave() {
  if (!browser_ || !browser_view_ || !GetWidget() || GetWidget()->IsClosed()) return;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
          [](base::WeakPtr<ShellHostView> host, base::WeakPtr<views::Widget> widget) {
            if (!host || !widget || widget->IsClosed() ||
                !host->browser_ || !host->browser_view_) return;
            host->utility_close_pending_ = false;
            widget->MakeCloseSynchronous({});
            host->browser_->GetWindow()->Close();
            if (host && widget && !widget->IsClosed() &&
                host->browser_ && host->browser_view_) host->ArmUtilityClose();
          }, GetWeakPtr(), GetWidget()->GetWeakPtr()));
}

bool ShellHostView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  const auto found = shortcuts_.find(accelerator);
  if (found == shortcuts_.end()) return false;
  const base::DictValue& binding = found->second;
  const std::string command = String(binding, "command");
  if (switcher_capture_) {
    std::string key;
    if (accelerator.key_code() == ui::VKEY_ESCAPE) key = "escape";
    if (accelerator.key_code() == ui::VKEY_RIGHT) key = "next";
    if (accelerator.key_code() == ui::VKEY_LEFT) key = "previous";
    if (!key.empty()) {
      Event("ArdiaShell", "onAppEvent", base::Value(base::DictValue()
          .Set("type", "switcherKey").Set("key", key)));
      return true;
    }
    if (command != "tabSwitcher") return true;
  }
  if (binding.FindBool("suppressed").value_or(false)) return true;
  if (command == "tabSwitcher" && !switcher_capture_) {
    switcher_source_.reset();
    for (const auto& [id, tab] : bindings_) {
      if (tab->viewport() && tab->viewport()->HasFocus() && tab->contents()) {
        switcher_source_ = tab->contents()->GetWeakPtr();
        break;
      }
    }
  }
  if (binding.FindBool("notInText").value_or(false)) {
    ui::InputMethod* input = GetWidget()->GetInputMethod();
    ui::TextInputClient* client = input ? input->GetTextInputClient() : nullptr;
    if (client && client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE) return false;
  }
  if (is_utility() && command == "closeTab") {
    RequestUtilityClose();
    return true;
  }
  if (command == "focusCommandBar" || command == "newTab" ||
      command == "newTabInGroup" || command == "findInPage" ||
      command == "findAndReplace" || command == "searchTabs" ||
      command == "tabSwitcher" || command == "openProfileMenu" ||
      command == "closeTab") {
    FocusShell();
  }
  const base::Value* arg = binding.Find("arg");
  Event("ArdiaShell", "onCommand", base::Value(base::DictValue()
      .Set("command", command).Set("arg", arg ? arg->Clone() : base::Value())
      .Set("shortcutModifiers", accelerator.modifiers()).Set("windowId", window_id_)));
  return true;
}

TabBinding* ShellHostView::Binding(int id) {
  const auto found = bindings_.find(id);
  return found == bindings_.end() ? nullptr : found->second.get();
}

TabBinding* ShellHostView::ClaimedBinding(const base::DictValue& args) {
  auto* binding = Binding(args.FindInt("id").value_or(0));
  return handler_ && binding && binding->contents() && binding->renderer_generation == renderer_generation_ && ardiacore::linux_contract::OwnsClaim(binding->claim, String(args, "claim")) ? binding : nullptr;
}

bool ShellHostView::CloseTab(TabBinding* binding) {
  if (!browser_ || !binding || !binding->contents()) return false;
  const int index = browser_->GetTabStripModel()->GetIndexOfWebContents(binding->contents());
  if (index < 0) return false;
  app_operation_ = true;
  browser_->GetTabStripModel()->CloseWebContentsAt(
      index, TabCloseTypes::CLOSE_USER_GESTURE | TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  app_operation_ = false;
  QueueTransaction(-1);
  return true;
}

bool ShellHostView::DisposeTemporary(TabBinding* binding) {
  if (!binding || !binding->temporary || !binding->contents() || !browser_) return false;
  auto* model = browser_->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(binding->contents());
  if (index < 0) return false;
  auto weak = GetWeakPtr();
  app_operation_ = true;
  model->DetachAndDeleteWebContentsAt(index);
  if (weak) {
    weak->app_operation_ = false;
    weak->QueueTransaction(-1);
  }
  return true;
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
  for (const auto& [id, host] : Hosts()) {
    if (!host->is_utility()) strips.Append(host->Strip());
  }
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
              target->Event("ArdiaTabs", "tabs", base::Value(base::DictValue()
                  .Set("name", "tabs.strips").Set("data", transaction.Clone())));
            }
          },
          GetWeakPtr(), std::move(transaction)));
}

void ShellHostView::EngineEvent(std::string_view topic,
                                const base::DictValue& payload) {
  if (!profile()) return;
  base::DictValue translated = payload.Clone();
  if (translated.FindBool("offTheRecord").value_or(false)) return;
  if (!translated.FindString("profile")) return;
  if (const std::string* path = translated.FindString("profile")) {
    if (*path != profile()->GetPath().value() ||
        translated.FindBool("offTheRecord").value_or(false)) return;
    translated.Set("profile", "");
  }
  if (topic == "downloads.changed") {
    const auto* download = translated.FindDict("download");
    if (!download || download->FindBool("offTheRecord").value_or(false)) return;
    translated = download->Clone();
    translated.Set("profile", "");
  }
  const auto name = ardiacore::linux_contract::EventName(topic);
  if (name.empty()) return;
  for (const auto field : {"tabId", "browserId"}) {
    if (const auto* key = translated.FindString(field)) {
      if (!BindingForKey(*key)) return;
    }
    if (const auto id = translated.FindInt(field)) {
      auto* binding = Binding(*id);
      if (!binding || !binding->contents()) return;
      translated.Set("tabId", binding->key());
      if (std::string_view(field) != "tabId") translated.Remove(field);
    }
  }
  const std::string domain(name.substr(0, name.find('.')));
  std::string module = "Ardia" + domain;
  module[5] = base::ToUpperASCII(module[5]);
  Event(module, domain, base::Value(base::DictValue().Set("name", name).Set("data", std::move(translated))));
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

void ShellHostView::ClaimLaunchTab(
    const base::ListValue& values,
    base::OnceCallback<void(bool, base::Value)> done) {
  if (values.size() != 3 || !values[0].is_string() || !values[1].is_string() || !values[2].is_string()) {
    return std::move(done).Run(false, base::Value("A launch key, URL and profile are required"));
  }
  const auto tab = launch_decision_.Consume(values[0].GetString(), values[1].GetString(), values[2].GetString());
  if (!tab || !DefaultProfileId(values[2].GetString())) return std::move(done).Run(false, base::Value("No matching unconsumed native launch offer"));
  if (*tab == 0) return std::move(done).Run(true, base::Value());
  const bool adopted = AdoptLaunchOffer(*tab, values[0].GetString(), values[1].GetString());
  std::move(done).Run(adopted, adopted ? base::Value() : base::Value("The native launch offer is no longer available"));
}

bool ShellHostView::AdoptLaunchOffer(int tab, std::string_view key, std::string_view url) {
  if (!browser_ || BindingForKey(key) || Binding(tab)) return false;
  auto* model = browser_->GetTabStripModel();
  for (int index = 0; index < model->count(); ++index) {
    auto* contents = model->GetWebContentsAt(index);
    if (TabId(contents) != tab) continue;
    if (contents->GetBrowserContext() != profile() || contents->GetVisibleURL() != GURL(url)) return false;
    bindings_[tab] = std::make_unique<TabBinding>(this, contents, std::string(key));
    bindings_[tab]->renderer_generation = renderer_generation_;
    QueueTransaction(-1);
    return true;
  }
  return false;
}

void ShellHostView::CreateTab(
    const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  const std::string key = String(args, "key");
  const std::string claim = String(args, "claim");
  if (claim.empty() || claim.size() > 256) return std::move(done).Run(false, base::Value("A bounded tab claim is required"));
  if (!browser_ || key.empty() || key.size() > 256 ||
      !args.FindString("profile") || !DefaultProfileId(String(args, "profile"))) {
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
  if (binding && !ardiacore::linux_contract::CanReclaim(binding->claim, String(args, "previousClaim"))) {
    return std::move(done).Run(false, base::Value("This tab already has an active owner"));
  }
  if (!binding) {
    for (const auto& [window, source] : Hosts()) {
      if (source == this) continue;
      TabBinding* existing = source->BindingForKey(key);
      if (!existing || !existing->contents()) continue;
      if (!ardiacore::linux_contract::CanTransfer(existing->transferable, existing->temporary, source->profile() == profile())) return std::move(done).Run(false, base::Value("This tab has not been offered for transfer"));
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
      bool offered = false;
      for (auto& [window, source] : Hosts()) {
        const auto offer = source->offered_.find(wanted);
        if (source->profile() == profile() && offer != source->offered_.end() && offer->second) offered = true;
      }
      if (!offered) return std::move(done).Run(false, base::Value("This tab has not been offered for adoption"));
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
    for (auto& [window, source] : Hosts()) source->offered_.erase(id);
    binding = bindings_[id].get();
    app_operation_ = false;
  }
  binding->temporary = args.FindBool("temporary").value_or(false);
  binding->claim = claim;
  binding->renderer_generation = renderer_generation_;
  binding->transferable = false;
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
  TabBinding* binding = ClaimedBinding(args);
  if (!browser_ || !binding) {
    return std::move(done).Run(false, base::Value("No such live tab"));
  }
  content::WebContents* contents = binding->contents();
  auto& controller = contents->GetController();
  const std::string command = String(args, "command");
  const base::ListValue& values = Arguments(args);
  if (command == "captureViewport") {
    auto* view = contents->GetRenderWidgetHostView();
    if (!view || !view->IsSurfaceAvailableForCopy()) {
      return std::move(done).Run(true, base::Value());
    }
    view->CopyFromSurface(
        gfx::Rect(), gfx::Size(), base::Milliseconds(250),
        base::BindOnce([](base::OnceCallback<void(bool, base::Value)> reply,
                          const content::CopyFromSurfaceResult& result) {
          const auto png = result.has_value()
              ? gfx::PNGCodec::EncodeBGRASkBitmap(result->bitmap, false)
              : std::nullopt;
          std::move(reply).Run(true, png
              ? base::Value("data:image/png;base64," + base::Base64Encode(*png))
              : base::Value());
        }, std::move(done)));
    return;
  } else if (command == "loadUrl") {
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
    if (binding->viewport()) binding->viewport()->RequestFocus();
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
    if (!CloseTab(binding)) return std::move(done).Run(false, base::Value("Tab is no longer in this window"));
  } else {
    return std::move(done).Run(false, base::Value("Unsupported tab command: " + command));
  }
  std::move(done).Run(true, base::Value());
}

void ShellHostView::SetViewport(
    const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  TabBinding* binding = ClaimedBinding(args);
  const base::DictValue* rect = args.FindDict("rect");
  if (!binding || !rect) {
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
  std::vector<gfx::Rect> shape{gfx::Rect(bounds.size())};
  std::vector<gfx::Rect> input_shape = shape;
  const auto subtract = [](std::vector<gfx::Rect>& region, const gfx::Rect& cut) {
    std::vector<gfx::Rect> remaining;
    for (const auto& piece : region) {
      gfx::Rect overlap = piece;
      overlap.Intersect(cut);
      if (overlap.IsEmpty()) {
        remaining.push_back(piece);
        continue;
      }
      for (const auto& part : {
               gfx::Rect(piece.x(), piece.y(), piece.width(), overlap.y() - piece.y()),
               gfx::Rect(piece.x(), overlap.bottom(), piece.width(), piece.bottom() - overlap.bottom()),
               gfx::Rect(piece.x(), overlap.y(), overlap.x() - piece.x(), overlap.height()),
               gfx::Rect(overlap.right(), overlap.y(), piece.right() - overlap.right(), overlap.height())}) {
        if (!part.IsEmpty()) remaining.push_back(part);
      }
    }
    region = std::move(remaining);
  };
  const base::ListValue* occlusions = args.FindList("occlusions");
  if (occlusions && occlusions->size() > 16) {
    return std::move(done).Run(false, base::Value("Too many viewport occlusions"));
  }
  if (occlusions) {
    for (const auto& entry : *occlusions) {
      const auto* occlusion = entry.GetIfDict();
      if (!occlusion) return std::move(done).Run(false, base::Value("Invalid viewport occlusion"));
      const double ox = occlusion->FindDouble("x").value_or(NAN);
      const double oy = occlusion->FindDouble("y").value_or(NAN);
      const double ow = occlusion->FindDouble("width").value_or(NAN);
      const double oh = occlusion->FindDouble("height").value_or(NAN);
      const double outset = occlusion->FindDouble("paintOutset").value_or(0);
      if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(ow) ||
          !std::isfinite(oh) || !std::isfinite(outset) || ow < 0 || oh < 0 || outset < 0 || outset > 64) {
        return std::move(done).Run(false, base::Value("Invalid viewport occlusion"));
      }
      const double ol = std::clamp(ox * scale, left, right);
      const double ot = std::clamp(oy * scale, top, bottom);
      const double oright = std::clamp((ox + ow) * scale, ol, right);
      const double obottom = std::clamp((oy + oh) * scale, ot, bottom);
      gfx::Rect cut = gfx::ToEnclosingRect(gfx::RectF(ol, ot, oright - ol, obottom - ot));
      cut.Offset(-bounds.x(), -bounds.y());
      if (cut.IsEmpty()) continue;
      subtract(input_shape, cut);
      cut.Outset(static_cast<int>(std::ceil(outset * scale)));
      subtract(shape, cut);
    }
  }
  if (!binding->viewport()) {
    binding->SetViewport(AddChildView(std::make_unique<ViewportView>(profile())));
  }
  views::WebView* view = binding->viewport();
  const bool visible = args.FindBool("visible").value_or(false) && !bounds.IsEmpty();
  view->SetBoundsRect(bounds);
  static_cast<ViewportView*>(view)->SetShape(std::move(shape), std::move(input_shape));
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

void ShellHostView::SetPresentations(
    const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  const base::ListValue* tabs = args.FindList("tabs");
  if (!tabs || tabs->size() > 1024) {
    return std::move(done).Run(false, base::Value("A tab presentation list is required"));
  }
  for (const auto& tab : *tabs) {
    const auto* item = tab.GetIfDict();
    TabBinding* binding = item ? ClaimedBinding(*item) : nullptr;
    if (!binding || !item->FindBool("visible")) {
      return std::move(done).Run(false, base::Value("A claimed tab and boolean visibility are required"));
    }
  }
  for (const auto& tab : *tabs) {
    const auto& item = tab.GetDict();
    auto* binding = Binding(*item.FindInt("id"));
    if (*item.FindBool("visible") && !binding->presented) binding->RememberPendingAutoPictureInPicture();
    binding->presentation_attached = true;
    binding->presented = *item.FindBool("visible");
  }
  std::vector<std::pair<base::WeakPtr<content::WebContents>, bool>> changes;
  for (const bool visible : {true, false}) {
    for (const auto& [id, binding] : bindings_) {
      if (!binding->contents() || binding->presented != visible ||
          binding->presentation_applied == visible) continue;
      binding->presentation_applied = visible;
      binding->presentation_departed = !visible;
      if (!visible) binding->cancelled_auto_pip_until = {};
      changes.emplace_back(binding->contents()->GetWeakPtr(), visible);
    }
  }
  for (const auto& [contents, visible] : changes) {
    if (!contents) continue;
    if (auto* helper = AutoPictureInPictureTabHelper::FromWebContents(contents.get())) {
      helper->OnTabActivatedChanged(visible);
    }
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
              target->Event("ArdiaTabs", "tabs", base::Value(base::DictValue().Set("name", "tabs.strips").Set("data", transaction.Clone())));
            }
          }, GetWeakPtr(), std::move(transaction)));
}

void ShellHostView::Invoke(
    std::string method, const base::DictValue& args,
    base::OnceCallback<void(bool, base::Value)> done) {
  if (!browser_) return std::move(done).Run(false, base::Value("The native browser window has closed"));
  const base::ListValue& values = Arguments(args);
  if (method == "ArdiaShell.restoreShortcutFocus") {
    if (switcher_source_ && browser_->GetTabStripModel()->GetActiveWebContents() == switcher_source_.get()) {
      switcher_source_->Focus();
    }
    switcher_source_.reset();
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaShell.setKeyboardShortcuts") {
    if (!focus_manager_ || values.empty() || !values[0].is_list() ||
        values[0].GetList().size() > 256) {
      return std::move(done).Run(false, base::Value("Invalid keyboard shortcut table"));
    }
    std::map<ui::Accelerator, base::DictValue> shortcuts;
    constexpr int modifiers = ui::EF_SHIFT_DOWN | ui::EF_CONTROL_DOWN |
                              ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN;
    for (const auto& value : values[0].GetList()) {
      if (!value.is_dict()) return std::move(done).Run(false, base::Value("Invalid keyboard shortcut"));
      const auto& binding = value.GetDict();
      const int key = binding.FindInt("keyCode").value_or(0);
      const int flags = binding.FindInt("modifiers").value_or(-1);
      const std::string command = String(binding, "command");
      if (key < 1 || key > 255 || flags < 0 || (flags & ~modifiers) ||
          command.empty() || command.size() > 64) {
        return std::move(done).Run(false, base::Value("Invalid keyboard shortcut"));
      }
      shortcuts.emplace(ui::Accelerator(static_cast<ui::KeyboardCode>(key), flags), binding.Clone());
    }
    shortcuts_ = std::move(shortcuts);
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaShell.setShortcutRecording" || method == "ArdiaShell.setSwitcherCapture") {
    if (!focus_manager_ || values.empty() || !values[0].is_bool()) {
      return std::move(done).Run(false, base::Value("A keyboard capture flag is required"));
    }
    if (method == "ArdiaShell.setShortcutRecording") {
      shortcut_recording_ = values[0].GetBool();
      focus_manager_->set_shortcut_handling_suspended(shortcut_recording_);
    } else {
      switcher_capture_ = values[0].GetBool();
      if (!switcher_capture_) {
        if (switcher_source_ && browser_->GetTabStripModel()->GetActiveWebContents() == switcher_source_.get()) {
          switcher_source_->Focus();
        }
        switcher_source_.reset();
      }
    }
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaTabs.claimLaunchTab") return ClaimLaunchTab(values, std::move(done));
  if (method == "ArdiaTabs.prepareTransfer" || method == "ArdiaTabs.releaseTransfer") {
    auto* binding = BindingForKey(ArgumentString(values, 0));
    if (!binding) return std::move(done).Run(false, base::Value("No owned tab to transfer"));
    binding->transferable = method == "ArdiaTabs.prepareTransfer";
    return std::move(done).Run(true, base::Value());
  }
  if (method == "tab.create") return CreateTab(args, std::move(done));
  if (method == "tab.command") return TabCommand(args, std::move(done));
  if (method == "tab.viewport") return SetViewport(args, std::move(done));
  if (method == "tab.presentations") return SetPresentations(args, std::move(done));
  if (method == "webView.setAutoPictureInPicture") {
    if (values.size() != 1 || !values[0].is_bool()) {
      return std::move(done).Run(false, base::Value("A boolean auto-PiP setting is required"));
    }
    const bool enabled = values[0].GetBool();
    if (!enabled) {
      for (const auto& [window, host] : Hosts()) {
        for (const auto& [id, binding] : host->bindings_) {
          binding->RememberPendingAutoPictureInPicture();
        }
      }
    }
    g_ardia_auto_picture_in_picture_off = !enabled;
    HostContentSettingsMapFactory::GetForProfile(profile())->SetDefaultContentSetting(
        ContentSettingsType::AUTO_PICTURE_IN_PICTURE,
        enabled ? CONTENT_SETTING_ALLOW : CONTENT_SETTING_BLOCK);
    if (!enabled) {
      std::vector<base::WeakPtr<content::WebContents>> contents;
      for (const auto& [window, host] : Hosts()) {
        for (const auto& [id, binding] : host->bindings_) {
          if (binding->contents()) contents.push_back(binding->contents()->GetWeakPtr());
        }
      }
      for (const auto& tab : contents) {
        if (!tab) continue;
        if (auto* helper = AutoPictureInPictureTabHelper::FromWebContents(tab.get())) {
          helper->OnTabBecameActive();
        }
      }
    }
    return std::move(done).Run(true, base::Value());
  }
  if (method == "tab.close") {
    TabBinding* binding = BindingForKey(String(args, "key"));
    if (!binding || !binding->contents()) {
      return std::move(done).Run(true, base::Value());
    }
    const bool closed = CloseTab(binding);
    return std::move(done).Run(closed, closed ? base::Value() : base::Value("Tab is no longer in this window"));
  }
  if (method == "tab.release") {
    TabBinding* binding = ClaimedBinding(args);
    if (!binding) return std::move(done).Run(false, base::Value("No owned live tab"));
    if (binding->temporary) {
      const bool disposed = DisposeTemporary(binding);
      return std::move(done).Run(disposed, disposed ? base::Value() : base::Value("The temporary surface is no longer owned by this window"));
    }
    binding->claim.clear();
    binding->RememberPendingAutoPictureInPicture();
    binding->presented = false;
    binding->presentation_applied = false;
    binding->presentation_attached = false;
    binding->presentation_departed = false;
    if (binding->contents()) {
      if (auto* helper = AutoPictureInPictureTabHelper::FromWebContents(binding->contents())) {
        helper->OnTabBecameActive();
      }
    }
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
               base::DictValue source_session,
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
                  base::DictValue update = event.Clone();
                  if (host->window_id() == source_window) {
                    update.Set("base", source_session.Clone());
                  }
                  host->Event("ArdiaHost", "session", base::Value(std::move(update)));
                }
              }
            }, profile_path, source_window, local->Clone(), std::move(done)));
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

  if (method == "tabs.strips") {
    return std::move(done).Run(true, base::Value(Transaction(std::nullopt, false)));
  }
  if (method == "tabs.stripCommand") {
    if (values.size() < 2 || !values[0].is_int() || !values[1].is_dict()) {
      return std::move(done).Run(false, base::Value("A command id and tab strip command are required"));
    }
    ShellHostView* target = this;
    const int strip = values[1].GetDict().FindInt("strip").value_or(0);
    const auto found = Hosts().find(strip);
    if (found == Hosts().end() || found->second != this) return std::move(done).Run(false, base::Value("The strip is not owned by this window"));
    return target->StripCommand(values[0].GetInt(), values[1].GetDict(), std::move(done));
  }
  if (method == "engine.info") {
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
  if (method == "windows.list") {
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
      method == "ArdiaShell.minimizeWindow" ||
      method == "ArdiaShell.toggleMaximizeWindow" ||
      method == "ArdiaShell.focusWindow" || method == "ArdiaShell.utilityReady" ||
      method == "ArdiaShell.setWindowTitle") {
    ShellHostView* target = nullptr;
    for (const auto& [id, host] : Hosts()) {
      if (host->window_id() == ArgumentString(values, 0)) target = host;
    }
    if (!target) return std::move(done).Run(false, base::Value("No such native window"));
    if (method == "ArdiaShell.utilityReady") {
      if (target != this || !is_utility()) {
        return std::move(done).Run(false, base::Value("No such utility renderer"));
      }
      utility_ready_ = true;
      if (utility_close_pending_) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE, base::BindOnce(&ShellHostView::RequestUtilityClose, GetWeakPtr()));
      }
      return std::move(done).Run(true, base::Value(utility_.Clone()));
    }
    if (method == "ArdiaShell.windowFrame") {
      const gfx::Rect bounds = target->browser()->GetWindow()->GetBounds();
      return std::move(done).Run(true, base::Value(base::ListValue()
          .Append(bounds.x()).Append(bounds.y()).Append(bounds.width()).Append(bounds.height())));
    }
    if (method == "ArdiaShell.setWindowTitle") {
      target->browser()->GetWindow()->GetNativeWindow()->SetTitle(
          base::UTF8ToUTF16(ArgumentString(values, 1)));
    } else if (method == "ArdiaShell.minimizeWindow") {
      target->browser()->GetWindow()->Minimize();
    } else if (method == "ArdiaShell.toggleMaximizeWindow") {
      if (target->kind_ == "import") {
        return std::move(done).Run(false, base::Value("The Import window has a fixed size"));
      }
      ui::BaseWindow* window = target->browser()->GetWindow();
      if (window->IsMaximized()) {
        window->Restore();
      } else {
        window->Maximize();
      }
    } else if (method == "ArdiaShell.focusWindow") {
      FocusNativeWindow(target->browser());
    } else if (target->is_utility()) {
      if (target == this) target->CloseUtilityAfterSave();
      else target->RequestUtilityClose();
    } else {
      CloseNativeWindow(target->browser());
    }
    return std::move(done).Run(true, base::Value());
  }
  if (method == "ArdiaShell.openWindow") {
    const std::string id = ArgumentString(values, 0);
    const base::DictValue* options = values.size() > 1 && values[1].is_dict()
                                       ? &values[1].GetDict() : nullptr;
    const std::string kind = options ? String(*options, "kind") : "";
    const bool utility = kind == "settings" || kind == "import";
    const base::DictValue* navigation = options ? options->FindDict("settings") : nullptr;
    std::optional<base::DictValue> utility_navigation;
    if (navigation) utility_navigation = navigation->Clone();
    if (utility && options && options->FindString("importSource")) {
      if (!utility_navigation) utility_navigation.emplace();
      utility_navigation->Set("importSource", String(*options, "importSource"));
    }
    if (id.empty() || id.size() > 256 ||
        (utility && id != kind) ||
        (!utility && (id == "settings" || id == "import")) ||
        (options && (options->FindBool("incognito").value_or(false) ||
                     !DefaultProfileId(String(*options, "profile"))))) {
      return std::move(done).Run(false, base::Value("Only regular default-profile windows are supported"));
    }
    std::optional<gfx::Rect> bounds;
    const base::ListValue* frame = options ? options->FindList("frame") : nullptr;
    if (!utility && frame && frame->size() == 4 &&
        std::ranges::all_of(*frame, [](const base::Value& value) { return value.is_int(); }) &&
        (*frame)[2].GetInt() > 0 && (*frame)[3].GetInt() > 0) {
      bounds = gfx::Rect((*frame)[0].GetInt(), (*frame)[1].GetInt(),
                         (*frame)[2].GetInt(), (*frame)[3].GetInt());
    }
    for (const auto& [native_id, host] : Hosts()) {
      if (host->window_id() != id) continue;
      if (bounds) host->browser()->GetWindow()->SetBounds(*bounds);
      if (utility_navigation && host->is_utility()) host->NavigateUtility(utility_navigation->Clone());
      if (utility) FocusNativeWindow(host->browser());
      return std::move(done).Run(true, base::Value());
    }
    for (auto& [native_id, request] : RequestedWindows()) {
      if (request.id != id || !request.browser) continue;
      if (utility_navigation) request.utility = utility_navigation->Clone();
      if (utility) FocusNativeWindow(request.browser.get());
      return std::move(done).Run(true, base::Value());
    }
    BrowserWindowCreateParams params(profile(), true);
    if (bounds) params.initial_bounds = *bounds;
    if (utility) {
      params.type = BrowserWindowInterface::TYPE_APP;
      params.app_name = kind == "settings" ? "ArdiaSettings" : "ArdiaImport";
      params.is_trusted_source = true;
      params.omit_from_session_restore = true;
      params.should_trigger_session_restore = false;
      params.user_title = options ? String(*options, "title") : "";
      params.can_resize = kind == "settings";
      params.can_maximize = kind == "settings";
      params.can_fullscreen = kind == "settings";
      const base::DictValue* placement = chrome::GetWindowPlacementDictionaryReadOnly(
          params.app_name, profile()->GetPrefs());
      const bool saved = ValidUtilityPlacement(placement);
      if (kind == "import" || !saved) {
        params.initial_bounds = gfx::Rect(kind == "settings" ? 820 : 640,
                                          kind == "settings" ? 640 : 600);
        params.initial_show_state = ui::mojom::WindowShowState::kNormal;
      }
    }
    BrowserWindowInterface* created = CreateBrowserWindow(std::move(params));
    if (!created) return std::move(done).Run(false, base::Value("Chrome could not create a window"));
    RequestedWindow request;
    request.id = id;
    request.kind = utility ? kind : "";
    request.title = options ? String(*options, "title") : "";
    if (utility_navigation) request.utility = utility_navigation->Clone();
    request.browser = created->GetWeakPtr();
    RequestedWindows()[created->GetSessionID().id()] = std::move(request);
    if (utility) FocusNativeWindow(created);
    else created->GetWindow()->ShowInactive();
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
  if (method == "ArdiaShell.setMenuState" || method == "ArdiaShell.setMenuBookmarks") {
    return std::move(done).Run(true, base::Value());
  }
  if (method == "search.setEngineName") {
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
  cancelled_auto_pip_until = {};
  requested_auto_pip_until = {};
  Observe(contents);
  contents_ = contents;
  if (viewport_ && viewport_->GetVisible()) viewport_->SetWebContents(contents);
}

void TabBinding::MoveTo(ShellHostView* owner) {
  RememberPendingAutoPictureInPicture();
  presented = false;
  presentation_applied = false;
  presentation_attached = false;
  presentation_departed = false;
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
  owner_->OfferTab(opened);
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

std::map<int, RequestedWindow>& RequestedWindows() {
  static base::NoDestructor<std::map<int, RequestedWindow>> ids;
  return *ids;
}

class HostRegistry : public BrowserCollectionObserver,
                     public PictureInPictureWindowManager::Observer {
 public:
  void Start() {
    pip_observation_.Observe(PictureInPictureWindowManager::GetInstance());
    observation_.Observe(GlobalBrowserCollection::GetInstance());
    GlobalBrowserCollection::GetInstance()->ForEach(
        [this](BrowserWindowInterface* browser) {
          Attach(browser->GetWeakPtr(), true);
          return true;
        });
  }

 private:
  void ReportPictureInPicture(bool active) {
    if (!pip_opener_) return;
    for (const auto& [id, host] : Hosts()) {
      if (host->TabShown(pip_opener_.get()) < 0) continue;
      host->ReportTab(TabId(pip_opener_.get()), base::DictValue()
          .Set("type", "pictureInPicture").Set("value", base::DictValue()
              .Set("active", active).Set("kind", pip_kind_)));
    }
  }
  void OnEnterPictureInPicture() override {
    ++pip_generation_;
    auto* manager = PictureInPictureWindowManager::GetInstance();
    auto* opener = manager->GetWebContents();
    if (pip_opener_ && pip_opener_.get() != opener) ReportPictureInPicture(false);
    pip_opener_ = opener ? opener->GetWeakPtr() : nullptr;
    pip_kind_ = manager->GetChildWebContents() ? "document" : "video";
    ReportPictureInPicture(true);
    auto* helper = opener ? AutoPictureInPictureTabHelper::FromWebContents(opener) : nullptr;
    bool cancelled = false;
    for (const auto& [id, host] : Hosts()) {
      cancelled |= host->AutoPictureInPictureCancelled(opener);
    }
    if (!cancelled) {
      for (const auto& [id, host] : Hosts()) host->ConsumeAutoPictureInPictureCancellation(opener);
    }
    if (cancelled || (helper && (helper->IsInAutoPictureInPicture() ||
                                helper->AreAutoPictureInPicturePreconditionsMet()))) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(&HostRegistry::CloseCancelledPictureInPicture,
                                    base::Unretained(this), pip_generation_, pip_opener_));
    }
  }
  void OnExitPictureInPicture() override {
    ++pip_generation_;
    ReportPictureInPicture(false);
    if (pip_opener_) {
      for (const auto& [id, host] : Hosts()) {
        host->ConsumeAutoPictureInPictureCancellation(pip_opener_.get());
      }
    }
    pip_opener_ = nullptr;
  }
  void CloseCancelledPictureInPicture(
      uint64_t generation, base::WeakPtr<content::WebContents> contents) {
    auto* manager = PictureInPictureWindowManager::GetInstance();
    if (generation != pip_generation_ || !contents ||
        manager->GetWebContents() != contents.get()) return;
    bool released = true;
    bool cancelled = false;
    for (const auto& [id, host] : Hosts()) {
      const int shown = host->TabShown(contents.get());
      if (shown >= 0 && shown < 2) released = false;
      cancelled |= host->AutoPictureInPictureCancelled(contents.get());
    }
    if (released || cancelled || g_ardia_auto_picture_in_picture_off) manager->ExitPictureInPicture();
  }

  void Attach(base::WeakPtr<BrowserWindowInterface> browser, bool startup) {
    if (!browser || browser->GetProfile()->IsOffTheRecord() ||
        Hosts().contains(browser->GetSessionID().id())) return;
    const auto requested = RequestedWindows().find(browser->GetSessionID().id());
    const bool utility = requested != RequestedWindows().end() &&
                         !requested->second.kind.empty();
    if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL &&
        !(utility && browser->GetType() == BrowserWindowInterface::TYPE_APP)) return;
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
      shell->ConfigureWindow(std::move(found->second));
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
    RequestedWindows().erase(browser->GetSessionID().id());
    const auto found = Hosts().find(browser->GetSessionID().id());
    if (found == Hosts().end()) return;
    if (found->second->is_utility()) {
      found->second->DetachBrowser();
      return;
    }
    base::DictValue strip = found->second->Strip();
    strip.Set("closed", true);
    strip.Set("tabs", base::ListValue());
    found->second->DetachBrowser();
    base::DictValue tx = base::DictValue()
        .Set("rev", ++Revision()).Set("cmd", base::Value())
        .Set("strips", base::ListValue().Append(std::move(strip)));
    for (const auto& [id, host] : Hosts()) {
      host->Event("ArdiaTabs", "tabs", base::Value(base::DictValue().Set("name", "tabs.strips").Set("data", tx.Clone())));
    }
  }

  base::FilePath profile_path_;
  base::WeakPtr<content::WebContents> pip_opener_;
  std::string pip_kind_;
  uint64_t pip_generation_ = 0;
  base::ScopedObservation<PictureInPictureWindowManager,
                          PictureInPictureWindowManager::Observer> pip_observation_{this};
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
  ardiacore::InitializeLinuxLaunchConfig();
  ardiacore::DecideTestGateAtStartup();
  g_ardia_auto_picture_in_picture_off = true;
  g_ardia_linux_tab_shown = [](content::WebContents* contents) {
    for (const auto& [id, host] : Hosts()) {
      const int shown = host->TabShown(contents);
      if (shown >= 0) return shown;
    }
    return -1;
  };
  g_ardia_linux_auto_pip_requested = [](content::WebContents* contents) {
    for (const auto& [id, host] : Hosts()) host->NoteAutoPictureInPictureRequested(contents);
  };
  content::WebUIConfigMap::GetInstance().AddWebUIConfig(std::make_unique<ShellConfig>());
  g_ardia_keeps_window_without_tabs = [](const BrowserWindowInterface* browser) {
    return browser && Hosts().contains(browser->GetSessionID().id());
  };
  ac_engine_set_event_sink(EngineEvent, nullptr);
  static base::NoDestructor<HostRegistry> registry;
  registry->Start();
}

}
