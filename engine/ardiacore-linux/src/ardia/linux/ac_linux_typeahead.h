#ifndef ARDIA_LINUX_AC_LINUX_TYPEAHEAD_H_
#define ARDIA_LINUX_AC_LINUX_TYPEAHEAD_H_

#include <cstddef>
#include <utility>
#include <vector>

namespace ardia::linux_window {

template <typename Event>
class Typeahead {
 public:
  static constexpr size_t kMaxEvents = 512;

  void Start() {
    if (state_ == State::kIdle) state_ = State::kCapturing;
  }
  bool capturing() const { return state_ == State::kCapturing; }
  bool Hold(Event event) {
    if (!capturing() || held_.size() >= kMaxEvents) return false;
    held_.push_back(std::move(event));
    return true;
  }
  std::vector<Event> Release() {
    if (!capturing()) return {};
    state_ = State::kDone;
    return std::exchange(held_, {});
  }

 private:
  enum class State { kIdle, kCapturing, kDone };
  State state_ = State::kIdle;
  std::vector<Event> held_;
};

}

#endif
