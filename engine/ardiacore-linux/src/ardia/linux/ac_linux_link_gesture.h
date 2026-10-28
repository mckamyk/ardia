#ifndef ARDIA_LINUX_AC_LINUX_LINK_GESTURE_H_
#define ARDIA_LINUX_AC_LINUX_LINK_GESTURE_H_

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>

#include "ardia/linux/ac_linux_page_messages.h"

namespace ardiacore {

inline constexpr int kLinuxLinkAlt = 1;
inline constexpr int kLinuxLinkShift = 2;
inline constexpr int kLinuxLinkControl = 4;
inline constexpr int kLinuxLinkMeta = 8;

int LinuxLinkKeyFlags(std::string_view key);

struct LinuxLinkPoint {
  double x = 0;
  double y = 0;
};

struct LinuxLinkRequest {
  uintptr_t surface = 0;
  std::string key;
  bool http = false;
  bool visible = false;
  bool same_origin = false;
  double now = 0;
};

enum class LinuxLinkOutcome { kNone, kPeek, kSplit, kWindow, kBackground, kDownload };

struct LinuxLinkDecision {
  LinuxLinkOutcome outcome = LinuxLinkOutcome::kNone;
  std::string refused;
  std::optional<LinuxLinkPoint> point;
};

class LinuxInputActivity {
 public:
  static constexpr double kInterval = 0.5;
  bool Note(double now);

 private:
  double last_ = -1;
};

class LinuxLinkGestureGate {
 public:
  static constexpr double kGestureLifetime = 10;
  static constexpr double kFallbackLifetime = 60;
  static constexpr double kDropLifetime = 2;
  void NotePress(uintptr_t surface);
  void NoteRelease(uintptr_t surface, int flags, double now, std::optional<LinuxLinkPoint> point);
  void Forget(uintptr_t surface);
  LinuxLinkDecision Decide(const LinuxLinkRequest& request, const LinuxPageOptions& options);
  uintptr_t DragSource() const { return press_open_ ? press_surface_ : 0; }
  void NoteDragEnded(uintptr_t surface, bool over, double now);
  std::string AcceptLinkDrop(uintptr_t surface, bool visible, double now);

 private:
  LinuxLinkDecision FallBack(const LinuxLinkRequest& request, const LinuxPageOptions& options, std::string reason);
  uintptr_t up_surface_ = 0;
  int up_flags_ = 0;
  double up_at_ = -1;
  uint64_t up_sequence_ = 0;
  uint64_t used_sequence_ = 0;
  bool pressed_since_up_ = false;
  std::optional<LinuxLinkPoint> up_point_;
  uintptr_t press_surface_ = 0;
  bool press_open_ = false;
  uintptr_t drop_surface_ = 0;
  bool drop_over_ = false;
  bool drop_used_ = true;
  bool pressed_since_drop_ = false;
  double drop_at_ = -1;
};

}

#endif
