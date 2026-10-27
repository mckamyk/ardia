#ifndef ARDIA_LINUX_AC_LINUX_WINDOW_LIFECYCLE_H_
#define ARDIA_LINUX_AC_LINUX_WINDOW_LIFECYCLE_H_

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace ardia::linux_window {

struct OpenIntent {
  bool focus = true;
  bool behind = false;
  bool utility = false;
};

enum class Presentation { kActivate, kInactive };
enum class Activation { kNone, kShowInactive, kActivate };

class FocusLedger {
 public:
  explicit FocusLedger(bool may_activate) : may_activate_(may_activate) {}

  Presentation Create(int window, const OpenIntent& intent);
  Activation Reuse(int window, const OpenIntent& intent, bool own_renderer);
  Activation Focus(int window);
  Activation Ready(int window);
  bool Observe(int window, bool active);
  void PointerPressed() { ++epoch_; }
  void Forget(int window);

  bool pending(int window) const { return pending_.contains(window); }
  int active() const { return active_; }

 private:
  bool Wants(const OpenIntent& intent) const;
  void Request(int window) { pending_[window] = epoch_; }

  bool may_activate_;
  uint64_t epoch_ = 0;
  int active_ = 0;
  std::map<int, uint64_t> pending_;
};

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct Requested {
  Rect bounds;
  bool positioned = false;
};

std::array<int, 4> SharedFrame(const Rect& bounds, const Rect& primary);
std::optional<Requested> RequestedFrame(const std::vector<double>& frame,
                                        const Rect& primary,
                                        bool global_coordinates);

class GeometryLedger {
 public:
  explicit GeometryLedger(bool global_coordinates)
      : global_coordinates_(global_coordinates) {}

  std::optional<std::array<int, 4>> Frame(const Rect& bounds,
                                          const Rect& primary,
                                          bool fullscreen,
                                          bool minimized);
  std::optional<bool> FullScreen(bool fullscreen);
  void Reset() { last_frame_.reset(); }

 private:
  bool global_coordinates_;
  std::optional<std::array<int, 4>> last_frame_;
  bool fullscreen_ = false;
};

}

#endif
