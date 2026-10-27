// Copyright 2026 Ardia. Apache-2.0.

#ifndef CHROME_BROWSER_ARDIA_AC_REAUTH_LINUX_POLICY_H_
#define CHROME_BROWSER_ARDIA_AC_REAUTH_LINUX_POLICY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ardia::reauth_linux {

inline constexpr char kPolkitService[] = "org.freedesktop.PolicyKit1";
inline constexpr char kPolkitPath[] = "/org/freedesktop/PolicyKit1/Authority";
inline constexpr char kPolkitInterface[] = "org.freedesktop.PolicyKit1.Authority";
inline constexpr char kPolkitAction[] = "dev.ardia.linux.reauthenticate";
inline constexpr uint32_t kAllowUserInteraction = 1;
inline constexpr int64_t kPromptTimeoutMs = 120000;

enum class Outcome { kGranted, kDenied, kCancelled, kExpired, kUnavailable };

struct PolkitReply {
  bool responded = false;
  std::string error_name;
  bool authorized = false;
  bool challenge = false;
  bool dismissed = false;
};

inline const char* OutcomeName(Outcome outcome) {
  switch (outcome) {
    case Outcome::kGranted:
      return "granted";
    case Outcome::kDenied:
      return "denied";
    case Outcome::kCancelled:
      return "cancelled";
    case Outcome::kExpired:
      return "expired";
    case Outcome::kUnavailable:
      return "unavailable";
  }
  return "unavailable";
}

inline Outcome Classify(const PolkitReply& reply) {
  constexpr std::string_view kExpiredErrors[] = {
      "org.freedesktop.DBus.Error.NoReply", "org.freedesktop.DBus.Error.Timeout",
      "org.freedesktop.DBus.Error.TimedOut"};
  if (!reply.error_name.empty()) {
    for (std::string_view expired : kExpiredErrors) {
      if (reply.error_name == expired) {
        return Outcome::kExpired;
      }
    }
    if (reply.error_name == "org.freedesktop.PolicyKit1.Error.Cancelled") {
      return Outcome::kCancelled;
    }
    return Outcome::kUnavailable;
  }
  if (!reply.responded) {
    return Outcome::kUnavailable;
  }
  if (reply.authorized && !reply.challenge) {
    return Outcome::kGranted;
  }
  if (reply.dismissed) {
    return Outcome::kCancelled;
  }
  if (reply.challenge) {
    return Outcome::kUnavailable;
  }
  return Outcome::kDenied;
}

class GrantWindow {
 public:
  void Grant(int64_t now_ms) { granted_at_ms_ = now_ms; }
  void Clear() { granted_at_ms_.reset(); }
  bool Covers(int64_t now_ms, int64_t validity_ms) const {
    return validity_ms > 0 && granted_at_ms_ && now_ms >= *granted_at_ms_ &&
           now_ms - *granted_at_ms_ < validity_ms;
  }

 private:
  std::optional<int64_t> granted_at_ms_;
};

}

#endif
