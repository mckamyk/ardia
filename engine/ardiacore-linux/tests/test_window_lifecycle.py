import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src'
LINUX = SOURCE / 'ardia' / 'linux'
HOST = (LINUX / 'ac_linux_host.cc').read_text()

LEDGER = r'''#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "ardia/linux/ac_linux_window_lifecycle.h"

using namespace ardia::linux_window;

static void NewRegularWindowsHonorRequestedFocus() {
  FocusLedger ledger(true);
  assert(ledger.Create(7, {.focus = true}) == Presentation::kActivate);
  assert(ledger.pending(7));
  assert(ledger.Create(8, {.focus = false}) == Presentation::kInactive);
  assert(!ledger.pending(8));
  assert(ledger.Create(9, {.focus = true, .behind = true}) == Presentation::kInactive);
  assert(ledger.Create(10, {.focus = false, .utility = true}) == Presentation::kActivate);
}

static void BackgroundInstancesNeverActivate() {
  FocusLedger ledger(false);
  assert(ledger.Create(1, {.focus = true}) == Presentation::kInactive);
  assert(ledger.Create(2, {.utility = true}) == Presentation::kInactive);
  assert(ledger.Reuse(1, {.focus = true}, false) == Activation::kNone);
  assert(ledger.Reuse(2, {.utility = true}, false) == Activation::kShowInactive);
  assert(ledger.Focus(1) == Activation::kShowInactive);
  assert(ledger.Ready(1) == Activation::kNone);
  assert(!ledger.pending(1));
}

static void APointerPressBlocksTheReadinessRetry() {
  FocusLedger ledger(true);
  ledger.Observe(1, true);
  ledger.Create(2, {.focus = true});
  ledger.PointerPressed();
  assert(ledger.Ready(2) == Activation::kNone);
  ledger.Create(3, {.focus = true});
  assert(ledger.Ready(3) == Activation::kActivate);
}

static void ReuseFollowsIntentButIgnoresTheWindowsOwnLaunchEcho() {
  FocusLedger ledger(true);
  ledger.Create(1, {.focus = false});
  assert(ledger.Reuse(1, {.focus = true}, true) == Activation::kNone);
  assert(!ledger.pending(1));
  assert(ledger.Reuse(1, {.focus = false}, false) == Activation::kNone);
  assert(ledger.Reuse(1, {.focus = true, .behind = true}, false) == Activation::kNone);
  assert(ledger.Reuse(1, {.focus = true}, false) == Activation::kActivate);
  assert(ledger.pending(1));
  assert(ledger.Reuse(2, {.focus = false, .utility = true}, true) == Activation::kActivate);
}

static void ReadinessRetriesOnlyWhileNothingElseWasActivated() {
  FocusLedger ledger(true);
  ledger.Observe(1, true);
  ledger.Create(2, {.focus = true});
  assert(ledger.Ready(2) == Activation::kActivate);
  assert(!ledger.pending(2));
  assert(ledger.Ready(2) == Activation::kNone);

  ledger.Create(3, {.focus = true});
  ledger.Observe(1, false);
  assert(ledger.Ready(3) == Activation::kNone);

  ledger.Create(4, {.focus = true});
  assert(ledger.Observe(4, true));
  assert(!ledger.pending(4));
  assert(ledger.Ready(4) == Activation::kNone);

  ledger.Create(5, {.focus = true});
  ledger.Forget(5);
  assert(!ledger.pending(5));
  assert(ledger.Ready(5) == Activation::kNone);
}

static void ObservedActivationIsReportedOncePerTransition() {
  FocusLedger ledger(false);
  assert(ledger.Observe(1, true));
  assert(!ledger.Observe(1, true));
  assert(ledger.active() == 1);
  assert(ledger.Observe(2, true));
  assert(!ledger.Observe(1, false));
  assert(ledger.active() == 2);
  assert(!ledger.Observe(2, false));
  assert(ledger.active() == 0);
  assert(ledger.Observe(2, true));
  ledger.Forget(2);
  assert(ledger.active() == 0);
}

static void SharedFramesUseBottomUpPrimaryCoordinates() {
  const Rect primary{0, 0, 1920, 1080};
  const auto shared = SharedFrame({100, 50, 800, 600}, primary);
  assert((shared == std::array<int, 4>{100, 430, 800, 600}));
  const auto back = RequestedFrame({100, 430, 800, 600}, primary, true);
  assert(back && back->positioned);
  assert(back->bounds.x == 100 && back->bounds.y == 50);
  assert(back->bounds.width == 800 && back->bounds.height == 600);
  const Rect offset{-1280, 200, 1920, 1080};
  const auto moved = SharedFrame({-1280, 200, 640, 480}, offset);
  assert((moved == std::array<int, 4>{0, 600, 640, 480}));
  const auto restored = RequestedFrame({0, 600, 640, 480}, offset, true);
  assert(restored->bounds.x == -1280 && restored->bounds.y == 200);
  const auto rounded = RequestedFrame({10.4, 20.6, 800.5, 600.2}, primary, true);
  assert(rounded->bounds.width == 801 && rounded->bounds.height == 600);
}

static void WaylandFramesCarrySizeOnly() {
  const Rect primary{0, 0, 1920, 1080};
  const auto wayland = RequestedFrame({400, 300, 900, 700}, primary, false);
  assert(wayland && !wayland->positioned);
  assert(wayland->bounds.width == 900 && wayland->bounds.height == 700);
  GeometryLedger geometry(false);
  assert(!geometry.Frame({0, 0, 900, 700}, primary, false, false));
}

static void MalformedFramesAreRejected() {
  const Rect primary{0, 0, 1920, 1080};
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  assert(!RequestedFrame({0, 0, 800}, primary, true));
  assert(!RequestedFrame({0, 0, 0, 600}, primary, true));
  assert(!RequestedFrame({0, 0, 800, -1}, primary, true));
  assert(!RequestedFrame({nan, 0, 800, 600}, primary, true));
  assert(!RequestedFrame({0, inf, 800, 600}, primary, true));
  assert(!RequestedFrame({0, 0, 1e9, 600}, primary, true));
  assert(!RequestedFrame({1e12, 0, 800, 600}, primary, true));
}

static void FrameReportsSkipFullscreenMinimizedAndRepeats() {
  const Rect primary{0, 0, 1920, 1080};
  GeometryLedger geometry(true);
  const auto first = geometry.Frame({100, 50, 800, 600}, primary, false, false);
  assert(first && (*first == std::array<int, 4>{100, 430, 800, 600}));
  assert(!geometry.Frame({100, 50, 800, 600}, primary, false, false));
  assert(!geometry.Frame({0, 0, 1920, 1080}, primary, true, false));
  assert(!geometry.Frame({0, 0, 160, 28}, primary, false, true));
  assert(!geometry.Frame({0, 0, 0, 0}, primary, false, false));
  assert(geometry.Frame({120, 50, 800, 600}, primary, false, false));
  geometry.Reset();
  assert(geometry.Frame({120, 50, 800, 600}, primary, false, false));
}

static void FullScreenChangesAreReportedOnTransitionsOnly() {
  GeometryLedger geometry(false);
  assert(!geometry.FullScreen(false));
  assert(geometry.FullScreen(true) == std::optional<bool>(true));
  assert(!geometry.FullScreen(true));
  assert(geometry.FullScreen(false) == std::optional<bool>(false));
}

int main() {
  NewRegularWindowsHonorRequestedFocus();
  BackgroundInstancesNeverActivate();
  ReuseFollowsIntentButIgnoresTheWindowsOwnLaunchEcho();
  ReadinessRetriesOnlyWhileNothingElseWasActivated();
  APointerPressBlocksTheReadinessRetry();
  ObservedActivationIsReportedOncePerTransition();
  SharedFramesUseBottomUpPrimaryCoordinates();
  WaylandFramesCarrySizeOnly();
  MalformedFramesAreRejected();
  FrameReportsSkipFullscreenMinimizedAndRepeats();
  FullScreenChangesAreReportedOnTransitionsOnly();
}
'''


from document_host_fixture import values
from test_renderer_contract import method as extract

REPO = ROOT.parents[1]
LOADER = REPO / 'apps' / 'linux' / 'scripts' / 'host-test-loader.mjs'
WINDOW_DROP = REPO / 'apps' / 'browser' / 'src' / 'state' / 'drag' / 'windowDrop.ts'


def fake_values():
    return values().replace('''  const DictValue* FindDict''', '''  std::optional<bool> FindBool(std::string_view key) const {
    auto* value = Find(key); return value && value->is_bool() ? std::optional(value->GetBool()) : std::nullopt;
  }
  Value* Set(std::string_view key, const char* value) & { return Set(key, Value(value)); }
  DictValue&& Set(std::string_view key, const char* value) && { Set(key, value); return std::move(*this); }
  Value* Set(std::string_view key, bool value) & { return Set(key, Value(value)); }
  DictValue&& Set(std::string_view key, bool value) && { Set(key, value); return std::move(*this); }
  Value* Set(std::string_view key, int value) & { return Set(key, Value(value)); }
  DictValue&& Set(std::string_view key, int value) && { Set(key, value); return std::move(*this); }
  Value* Set(std::string_view key, ListValue&& value) & { return Set(key, Value(std::move(value))); }
  DictValue&& Set(std::string_view key, ListValue&& value) && { Set(key, std::move(value)); return std::move(*this); }
  const DictValue* FindDict''').replace('''  void Append(Value&& value) & { entries_.push_back(std::move(value)); }''', '''  void Append(Value&& value) & { entries_.push_back(std::move(value)); }
  void Append(int value) & { entries_.push_back(Value(value)); }
  void Append(const char* value) & { entries_.push_back(Value(value)); }
  ListValue&& Append(int value) && { Append(value); return std::move(*this); }
  ListValue&& Append(const char* value) && { Append(value); return std::move(*this); }''')


def between(start, end_marker, inclusive=True):
    begin = HOST.index(start)
    end = HOST.index(end_marker, begin) + (len(end_marker) if inclusive else 0)
    return HOST[begin:end]


PRELUDE = r'''#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "base/values.h"
#include "ardia/linux/ac_linux_profiles.h"
#include "ardia/linux/ac_linux_window_lifecycle.h"

#define FROM_HERE 0

namespace gfx {
class Rect {
 public:
  Rect() = default;
  Rect(int width, int height) : width_(width), height_(height) {}
  Rect(int x, int y, int width, int height) : x_(x), y_(y), width_(width), height_(height) {}
  int x() const { return x_; }
  int y() const { return y_; }
  int width() const { return width_; }
  int height() const { return height_; }
  bool operator==(const Rect&) const = default;
 private:
  int x_ = 0, y_ = 0, width_ = 0, height_ = 0;
};
}

namespace base {
template <typename T> class NoDestructor {
 public:
  template <typename... Args> explicit NoDestructor(Args&&... args) : value_(std::forward<Args>(args)...) {}
  T& operator*() { return value_; }
 private:
  T value_;
};
struct CommandLine {
  static CommandLine* ForCurrentProcess() { static CommandLine line; return &line; }
  bool HasSwitch(std::string_view) const { return false; }
  std::string GetSwitchValueASCII(std::string_view) const { return ""; }
};
template <typename T> class AutoReset {
 public:
  AutoReset(T* target, T value) : target_(target), old_(*target) { *target = value; }
  ~AutoReset() { *target_ = old_; }
 private:
  T* target_;
  T old_;
};
template <typename... Args> int BindOnce(Args&&...) { return 0; }
template <typename T> T* Unretained(T* value) { return value; }
inline int Milliseconds(int value) { return value; }
struct OneShotTimer {
  bool running = false;
  void Start(int, int, int) { running = true; }
  void Stop() { running = false; }
};
struct SingleThreadTaskRunner {
  static SingleThreadTaskRunner* GetCurrentDefault() { static SingleThreadTaskRunner runner; return &runner; }
  void PostTask(int, int) {}
};
template <typename T> class WeakPtr {
 public:
  WeakPtr() = default;
  explicit WeakPtr(T* value) : value_(value) {}
  T* get() const { return value_; }
  explicit operator bool() const { return value_ != nullptr; }
 private:
  T* value_ = nullptr;
};
inline std::string UTF8ToUTF16(std::string value) { return value; }
}

namespace ardiacore {
inline bool background = false;
inline bool BackgroundInstance() { return background; }
}

namespace ui {
struct PlatformProperties {
  bool supports_global_screen_coordinates = true;
};
struct OzonePlatform {
  static inline PlatformProperties properties;
  static bool IsInitialized() { return true; }
  static OzonePlatform* GetInstance() { static OzonePlatform platform; return &platform; }
  const PlatformProperties& GetPlatformProperties() const { return properties; }
};
enum class EventType { kMousePressed, kMouseReleased, kTouchPressed };
struct Event {
  EventType kind;
  EventType type() const { return kind; }
};
struct MouseEvent : Event {};
struct TouchEvent : Event {};
class EventHandler {
 public:
  virtual ~EventHandler() = default;
  virtual void OnMouseEvent(MouseEvent*) {}
  virtual void OnTouchEvent(TouchEvent*) {}
};
namespace mojom { enum class WindowShowState { kDefault, kNormal }; }
struct NativeWindow {
  std::string title;
  void SetTitle(std::string value) { title = std::move(value); }
};
struct BaseWindow {
  std::vector<std::string> calls;
  gfx::Rect bounds;
  bool maximized = false;
  NativeWindow native;
  void Show() { calls.push_back("Show"); }
  void ShowInactive() { calls.push_back("ShowInactive"); }
  void Activate() { calls.push_back("Activate"); }
  void SetBounds(const gfx::Rect& value) { calls.push_back("SetBounds"); bounds = value; }
  gfx::Rect GetBounds() const { return bounds; }
  void Minimize() { calls.push_back("Minimize"); }
  bool IsMaximized() const { return maximized; }
  void Restore() { maximized = false; }
  void Maximize() { maximized = true; }
  NativeWindow* GetNativeWindow() { return &native; }
};
}

namespace display {
struct Display {
  gfx::Rect rect;
  gfx::Rect bounds() const { return rect; }
};
struct Screen {
  static inline gfx::Rect primary{0, 0, 1920, 1080};
  static Screen* Get() { static Screen screen; return &screen; }
  Display GetPrimaryDisplay() const { return {primary}; }
};
}

namespace views {
struct Widget {
  bool fullscreen = false, minimized = false, active = false;
  gfx::Rect bounds;
  bool IsFullscreen() const { return fullscreen; }
  bool IsMinimized() const { return minimized; }
  bool IsActive() const { return active; }
  gfx::Rect GetWindowBoundsInScreen() const { return bounds; }
};
struct WebView {
  bool visible = true;
  std::function<void()> focused;
  void RequestFocus() { if (focused) focused(); }
  bool GetVisible() const { return visible; }
};
}

namespace content {
class RenderWidgetHost;
struct WebContents {
  int id = 0;
  std::function<void()> focused;
  void Focus() { if (focused) focused(); }
};
}

struct Profile {
  int prefs = 0;
  int* GetPrefs() { return &prefs; }
  base::WeakPtr<Profile> GetWeakPtr() { return base::WeakPtr<Profile>(this); }
};
struct SessionID {
  int value;
  int id() const { return value; }
};
struct BrowserWindowInterface {
  enum Type { TYPE_NORMAL, TYPE_APP };
  enum class CreationStatus { kOk };
  explicit BrowserWindowInterface(int value = 0) : session(value) {}
  int session = 0;
  ui::BaseWindow window;
  ui::BaseWindow* GetWindow() { return &window; }
  SessionID GetSessionID() const { return {session}; }
  base::WeakPtr<BrowserWindowInterface> GetWeakPtr() { return base::WeakPtr<BrowserWindowInterface>(this); }
};
struct BrowserWindowCreateParams {
  BrowserWindowCreateParams(Profile*, bool) {}
  BrowserWindowInterface::Type type = BrowserWindowInterface::TYPE_NORMAL;
  std::string app_name;
  bool is_trusted_source = false, omit_from_session_restore = false, should_trigger_session_restore = true;
  std::string user_title;
  bool can_resize = true, can_maximize = true, can_fullscreen = true;
  gfx::Rect initial_bounds;
  ui::mojom::WindowShowState initial_show_state = ui::mojom::WindowShowState::kDefault;
};
std::vector<std::unique_ptr<BrowserWindowInterface>> created_browsers;
std::vector<gfx::Rect> created_bounds;
int next_session = 100;
BrowserWindowInterface* CreateBrowserWindow(BrowserWindowCreateParams params) {
  auto browser = std::make_unique<BrowserWindowInterface>();
  browser->session = next_session++;
  created_bounds.push_back(params.initial_bounds);
  created_browsers.push_back(std::move(browser));
  return created_browsers.back().get();
}
namespace chrome {
inline const base::DictValue* GetWindowPlacementDictionaryReadOnly(const std::string&, int*) { return nullptr; }
}
inline bool ValidUtilityPlacement(const base::DictValue*) { return false; }
std::string String(const base::DictValue& dict, std::string_view key) {
  const auto* value = dict.FindString(key);
  return value ? *value : "";
}
std::string ArgumentString(const base::ListValue& values, size_t index) {
  return index < values.size() && values[index].is_string() ? values[index].GetString() : "";
}
bool DefaultProfileId(const std::string& profile) { return profile.empty() || profile == "default"; }
struct RequestedWindow {
  std::string id;
  std::string kind;
  std::string title;
  base::DictValue utility;
  base::WeakPtr<BrowserWindowInterface> browser;
  std::string page_profile;
  base::WeakPtr<Profile> app_profile;
  base::DictValue private_window;
  int opener_host = 0;
  uint64_t opener_generation = 0;
};
std::map<int, RequestedWindow>& RequestedWindows() { static std::map<int, RequestedWindow> windows; return windows; }
struct Done {
  std::function<void(bool, base::Value)> reply;
  void Run(bool ok, base::Value value) && { reply(ok, std::move(value)); }
};
'''

HOST_FAKE = r'''
struct ShellHostView {
  ShellHostView(int id, std::string window_id, BrowserWindowInterface* browser, std::string kind = "")
      : id_(id), window_id_(std::move(window_id)), kind_(std::move(kind)), browser_(browser) { Hosts()[id_] = this; }
  ~ShellHostView() { Hosts().erase(id_); }
  int id_;
  std::string window_id_;
  std::string kind_;
  BrowserWindowInterface* browser_;
  Profile profile_;
  views::Widget widget;
  base::DictValue utility_;
  bool utility_ready_ = false;
  bool utility_close_pending_ = false;
  linux_window::GeometryLedger geometry_{GlobalScreenCoordinates()};
  base::OneShotTimer frame_timer_;
  std::vector<base::DictValue> events;
  std::vector<std::pair<int, std::string>> reports;
  int id() const { return id_; }
  const std::string& window_id() const { return window_id_; }
  bool is_utility() const { return !kind_.empty(); }
  BrowserWindowInterface* browser() const { return browser_; }
  Profile* profile() { return &profile_; }
  std::string page_profile_;
  int opener_host_ = 0;
  uint64_t opener_generation_ = 0;
  uint64_t renderer_generation_ = 1;
  ardiacore::linux_profiles::WindowAccess access() const { return {window_id_, page_profile_, id_, opener_host_, opener_generation_}; }
  bool Reaches(const ShellHostView* target) const {
    return target && ardiacore::linux_profiles::WindowReachableBy(target->access(), {id_, renderer_generation_});
  }
  views::Widget* GetWidget() { return &widget; }
  const views::Widget* GetWidget() const { return &widget; }
  void Event(std::string_view module, std::string_view event, base::Value payload) {
    if (module == "ArdiaShell" && event == "onWindowEvent") events.push_back(payload.GetDict().Clone());
  }
  void NavigateUtility(base::DictValue) {}
  base::DictValue RequestWindowClose() { return {}; }
  void RequestUtilityClose() {}
  base::WeakPtr<ShellHostView> GetWeakPtr() { return base::WeakPtr<ShellHostView>(this); }
  void WindowInvoke(const std::string& method, const base::ListValue& values, Done done);
  void OnWidgetActivationChanged(views::Widget* widget, bool active);
  void OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect& bounds);
  void OnWidgetShowStateChanged(views::Widget* widget);
  void ReportWindowGeometry();
  std::vector<base::DictValue> WindowState() const;
  void SeedWindowActivation();
  void RendererReady();
};

struct TabBinding {
  ShellHostView* owner_ = nullptr;
  content::WebContents* contents_ = nullptr;
  views::WebView* viewport_ = nullptr;
  bool presented = true;
  bool host_focusing_ = false;
  void FocusFromHost();
  void OnWebContentsFocused(content::RenderWidgetHost*);
  void ReportPresentation(std::string name, base::DictValue data);
};
int TabId(content::WebContents* contents) { return contents->id; }
void TabBinding::ReportPresentation(std::string name, base::DictValue) {
  owner_->reports.emplace_back(TabId(contents_), std::move(name));
}
'''

SCENARIOS = r'''
static base::Value Call(ShellHostView& host, const std::string& method, base::ListValue values, bool* ok = nullptr) {
  base::Value result;
  bool succeeded = false;
  host.WindowInvoke(method, values, Done{[&](bool success, base::Value value) { succeeded = success; result = std::move(value); }});
  if (ok) *ok = succeeded;
  return result;
}

static base::ListValue Args(const char* id, base::DictValue options) {
  base::ListValue values;
  values.Append(id);
  values.Append(base::Value(std::move(options)));
  return values;
}

static base::ListValue Frame(int x, int y, int width, int height) {
  return base::ListValue().Append(x).Append(y).Append(width).Append(height);
}

static int Count(const std::vector<std::string>& calls, const char* call) {
  return static_cast<int>(std::count(calls.begin(), calls.end(), call));
}

static int EventsOf(const ShellHostView& host, const char* type, const char* id) {
  int count = 0;
  for (const auto& event : host.events) {
    if (*event.FindString("type") == type && *event.FindString("id") == id) ++count;
  }
  return count;
}

static void Created() {
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  Call(main, "ArdiaShell.openWindow", Args("w-new", base::DictValue().Set("focus", true).Set("frame", Frame(100, 430, 800, 600))));
  assert(created_bounds.back() == gfx::Rect(100, 50, 800, 600));
  assert((created_browsers.back()->window.calls == std::vector<std::string>{"Show", "Activate"}));
  Call(main, "ArdiaShell.openWindow", Args("w-behind", base::DictValue().Set("focus", false).Set("behind", "main")));
  assert((created_browsers.back()->window.calls == std::vector<std::string>{"ShowInactive"}));
  bool ok = false;
  Call(main, "ArdiaShell.openWindow", Args("w-bad", base::DictValue().Set("frame", Frame(0, 0, 0, 600))), &ok);
  assert(ok && created_bounds.back() == gfx::Rect());
}

static void Reused() {
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  BrowserWindowInterface full_browser{2};
  ShellHostView full(2, "w-full", &full_browser);
  full.widget.fullscreen = true;
  Call(main, "ArdiaShell.openWindow", Args("w-full", base::DictValue().Set("focus", false).Set("frame", Frame(5, 5, 640, 480))));
  Call(full, "ArdiaShell.openWindow", Args("w-full", base::DictValue().Set("focus", true).Set("frame", Frame(5, 5, 640, 480))));
  assert(Count(full_browser.window.calls, "SetBounds") == 0);
  assert(Count(full_browser.window.calls, "Activate") == 0);
  Call(main, "ArdiaShell.openWindow", Args("w-full", base::DictValue().Set("focus", true)));
  assert(Count(full_browser.window.calls, "Activate") == 1);
  Call(main, "ArdiaShell.focusWindow", base::ListValue().Append("w-full"));
  assert(Count(full_browser.window.calls, "Activate") == 2);
}

static void Ready() {
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  main.widget.bounds = gfx::Rect(0, 0, 800, 600);
  BrowserWindowInterface full_browser{2};
  ShellHostView full(2, "w-full", &full_browser);
  full.widget.fullscreen = true;
  Call(main, "ArdiaShell.openWindow", Args("w-new", base::DictValue().Set("focus", true)));
  BrowserWindowInterface* created = created_browsers.back().get();
  ShellHostView arrived(created->GetSessionID().id(), "w-new", created);
  arrived.widget.bounds = gfx::Rect(100, 50, 800, 600);
  Call(arrived, "ArdiaShell.openWindow", Args("w-new", base::DictValue().Set("focus", true)));
  assert(Count(created->window.calls, "Activate") == 2);
  assert(EventsOf(main, "frame", "w-new") == 1);
  assert(EventsOf(full, "frame", "w-new") == 1);
  assert(EventsOf(arrived, "frame", "w-new") == 1);
  assert(EventsOf(arrived, "frame", "main") == 1);
  assert(EventsOf(arrived, "fullScreen", "w-full") == 1);
  assert(EventsOf(main, "frame", "main") == 0);
}

static void Clicked() {
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  main.OnWidgetActivationChanged(&main.widget, true);
  Call(main, "ArdiaShell.openWindow", Args("w-clicked", base::DictValue().Set("focus", true)));
  BrowserWindowInterface* clicked = created_browsers.back().get();
  WindowPointerHandler pointer;
  ui::MouseEvent release{{ui::EventType::kMouseReleased}};
  pointer.OnMouseEvent(&release);
  ui::MouseEvent press{{ui::EventType::kMousePressed}};
  pointer.OnMouseEvent(&press);
  ShellHostView refused(clicked->GetSessionID().id(), "w-clicked", clicked);
  Call(refused, "ArdiaShell.openWindow", Args("w-clicked", base::DictValue().Set("focus", true)));
  assert(Count(clicked->window.calls, "Activate") == 1);
}

static void Activation() {
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  BrowserWindowInterface other_browser{2};
  ShellHostView other(2, "w-other", &other_browser);
  BrowserWindowInterface settings_browser{3};
  ShellHostView settings(3, "settings", &settings_browser, "settings");
  other.OnWidgetActivationChanged(&other.widget, true);
  assert(EventsOf(main, "focus", "w-other") == 1 && EventsOf(other, "focus", "w-other") == 1);
  assert(settings.events.empty());
  other.OnWidgetActivationChanged(&other.widget, true);
  assert(EventsOf(main, "focus", "w-other") == 1);
  settings.OnWidgetActivationChanged(&settings.widget, true);
  assert(EventsOf(main, "focus", "settings") == 0 && settings.events.empty());
}

static void Seeded() {
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  main.widget.bounds = gfx::Rect(0, 0, 800, 600);
  BrowserWindowInterface seeded_browser{4};
  ShellHostView seeded(4, "w-seeded", &seeded_browser);
  seeded.widget.active = true;
  seeded.SeedWindowActivation();
  assert(EventsOf(main, "focus", "w-seeded") == 1);
  seeded.events.clear();
  seeded.RendererReady();
  assert(EventsOf(seeded, "focus", "w-seeded") == 1);
  assert(*seeded.events.back().FindString("type") == "focus");
}

static void Geometry() {
  BrowserWindowInterface main_browser{1};
  main_browser.window.bounds = gfx::Rect(100, 50, 800, 600);
  ShellHostView main(1, "main", &main_browser);
  BrowserWindowInterface other_browser{2};
  ShellHostView other(2, "w-other", &other_browser);
  BrowserWindowInterface settings_browser{3};
  ShellHostView settings(3, "settings", &settings_browser, "settings");
  main.widget.bounds = gfx::Rect(10, 20, 900, 700);
  main.OnWidgetBoundsChanged(&main.widget, main.widget.bounds);
  assert(main.frame_timer_.running && main.events.empty());
  main.ReportWindowGeometry();
  const base::ListValue* frame = main.events.back().FindList("frame");
  assert(frame && (*frame == Frame(10, 360, 900, 700)));
  assert(EventsOf(other, "frame", "main") == 1 && settings.events.empty());
  main.ReportWindowGeometry();
  assert(EventsOf(other, "frame", "main") == 1);
  main.widget.fullscreen = true;
  main.OnWidgetShowStateChanged(&main.widget);
  assert(*main.events.back().FindString("type") == "fullScreen" && main.events.back().FindBool("on") == std::optional<bool>(true));
  assert(EventsOf(main, "frame", "main") == 1);
  bool ok = false;
  base::Value reply = Call(main, "ArdiaShell.windowFrame", base::ListValue().Append("main"), &ok);
  assert(ok && reply.is_list() && (reply.GetList() == Frame(100, 430, 800, 600)));
}

static void PageFocus() {
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  content::WebContents page;
  page.id = 42;
  views::WebView viewport;
  TabBinding binding{&main, &page, &viewport};
  page.focused = [&] { binding.OnWebContentsFocused(nullptr); };
  viewport.focused = [&] { page.Focus(); };
  binding.FocusFromHost();
  assert(main.reports.empty());
  page.Focus();
  assert(main.reports.size() == 1 && main.reports[0] == std::make_pair(42, std::string("focus")));
  viewport.visible = false;
  page.Focus();
  assert(main.reports.size() == 1);
}

static void Wayland() {
  ui::OzonePlatform::properties.supports_global_screen_coordinates = false;
  BrowserWindowInterface main_browser{1};
  main_browser.window.bounds = gfx::Rect(0, 0, 800, 600);
  ShellHostView main(1, "main", &main_browser);
  main.widget.bounds = gfx::Rect(0, 0, 800, 600);
  bool ok = false;
  base::Value reply = Call(main, "ArdiaShell.windowFrame", base::ListValue().Append("main"), &ok);
  assert(ok && reply.is_none());
  Call(main, "ArdiaShell.openWindow", Args("w-torn", base::DictValue().Set("focus", true).Set("frame", Frame(400, 300, 900, 700))));
  assert(created_bounds.back() == gfx::Rect(0, 0, 900, 700));
  main.ReportWindowGeometry();
  assert(EventsOf(main, "frame", "main") == 0);
  main.RendererReady();
  assert(EventsOf(main, "frame", "main") == 0);
}

static void Background() {
  ardiacore::background = true;
  BrowserWindowInterface main_browser{1};
  ShellHostView main(1, "main", &main_browser);
  BrowserWindowInterface settings_browser{2};
  ShellHostView settings(2, "settings", &settings_browser, "settings");
  Call(main, "ArdiaShell.openWindow", Args("w-new", base::DictValue().Set("focus", true)));
  assert((created_browsers.back()->window.calls == std::vector<std::string>{"ShowInactive"}));
  Call(main, "ArdiaShell.focusWindow", base::ListValue().Append("main"));
  assert((main_browser.window.calls == std::vector<std::string>{"ShowInactive"}));
  Call(main, "ArdiaShell.openWindow", Args("settings", base::DictValue().Set("kind", "settings")));
  assert((settings_browser.window.calls == std::vector<std::string>{"ShowInactive"}));
}

static void Frames() {
  BrowserWindowInterface a_browser{1};
  a_browser.window.bounds = gfx::Rect(0, 0, 800, 600);
  ShellHostView a(1, "a", &a_browser);
  BrowserWindowInterface b_browser{2};
  b_browser.window.bounds = gfx::Rect(900, 400, 800, 600);
  ShellHostView b(2, "b", &b_browser);
  base::Value fa = Call(a, "ArdiaShell.windowFrame", base::ListValue().Append("a"));
  base::Value fb = Call(a, "ArdiaShell.windowFrame", base::ListValue().Append("b"));
  auto print = [](const base::Value& frame) {
    const auto& list = frame.GetList();
    std::printf("[%d,%d,%d,%d]", list[0].GetInt(), list[1].GetInt(), list[2].GetInt(), list[3].GetInt());
  };
  std::printf("{\"a\":");
  print(fa);
  std::printf(",\"b\":");
  print(fb);
  std::printf("}\n");
}

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  const std::map<std::string, void (*)()> scenarios = {
      {"created", Created}, {"reused", Reused}, {"ready", Ready}, {"clicked", Clicked},
      {"activation", Activation}, {"seeded", Seeded}, {"geometry", Geometry}, {"page-focus", PageFocus},
      {"wayland", Wayland}, {"background", Background}, {"frames", Frames}};
  const auto found = scenarios.find(mode);
  if (found == scenarios.end()) return 2;
  found->second();
}
'''

DROP = r'''globalThis.__DEV__ = false;
const [frames, module] = [JSON.parse(process.argv[2]), process.argv[3]];
const { screenPoint, windowUnder } = await import(module);
const state = {
  windows: {
    a: { id: "a", incognito: false, kind: "browser", frame: frames.a },
    b: { id: "b", incognito: false, kind: "browser", frame: frames.b },
  },
  ui: { focusOrder: ["a", "b"] },
};
const local = [1000, 500];
const under = windowUnder(state, "a", local[0], local[1], frames.a);
const screen = screenPoint(state, "a", local[0], local[1], frames.a);
const over = frames.b;
console.log(JSON.stringify({ under, overPoint: [screen[0] - over[0], over[1] + over[3] - screen[1]] }));
'''


def profile_openings():
    glue = r'''
namespace base {
inline std::optional<std::string> WriteJson(const DictValue&) { return std::string("{}"); }
}
namespace ardiacore {
inline bool LinuxProfileHosts(std::string_view, const Profile*) { return true; }
}
namespace browser_shutdown {
inline bool IsTryingToQuit() { return false; }
}
inline BrowserWindowInterface::CreationStatus GetBrowserWindowCreationStatusForProfile(Profile&) {
  return BrowserWindowInterface::CreationStatus::kOk;
}
using WindowReply = Done;
'''
    real = '\n'.join([
        extract(HOST, 'base::Value ProfileFailure('),
        between('constexpr size_t kPrivateWindowLimit', '\n'),
        extract(HOST, 'bool PrivateProfileId('),
        extract(HOST, 'struct WindowOpening {') + ';',
        extract(HOST, 'ardiacore::linux_profiles::OpeningLedger<WindowReply>& OpeningWindows() {'),
        extract(HOST, 'void FinishWindowOpening('),
        extract(HOST, 'void OpenNativeWindow('),
    ])
    begin = r'''
void BeginWindowOpening(WindowOpening opening, uint64_t generation, Profile* utility_profile) {
  static Profile page;
  OpenNativeWindow(std::move(opening), generation, utility_profile ? utility_profile : &page,
                   ardiacore::linux_profiles::ProfileError::kNone);
}
'''
    return glue + real + begin


def host_harness():
    free = between('namespace linux_window = ardia::linux_window;', 'class WindowPointerHandler', False)
    pointer = between('class WindowPointerHandler', '\n};\n')
    window_branch = extract(HOST, '  if (method == "ArdiaShell.windowFrame" || method == "ArdiaShell.closeWindow"')
    open_branch = extract(HOST, '  if (method == "ArdiaShell.openWindow")')
    broadcast = extract(HOST, 'void BroadcastWindowEvent(')
    members = '\n'.join(extract(HOST, signature) for signature in [
        'void ShellHostView::OnWidgetActivationChanged(',
        'void ShellHostView::OnWidgetBoundsChanged(',
        'void ShellHostView::OnWidgetShowStateChanged(',
        'void ShellHostView::ReportWindowGeometry(',
        'std::vector<base::DictValue> ShellHostView::WindowState(',
        'void ShellHostView::SeedWindowActivation(',
        'void ShellHostView::RendererReady(',
        'void TabBinding::FocusFromHost(',
        'void TabBinding::OnWebContentsFocused('])
    hosts = 'struct ShellHostView;\nstd::map<int, ShellHostView*>& Hosts() { static std::map<int, ShellHostView*> hosts; return hosts; }\n'
    invoke = ('void ShellHostView::WindowInvoke(const std::string& method, const base::ListValue& values, Done done) {\n'
              + window_branch + '\n' + open_branch + '\n  std::move(done).Run(false, base::Value("unsupported"));\n}\n')
    return PRELUDE + hosts + free + pointer + HOST_FAKE + broadcast + '\n' + profile_openings() + invoke + members + SCENARIOS


class WindowLifecycleHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise unittest.SkipTest('C++ compiler is unavailable')
        cls.scratch = tempfile.TemporaryDirectory(prefix='ardia-window-host-')
        root = pathlib.Path(cls.scratch.name)
        (root / 'base').mkdir()
        (root / 'base' / 'values.h').write_text(fake_values())
        (root / 'host.cc').write_text(host_harness())
        cls.binary = root / 'host'
        result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                                 '-I', str(root), '-I', str(SOURCE), str(root / 'host.cc'),
                                 str(LINUX / 'ac_linux_window_lifecycle.cc'), str(LINUX / 'ac_linux_profiles.cc'), '-o', str(cls.binary)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            cls.scratch.cleanup()
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def run_mode(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_new_windows_follow_focus_behind_and_frame_intent(self):
        self.run_mode('created')

    def test_reuse_never_moves_or_unfullscreens_an_existing_window(self):
        self.run_mode('reused')

    def test_readiness_retries_activation_and_broadcasts_the_new_windows_state(self):
        self.run_mode('ready')

    def test_a_pointer_press_blocks_the_readiness_retry(self):
        self.run_mode('clicked')

    def test_observed_activation_reaches_regular_renderers_only(self):
        self.run_mode('activation')

    def test_activation_before_attach_is_seeded_and_replayed_last(self):
        self.run_mode('seeded')

    def test_geometry_reports_use_the_shared_contract(self):
        self.run_mode('geometry')

    def test_app_focus_is_not_reported_as_page_focus(self):
        self.run_mode('page-focus')

    def test_wayland_has_no_origins_in_replies_or_reports(self):
        self.run_mode('wayland')

    def test_background_instances_show_without_activating(self):
        self.run_mode('background')

    def test_shared_drop_code_targets_windows_from_native_frames(self):
        node = shutil.which('node')
        if node is None:
            self.skipTest('node is unavailable')
        frames = self.run_mode('frames').strip()
        with tempfile.TemporaryDirectory(prefix='ardia-window-drop-') as scratch:
            script = pathlib.Path(scratch) / 'drop.mjs'
            script.write_text(DROP)
            result = subprocess.run([node, '--experimental-strip-types', '--no-warnings', '--import', LOADER.as_uri(),
                                     str(script), frames, WINDOW_DROP.as_uri()],
                                    capture_output=True, text=True, cwd=REPO / 'apps' / 'linux')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        import json
        reply = json.loads(result.stdout.strip().splitlines()[-1])
        self.assertEqual(reply['under'], {'outside': True, 'overWindow': 'b'})
        self.assertEqual(reply['overPoint'], [100, 100])

    def test_native_glue_seeds_activation_and_registers_pointer_input(self):
        initialize = extract(HOST, 'void ShellHostView::Initialize()')
        self.assertIn('SeedWindowActivation();', initialize)
        self.assertIn('AddPreTargetHandler(&pointer_handler_', initialize)
        self.assertIn('RemovePreTargetHandler(&pointer_handler_)', extract(HOST, 'void ShellHostView::UnregisterAccelerators()'))
        self.assertIn('binding->FocusFromHost();', HOST)
        self.assertIn('WindowFocus().Forget(browser->GetSessionID().id());', extract(HOST, 'void OnBrowserClosed('))

    def test_lifecycle_helper_is_built(self):
        build = (LINUX / 'BUILD.gn').read_text()
        for name in ('"ac_linux_window_lifecycle.cc"', '"ac_linux_window_lifecycle.h"', '"//ui/display"', '"//ui/ozone"'):
            self.assertIn(name, build)

    def test_new_sources_have_no_comments(self):
        for path in (LINUX / 'ac_linux_window_lifecycle.h', LINUX / 'ac_linux_window_lifecycle.cc'):
            self.assertIsNone(re.search(r'//|/\*', path.read_text()), path)


if __name__ == '__main__':
    unittest.main()
