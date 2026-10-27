#include "ardia/linux/ac_linux_profiles.h"

namespace ardiacore::linux_profiles {
namespace {

bool AsciiLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool AsciiDigit(char c) {
  return c >= '0' && c <= '9';
}

bool ValidToken(std::string_view value) {
  if (value.empty() || value.size() > kMaxProfileTokenLength || !AsciiLetter(value.front())) return false;
  for (const char c : value) {
    if (!AsciiLetter(c) && !AsciiDigit(c) && c != '-' && c != '_') return false;
  }
  return true;
}

bool DefaultAlias(std::string_view value) {
  if (value.size() != 7) return false;
  constexpr std::string_view kDefault = "default";
  for (size_t i = 0; i < value.size(); ++i) {
    const char c = value[i] >= 'A' && value[i] <= 'Z' ? static_cast<char>(value[i] - 'A' + 'a') : value[i];
    if (c != kDefault[i]) return false;
  }
  return true;
}

std::optional<ProfileName> ParseRegular(std::string_view value) {
  if (value.empty() || value == "default") return ProfileName{ProfileKind::kDefault, "", "", ""};
  if (DefaultAlias(value) || !ValidToken(value)) return std::nullopt;
  return ProfileName{ProfileKind::kIsolated, std::string(value), std::string(value), ""};
}

}

std::optional<ProfileName> ParseProfileName(std::string_view value) {
  if (!value.starts_with(kPrivatePrefix)) return ParseRegular(value);
  const std::string_view rest = value.substr(kPrivatePrefix.size());
  const size_t at = rest.find('@');
  if (at == std::string_view::npos) return std::nullopt;
  const std::string_view window = rest.substr(0, at);
  const auto original = ParseRegular(rest.substr(at + 1));
  if (!ValidToken(window) || !original) return std::nullopt;
  ProfileName name;
  name.kind = ProfileKind::kPrivate;
  name.window = std::string(window);
  name.original = original->id;
  name.id = std::string(kPrivatePrefix) + name.window + "@" + name.original;
  return name;
}

std::string ProfileDirectoryName(std::string_view isolated_id) {
  return isolated_id.empty() ? std::string() : "Profile " + std::string(isolated_id);
}

std::string_view ProfileErrorCode(ProfileError error) {
  switch (error) {
    case ProfileError::kNone: return "";
    case ProfileError::kInvalid: return "ERR_PROFILE_INVALID";
    case ProfileError::kDeleted: return "ERR_PROFILE_DELETED";
    case ProfileError::kReleased: return "ERR_PROFILE_RELEASED";
    case ProfileError::kUnavailable: return "ERR_PROFILE_UNAVAILABLE";
    case ProfileError::kWindowMismatch: return "ERR_PROFILE_WINDOW";
    case ProfileError::kPrivateService: return "ERR_PROFILE_PRIVATE";
  }
  return "ERR_PROFILE_UNAVAILABLE";
}

std::string_view ProfileErrorMessage(ProfileError error) {
  switch (error) {
    case ProfileError::kNone: return "";
    case ProfileError::kInvalid: return "The profile identity is not valid on Linux";
    case ProfileError::kDeleted: return "This profile was deleted";
    case ProfileError::kReleased: return "This private session has ended";
    case ProfileError::kUnavailable: return "Chrome could not open this profile";
    case ProfileError::kWindowMismatch: return "This window hosts a different profile";
    case ProfileError::kPrivateService: return "This service is unavailable in private windows";
  }
  return "Chrome could not open this profile";
}

bool ProfileOpensWindow(const ProfileName& name, bool incognito, bool utility) {
  if (utility) return name.kind == ProfileKind::kDefault && !incognito;
  return incognito == (name.kind == ProfileKind::kPrivate);
}

}
