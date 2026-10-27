#ifndef ARDIA_LINUX_AC_LINUX_FAVICON_POLICY_H_
#define ARDIA_LINUX_AC_LINUX_FAVICON_POLICY_H_

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace ardiacore::linux_favicons {

inline constexpr int kPixels = 64;
inline constexpr int64_t kMaxBytes = 2 * 1024 * 1024;
inline constexpr size_t kMaxUrlLength = 2 * 1024 * 1024;
inline constexpr int kTimeoutSeconds = 30;

enum class Source { kPage, kNetwork };

inline bool StartsWithLowercase(std::string_view text, std::string_view prefix) {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char c = text[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != prefix[i]) return false;
  }
  return true;
}

inline bool Allowed(std::string_view url, Source source) {
  if (url.empty() || url.size() > kMaxUrlLength) return false;
  if (StartsWithLowercase(url, "https://") || StartsWithLowercase(url, "http://")) return true;
  return source == Source::kPage && StartsWithLowercase(url, "data:image/");
}

inline int Pick(const std::vector<std::pair<int, int>>& sizes, int pixels = kPixels) {
  int best = -1;
  int best_size = 0;
  for (size_t i = 0; i < sizes.size(); ++i) {
    if (sizes[i].first <= 0 || sizes[i].second <= 0) continue;
    const int size = std::max(sizes[i].first, sizes[i].second);
    const bool better = best < 0 ||
        (best_size < pixels ? size > best_size : (size >= pixels && size < best_size));
    if (better) {
      best = static_cast<int>(i);
      best_size = size;
    }
  }
  return best;
}

inline std::pair<int, int> Fit(int width, int height, int pixels = kPixels) {
  if (width <= 0 || height <= 0) return {0, 0};
  const int longest = std::max(width, height);
  if (longest <= pixels) return {width, height};
  const auto scale = [&](int side) {
    return std::max(1, static_cast<int>((static_cast<int64_t>(side) * pixels + longest / 2) / longest));
  };
  return {scale(width), scale(height)};
}

}

#endif
