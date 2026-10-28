#pragma once

#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace content { class WebContents; }

namespace views {
class View;
class FocusManager {
 public:
  View* GetFocusedView() const { return focused; }
  View* focused = nullptr;
};
inline FocusManager focus_manager;
class View {
 public:
  virtual ~View() = default;
  virtual void RequestFocus() { if (visible) focus_manager.focused = this; }
  bool IsFocusable() const { return visible; }
  bool visible = true;
};
class WebView : public View {
 public:
  void RequestFocus() override;
  content::WebContents* contents = nullptr;
};
}

namespace content {
class WebContents {
 public:
  void Focus() { focuses++; }
  int focuses = 0;
};
}
inline void views::WebView::RequestFocus() {
  View::RequestFocus();
  if (visible && contents) contents->Focus();
}

class ChromeWebContentsViewFocusHelper {
 public:
  static ChromeWebContentsViewFocusHelper* FromWebContents(content::WebContents* contents) {
    return &helpers[contents];
  }
  views::View* GetStoredFocus() { return stored && stored->IsFocusable() ? stored : nullptr; }
  void SetStoredFocusView(views::View* view) { stored = view; }
  void StoreFocus() { stored = views::focus_manager.focused; }
  bool RestoreFocus() {
    views::View* view = GetStoredFocus();
    stored = nullptr;
    if (!view) return false;
    view->RequestFocus();
    return true;
  }
  views::View* stored = nullptr;
  static inline std::map<content::WebContents*, ChromeWebContentsViewFocusHelper> helpers;
};

class TabBinding {
 public:
  content::WebContents* contents() const { return contents_; }
  views::WebView* viewport() const { return viewport_; }
  views::WebView* devtools_viewport() const { return devtools_viewport_; }
  content::WebContents* contents_ = nullptr;
  views::WebView* viewport_ = nullptr;
  views::WebView* devtools_viewport_ = nullptr;
};

class ShellHostView {
 public:
  views::FocusManager* GetFocusManager() { return &views::focus_manager; }
  void KeepPaneFocus(TabBinding* activating);
  std::map<int, std::unique_ptr<TabBinding>> bindings_;
};

ARDIA_HOST_METHODS
