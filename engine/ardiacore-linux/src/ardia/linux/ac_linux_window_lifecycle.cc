#include "ardia/linux/ac_linux_window_lifecycle.h"

#include <cmath>

namespace ardia::linux_window {

namespace {

constexpr double kMaxExtent = 100000;
constexpr double kMaxOrigin = 1000000;

}

bool FocusLedger::Wants(const OpenIntent& intent) const {
  return may_activate_ && (intent.utility || (intent.focus && !intent.behind));
}

Presentation FocusLedger::Create(int window, const OpenIntent& intent) {
  if (!Wants(intent)) return Presentation::kInactive;
  Request(window);
  return Presentation::kActivate;
}

Activation FocusLedger::Reuse(int window, const OpenIntent& intent,
                              bool own_renderer) {
  if (!Wants(intent) || (own_renderer && !intent.utility)) return Activation::kNone;
  Request(window);
  return Activation::kActivate;
}

Activation FocusLedger::Focus(int window) {
  if (!may_activate_) return Activation::kNone;
  Request(window);
  return Activation::kActivate;
}

Activation FocusLedger::Ready(int window) {
  const auto found = pending_.find(window);
  if (found == pending_.end()) return Activation::kNone;
  const uint64_t requested = found->second;
  pending_.erase(found);
  if (!may_activate_ || active_ == window || requested != epoch_) {
    return Activation::kNone;
  }
  return Activation::kActivate;
}

bool FocusLedger::Observe(int window, bool active) {
  if (active) {
    pending_.erase(window);
    if (active_ == window) return false;
    active_ = window;
    ++epoch_;
    return true;
  }
  if (active_ != window) return false;
  active_ = 0;
  ++epoch_;
  return false;
}

void FocusLedger::Forget(int window) {
  pending_.erase(window);
  if (active_ == window) active_ = 0;
}

std::array<int, 4> SharedFrame(const Rect& bounds, const Rect& primary) {
  return {bounds.x - primary.x,
          primary.y + primary.height - (bounds.y + bounds.height),
          bounds.width, bounds.height};
}

std::optional<Requested> RequestedFrame(const std::vector<double>& frame,
                                        const Rect& primary,
                                        bool global_coordinates) {
  if (frame.size() != 4) return std::nullopt;
  for (const double value : frame) {
    if (!std::isfinite(value)) return std::nullopt;
  }
  if (std::fabs(frame[0]) > kMaxOrigin || std::fabs(frame[1]) > kMaxOrigin ||
      frame[2] > kMaxExtent || frame[3] > kMaxExtent) {
    return std::nullopt;
  }
  const int width = static_cast<int>(std::lround(frame[2]));
  const int height = static_cast<int>(std::lround(frame[3]));
  if (width <= 0 || height <= 0) return std::nullopt;
  Requested requested;
  requested.bounds.width = width;
  requested.bounds.height = height;
  requested.positioned = global_coordinates;
  if (global_coordinates) {
    requested.bounds.x = primary.x + static_cast<int>(std::lround(frame[0]));
    requested.bounds.y = primary.y + primary.height -
                         (static_cast<int>(std::lround(frame[1])) + height);
  }
  return requested;
}

std::optional<std::array<int, 4>> GeometryLedger::Frame(const Rect& bounds,
                                                        const Rect& primary,
                                                        bool fullscreen,
                                                        bool minimized) {
  if (!global_coordinates_ || fullscreen || minimized || bounds.width <= 0 ||
      bounds.height <= 0) {
    return std::nullopt;
  }
  const auto frame = SharedFrame(bounds, primary);
  if (last_frame_ == frame) return std::nullopt;
  last_frame_ = frame;
  return frame;
}

std::optional<bool> GeometryLedger::FullScreen(bool fullscreen) {
  if (fullscreen == fullscreen_) return std::nullopt;
  fullscreen_ = fullscreen;
  return fullscreen;
}

}
