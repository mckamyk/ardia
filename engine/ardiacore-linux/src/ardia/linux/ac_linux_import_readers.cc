#include "ardia/linux/ac_linux_import_readers.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <map>
#include <memory>
#include <utility>

#include "ardia/linux/ac_linux_import_crypto.h"
#include "ardia/linux/ac_linux_import_keyring.h"
#include "ardia/linux/ac_linux_import_secrets.h"
#include "ardia/linux/ac_linux_import_sessions.h"
#include "ardia/linux/ac_linux_import_storage.h"
#include "base/json/json_reader.h"
#include "third_party/sqlite/sqlite3.h"

namespace ardia::linux_import {

const unsigned char kChromiumBasicKey[16] = {0xfd, 0x62, 0x1f, 0xe5, 0xa2, 0xb4, 0x02, 0x53,
                                             0x9d, 0xfa, 0x14, 0x7c, 0xa9, 0x27, 0x27, 0x78};

const unsigned char kChromiumEmptyKey[16] = {0xd0, 0xd0, 0xec, 0x9c, 0x7d, 0x77, 0xd4, 0x3a,
                                             0xc5, 0x41, 0x87, 0xfa, 0x48, 0x18, 0xd1, 0x7f};

namespace {

constexpr int64_t kWebKitEpochOffsetMicros = 11644473600000000LL;
constexpr size_t kMaxSmallJsonBytes = 8u * 1024 * 1024;
constexpr size_t kMaxIniBytes = 1024u * 1024;
constexpr int kCancelCheckRows = 128;

constexpr BrowserDefinition kBrowsers[] = {
    {"chrome", "Google Chrome", Family::kChromium, true, "google-chrome", false},
    {"chromeBeta", "Google Chrome Beta", Family::kChromium, true, "google-chrome-beta", false},
    {"chromeDev", "Google Chrome Dev", Family::kChromium, true, "google-chrome-unstable", false},
    {"chromeFlatpak", "Google Chrome (Flatpak)", Family::kChromium, false, ".var/app/com.google.Chrome/config/google-chrome", false},
    {"chromium", "Chromium", Family::kChromium, true, "chromium", false},
    {"chromiumFlatpak", "Chromium (Flatpak)", Family::kChromium, false, ".var/app/org.chromium.Chromium/config/chromium", false},
    {"chromiumSnap", "Chromium (Snap)", Family::kChromium, false, "snap/chromium/common/chromium", false},
    {"brave", "Brave", Family::kChromium, true, "BraveSoftware/Brave-Browser", false},
    {"braveBeta", "Brave Beta", Family::kChromium, true, "BraveSoftware/Brave-Browser-Beta", false},
    {"braveNightly", "Brave Nightly", Family::kChromium, true, "BraveSoftware/Brave-Browser-Nightly", false},
    {"braveFlatpak", "Brave (Flatpak)", Family::kChromium, false, ".var/app/com.brave.Browser/config/BraveSoftware/Brave-Browser", false},
    {"edge", "Microsoft Edge", Family::kChromium, true, "microsoft-edge", false},
    {"edgeBeta", "Microsoft Edge Beta", Family::kChromium, true, "microsoft-edge-beta", false},
    {"edgeDev", "Microsoft Edge Dev", Family::kChromium, true, "microsoft-edge-dev", false},
    {"vivaldi", "Vivaldi", Family::kChromium, true, "vivaldi", false},
    {"vivaldiSnapshot", "Vivaldi Snapshot", Family::kChromium, true, "vivaldi-snapshot", false},
    {"opera", "Opera", Family::kChromium, true, "opera", true},
    {"helium", "Helium", Family::kChromium, true, "net.imput.helium", false},
    {"firefox", "Firefox", Family::kFirefox, false, ".mozilla/firefox", false},
    {"firefoxFlatpak", "Firefox (Flatpak)", Family::kFirefox, false, ".var/app/org.mozilla.firefox/.mozilla/firefox", false},
    {"firefoxSnap", "Firefox (Snap)", Family::kFirefox, false, "snap/firefox/common/.mozilla/firefox", false},
    {"librewolf", "LibreWolf", Family::kFirefox, false, ".librewolf", false},
    {"zen", "Zen", Family::kFirefox, false, ".zen", false},
};

constexpr const char* kKindOrder[] = {"bookmarks", "history", "tabs", "passwords", "cookies",
                                      "autofill", "spaces", "pinnedTabs", "favorites"};

std::string RealPath(const std::string& path) {
  if (path.empty()) return {};
  char* resolved = realpath(path.c_str(), nullptr);
  if (!resolved) return {};
  std::string out = resolved;
  free(resolved);
  return out;
}

std::optional<base::DictValue> ReadJsonDict(const std::string& path, size_t max_bytes) {
  std::string text;
  if (ReadFileLimited(path, max_bytes, &text) != FileRead::kOk) return std::nullopt;
  return base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
}

double FromWebKit(int64_t micros) {
  return micros > kWebKitEpochOffsetMicros ? static_cast<double>(micros - kWebKitEpochOffsetMicros) / 1000 : 0;
}

double FromWebKitString(const std::string* text) {
  int64_t micros = 0;
  return text && ParseInt64(*text, &micros) ? FromWebKit(micros) : 0;
}

double FromUnixMicros(int64_t micros) {
  return micros > 0 ? static_cast<double>(micros) / 1000 : 0;
}

int ClampInt(int64_t value) {
  return static_cast<int>(std::clamp<int64_t>(value, 0, 0x7fffffff));
}

std::string SkColorHex(int color) {
  const uint32_t value = static_cast<uint32_t>(color);
  if (value == 0xFF000000u) return {};
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "#%06x", value & 0xFFFFFFu);
  return buffer;
}

std::string Plural(int count, std::string_view word) {
  return std::to_string(count) + " " + std::string(word) + (count == 1 ? "" : "s");
}

bool IsChromiumProfile(const std::string& directory) {
  for (const char* name : {"Preferences", "Bookmarks", "History", "Login Data"}) {
    if (IsFile(Join(directory, name))) return true;
  }
  return false;
}

base::ListValue ChromiumKinds(const std::string& directory) {
  base::ListValue kinds;
  if (IsFile(Join(directory, "Bookmarks")) || IsFile(Join(directory, "AccountBookmarks"))) kinds.Append("bookmarks");
  if (IsFile(Join(directory, "History"))) kinds.Append("history");
  if (IsDirectory(Join(directory, "Sessions")) || IsDirectory(Join(directory, "Sessions_Encrypted")) ||
      IsFile(Join(directory, "Current Session")) || IsFile(Join(directory, "Last Session"))) {
    kinds.Append("tabs");
  }
  if (IsFile(Join(directory, "Login Data")) || IsFile(Join(directory, "Login Data For Account"))) kinds.Append("passwords");
  if (IsFile(Join(directory, "Network/Cookies")) || IsFile(Join(directory, "Cookies"))) kinds.Append("cookies");
  if (IsFile(Join(directory, "Web Data"))) kinds.Append("autofill");
  return kinds;
}

std::vector<base::DictValue> ChromiumProfiles(const std::string& root, const BrowserDefinition& browser,
                                              std::optional<std::string_view> only) {
  std::vector<base::DictValue> profiles;
  std::optional<base::DictValue> local_state = ReadJsonDict(Join(root, "Local State"), kMaxSmallJsonBytes);
  const base::DictValue* profile_state = local_state ? local_state->FindDict("profile") : nullptr;
  const base::DictValue* info_cache = profile_state ? profile_state->FindDict("info_cache") : nullptr;
  const base::ListValue* order = profile_state ? profile_state->FindList("profiles_order") : nullptr;
  const std::string* last_used = profile_state ? profile_state->FindString("last_used") : nullptr;

  std::vector<std::string> directories;
  auto add = [&directories](const std::string& name) {
    if (ValidProfileId(name) && std::find(directories.begin(), directories.end(), name) == directories.end()) {
      directories.push_back(name);
    }
  };
  if (browser.root_is_profile && IsChromiumProfile(root)) add(".");
  if (order) {
    for (const base::Value& name : *order) {
      if (name.is_string()) add(name.GetString());
    }
  }
  if (info_cache) {
    std::vector<std::string> keys;
    for (const auto [key, value] : *info_cache) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    for (const std::string& key : keys) add(key);
  }
  for (const std::string& name : ListDirectory(root)) {
    if (name == "Default" || name.rfind("Profile ", 0) == 0) add(name);
  }

  int fallback_index = 0;
  for (const std::string& id : directories) {
    const std::string path = Join(root, id);
    if (!IsDirectory(path) || !IsChromiumProfile(path)) continue;
    const base::DictValue* info = info_cache ? info_cache->FindDict(id == "." ? "Default" : id) : nullptr;
    ++fallback_index;
    if (only && *only != id) continue;
    const std::string* stored_name = info ? info->FindString("name") : nullptr;
    std::string name = stored_name && !stored_name->empty() ? *stored_name
                       : (id == "Default" || id == ".") ? "Default"
                                                        : "Profile " + std::to_string(fallback_index);
    base::DictValue profile;
    profile.Set("id", id);
    profile.Set("name", std::move(name));
    profile.Set("path", path);
    if (const std::string* email = info ? info->FindString("user_name") : nullptr; email && !email->empty()) {
      profile.Set("email", *email);
    }
    const std::string picture = Join(path, "Google Profile Picture.png");
    if (IsFile(picture)) profile.Set("avatarPath", picture);
    std::optional<int> color = info ? info->FindInt("profile_highlight_color") : std::nullopt;
    if (!color && info) color = info->FindInt("default_avatar_fill_color");
    if (color) {
      const std::string hex = SkColorHex(*color);
      if (!hex.empty()) profile.Set("color", hex);
    }
    const bool is_default = last_used ? id == *last_used : (id == "Default" || id == ".");
    profile.Set("isDefault", is_default);
    profile.Set("available", ChromiumKinds(path));
    if (ChromiumSessionNeedsKey(path)) profile.Set("tabsNeedKey", true);
    profiles.push_back(std::move(profile));
  }
  return profiles;
}

std::vector<base::DictValue> FirefoxProfiles(const std::string& root) {
  std::vector<base::DictValue> profiles;
  std::string ini;
  if (ReadFileLimited(Join(root, "profiles.ini"), kMaxIniBytes, &ini) != FileRead::kOk) return profiles;
  for (const FirefoxProfileEntry& entry : ParseProfilesIni(ini)) {
    if (!entry.relative || !ValidProfileId(entry.path)) continue;
    const std::string path = Join(root, entry.path);
    if (!IsDirectory(path)) continue;
    base::ListValue kinds;
    if (IsFile(Join(path, "places.sqlite"))) {
      kinds.Append("bookmarks");
      kinds.Append("history");
    }
    if (IsFile(Join(path, "sessionstore.jsonlz4")) || IsFile(Join(path, "sessionstore-backups/recovery.jsonlz4")) ||
        IsFile(Join(path, "sessionstore-backups/previous.jsonlz4"))) {
      kinds.Append("tabs");
    }
    if (IsFile(Join(path, "logins.json")) && IsFile(Join(path, "key4.db"))) kinds.Append("passwords");
    if (IsFile(Join(path, "cookies.sqlite"))) kinds.Append("cookies");
    base::DictValue profile;
    profile.Set("id", entry.path);
    profile.Set("name", entry.name);
    profile.Set("path", path);
    profile.Set("isDefault", entry.is_default);
    profile.Set("available", std::move(kinds));
    profiles.push_back(std::move(profile));
  }
  return profiles;
}

struct RootFolder {
  std::string role;
  std::string title;
  double added = 0;
  base::ListValue children;
};

std::optional<base::DictValue> ChromiumNode(const base::DictValue& json, int depth, size_t* nodes) {
  if (*nodes >= kMaxBookmarkNodes) return std::nullopt;
  const std::string* type = json.FindString("type");
  const std::string* name = json.FindString("name");
  const double added = FromWebKitString(json.FindString("date_added"));
  base::DictValue node;
  if (type && *type == "url") {
    const std::string* url = json.FindString("url");
    if (!url || url->empty()) return std::nullopt;
    node.Set("type", "url");
    node.Set("title", name ? *name : std::string());
    node.Set("url", *url);
  } else if (type && *type == "folder") {
    node.Set("type", "folder");
    node.Set("title", name ? *name : std::string());
    base::ListValue children;
    const base::ListValue* list = json.FindList("children");
    if (list && depth < kMaxBookmarkDepth) {
      for (const base::Value& child : *list) {
        if (!child.is_dict()) continue;
        if (std::optional<base::DictValue> made = ChromiumNode(child.GetDict(), depth + 1, nodes)) {
          children.Append(std::move(*made));
        }
      }
    }
    node.Set("children", std::move(children));
  } else {
    return std::nullopt;
  }
  if (added > 0) node.Set("dateAdded", added);
  ++*nodes;
  return node;
}

std::optional<std::vector<RootFolder>> ChromiumBookmarkRoots(const std::string& path, size_t* nodes) {
  std::optional<base::DictValue> top = ReadJsonDict(path, kMaxJsonBytes);
  const base::DictValue* roots = top ? top->FindDict("roots") : nullptr;
  if (!roots) return std::nullopt;
  std::vector<RootFolder> folders;
  constexpr const char* kRoots[][3] = {{"bookmark_bar", "toolbar", "Bookmarks Bar"},
                                       {"other", "other", "Other Bookmarks"},
                                       {"synced", "mobile", "Mobile Bookmarks"}};
  for (const auto& root : kRoots) {
    const base::DictValue* json = roots->FindDict(root[0]);
    if (!json) continue;
    std::optional<base::DictValue> node = ChromiumNode(*json, 0, nodes);
    if (!node) continue;
    base::ListValue* children = node->FindList("children");
    if (!children || children->empty()) continue;
    RootFolder folder;
    folder.role = root[1];
    const std::string* name = json->FindString("name");
    folder.title = name && !name->empty() ? *name : root[2];
    folder.added = FromWebKitString(json->FindString("date_added"));
    folder.children = std::move(*children);
    folders.push_back(std::move(folder));
  }
  return folders;
}

base::DictValue BookmarksRoot(std::vector<RootFolder> folders) {
  base::ListValue children;
  for (RootFolder& folder : folders) {
    base::DictValue node;
    node.Set("type", "folder");
    node.Set("title", std::move(folder.title));
    node.Set("role", std::move(folder.role));
    if (folder.added > 0) node.Set("dateAdded", folder.added);
    node.Set("children", std::move(folder.children));
    children.Append(std::move(node));
  }
  base::DictValue root;
  root.Set("type", "folder");
  root.Set("title", "Bookmarks");
  root.Set("children", std::move(children));
  return root;
}

std::optional<Failure> ReadChromiumBookmarks(const std::string& directory, base::DictValue* out, int* count) {
  const std::string local = Join(directory, "Bookmarks");
  const std::string account = Join(directory, "AccountBookmarks");
  if (!IsFile(local) && !IsFile(account)) return Failure{"empty", "No bookmarks in this profile"};
  size_t nodes = 0;
  std::vector<RootFolder> folders;
  if (IsFile(local)) {
    std::optional<std::vector<RootFolder>> parsed = ChromiumBookmarkRoots(local, &nodes);
    if (!parsed) return Failure{"unreadable", "The Bookmarks file is not Chromium bookmarks JSON"};
    folders = std::move(*parsed);
  }
  if (IsFile(account)) {
    if (std::optional<std::vector<RootFolder>> extra = ChromiumBookmarkRoots(account, &nodes)) {
      for (RootFolder& folder : *extra) {
        auto same = std::find_if(folders.begin(), folders.end(), [&](const RootFolder& f) { return f.role == folder.role; });
        if (same == folders.end()) {
          folders.push_back(std::move(folder));
        } else {
          for (base::Value& child : folder.children) same->children.Append(std::move(child));
        }
      }
    }
  }
  *out = BookmarksRoot(std::move(folders));
  *count = CountBookmarkLinks(*out);
  return std::nullopt;
}

std::optional<Failure> ReadFirefoxBookmarks(const std::string& directory, ScratchDirectory& scratch,
                                            ImportObserver& observer, base::DictValue* out, int* count,
                                            bool* cancelled) {
  const std::string places = Join(directory, "places.sqlite");
  if (!IsFile(places)) return Failure{"empty", "No bookmarks in this profile"};
  FileRead status = FileRead::kOk;
  std::unique_ptr<Database> db = Database::Snapshot(places, scratch, observer, &status);
  if (!db) return Failure{status == FileRead::kTooLarge ? "tooLarge" : "unreadable", "Couldn't read places.sqlite"};
  if (!db->HasTable("moz_bookmarks")) return Failure{"unreadable", "places.sqlite has no bookmarks"};
  struct Row {
    int64_t id;
    int64_t type;
    std::string title;
    std::string url;
    double added;
    std::string guid;
  };
  std::map<int64_t, std::vector<Row>> children;
  std::optional<int64_t> root_id;
  Statement statement(db->get(),
                      "SELECT b.id, b.type, b.parent, b.title, p.url, b.dateAdded, b.guid FROM moz_bookmarks b "
                      "LEFT JOIN moz_places p ON b.fk = p.id ORDER BY b.parent, b.position");
  int rows = 0;
  while (statement.Step()) {
    if (++rows % kCancelCheckRows == 0 && observer.Cancelled()) {
      *cancelled = true;
      return std::nullopt;
    }
    Row row{statement.Int(0), statement.Int(1), statement.Text(3), statement.Text(4),
            statement.IsNull(5) ? 0 : FromUnixMicros(statement.Int(5)), statement.Text(6)};
    const int64_t parent = statement.Int(2);
    if (row.guid == "root________") root_id = row.id;
    else children[parent].push_back(std::move(row));
  }
  if (!statement.ok() || statement.failed()) return Failure{"unreadable", "Couldn't read places.sqlite bookmarks"};
  if (!root_id) return Failure{"unreadable", "places.sqlite has no bookmarks root"};
  size_t nodes = 0;
  auto build = [&](auto&& self, int64_t parent, int depth) -> base::ListValue {
    base::ListValue list;
    if (depth >= kMaxBookmarkDepth) return list;
    auto found = children.find(parent);
    if (found == children.end()) return list;
    for (const Row& row : found->second) {
      if (nodes >= kMaxBookmarkNodes) break;
      base::DictValue node;
      if (row.type == 1) {
        if (row.url.empty() || row.url.rfind("place:", 0) == 0) continue;
        node.Set("type", "url");
        node.Set("title", row.title);
        node.Set("url", row.url);
      } else if (row.type == 2) {
        node.Set("type", "folder");
        node.Set("title", row.title);
        node.Set("children", self(self, row.id, depth + 1));
      } else {
        continue;
      }
      if (row.added > 0) node.Set("dateAdded", row.added);
      ++nodes;
      list.Append(std::move(node));
    }
    return list;
  };
  constexpr const char* kRoots[][3] = {{"toolbar_____", "toolbar", "Bookmarks Toolbar"},
                                       {"menu________", "menu", "Bookmarks Menu"},
                                       {"unfiled_____", "other", "Other Bookmarks"},
                                       {"mobile______", "mobile", "Mobile Bookmarks"}};
  std::vector<RootFolder> folders;
  for (const auto& root : kRoots) {
    const auto& top = children[*root_id];
    auto row = std::find_if(top.begin(), top.end(), [&](const Row& r) { return r.guid == root[0]; });
    if (row == top.end()) continue;
    base::ListValue kids = build(build, row->id, 0);
    if (kids.empty()) continue;
    RootFolder folder;
    folder.role = root[1];
    folder.title = root[2];
    folder.added = row->added;
    folder.children = std::move(kids);
    folders.push_back(std::move(folder));
  }
  *out = BookmarksRoot(std::move(folders));
  *count = CountBookmarkLinks(*out);
  return std::nullopt;
}

std::optional<Failure> ReadHistory(const std::string& file, bool firefox, const ImportOptions& options,
                                   ScratchDirectory& scratch, ImportObserver& observer, base::ListValue* entries,
                                   int* count, bool* cancelled) {
  if (!IsFile(file)) return Failure{"empty", "No history in this profile"};
  FileRead status = FileRead::kOk;
  std::unique_ptr<Database> db = Database::Snapshot(file, scratch, observer, &status);
  if (!db) return Failure{status == FileRead::kTooLarge ? "tooLarge" : "unreadable", "Couldn't read the history database"};
  const char* table = firefox ? "moz_places" : "urls";
  if (!db->HasTable(table)) return Failure{"unreadable", "The history database has no history table"};
  const std::string web = "(url LIKE 'http://%' OR url LIKE 'https://%')";
  std::string where = firefox ? "hidden = 0 AND visit_count > 0 AND last_visit_date IS NOT NULL AND " + web
                              : "hidden = 0 AND last_visit_time > 0 AND " + web;
  std::optional<int64_t> since;
  if (options.history_since && *options.history_since > 0) {
    const int64_t micros = static_cast<int64_t>(*options.history_since * 1000);
    since = firefox ? micros : micros + kWebKitEpochOffsetMicros;
    where += firefox ? " AND last_visit_date >= ?1" : " AND last_visit_time >= ?1";
  }
  const std::string sql = firefox
      ? "SELECT url, title, visit_count, last_visit_date, typed FROM moz_places WHERE " + where + " ORDER BY last_visit_date DESC"
      : "SELECT url, title, visit_count, last_visit_time, typed_count FROM urls WHERE " + where + " ORDER BY last_visit_time DESC";
  Statement total_statement(db->get(), "SELECT count(*) FROM (" + sql + ")");
  if (since) total_statement.Bind(1, *since);
  if (!total_statement.Step()) return Failure{"unreadable", "Couldn't read the history database"};
  const int total = ClampInt(total_statement.Int(0));
  const std::optional<int> limit =
      options.history_limit && *options.history_limit > 0 ? options.history_limit : std::nullopt;
  const int expected = limit ? std::min(*limit, total) : total;
  observer.Progress("history", "start", 0, expected);
  Statement statement(db->get(), sql);
  if (since) statement.Bind(1, *since);
  int read = 0;
  while ((!limit || read < *limit) && statement.Step()) {
    if (read % kCancelCheckRows == 0 && observer.Cancelled()) {
      *cancelled = true;
      return std::nullopt;
    }
    std::string url = statement.Text(0);
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) continue;
    base::DictValue entry;
    entry.Set("url", std::move(url));
    entry.Set("title", statement.Text(1));
    entry.Set("visits", std::max(1, ClampInt(statement.Int(2))));
    const int64_t time = statement.Int(3);
    entry.Set("lastVisit", firefox ? FromUnixMicros(time) : FromWebKit(time));
    if (!statement.IsNull(4)) entry.Set("typedCount", ClampInt(statement.Int(4)));
    entries->Append(std::move(entry));
    ++read;
    if (read % kProgressStep == 0) observer.Progress("history", "progress", read, expected);
  }
  if (!statement.ok() || statement.failed()) return Failure{"unreadable", "Couldn't read the history database"};
  *count = read;
  observer.Progress("history", "end", read, read);
  return std::nullopt;
}

struct LoginsRead {
  std::vector<Credential> credentials;
  int undecryptable = 0;
  int keyring = 0;
  int portal = 0;
};

std::optional<Failure> ReadChromiumLogins(const std::string& directory, ScratchDirectory& scratch,
                                          ImportObserver& observer, const SecretBytes* key, LoginsRead* out,
                                          bool* cancelled) {
  bool found = false;
  for (const char* name : {"Login Data", "Login Data For Account"}) {
    const std::string file = Join(directory, name);
    if (!IsFile(file)) continue;
    found = true;
    FileRead status = FileRead::kOk;
    std::unique_ptr<Database> db = Database::Snapshot(file, scratch, observer, &status);
    if (!db) return Failure{status == FileRead::kTooLarge ? "tooLarge" : "unreadable", "Couldn't read the saved passwords"};
    if (!db->HasTable("logins")) return Failure{"unreadable", "Login Data has no logins table"};
    Statement statement(db->get(), "SELECT * FROM logins");
    const int origin = statement.Column("origin_url");
    const int realm = statement.Column("signon_realm");
    const int username = statement.Column("username_value");
    const int password = statement.Column("password_value");
    const int blocked = statement.Column("blacklisted_by_user");
    const int created = statement.Column("date_created");
    const int last_used = statement.Column("date_last_used");
    const int times_used = statement.Column("times_used");
    if (password < 0) return Failure{"unreadable", "Login Data has no password column"};
    int rows = 0;
    while (statement.Step()) {
      if (++rows % kCancelCheckRows == 0 && observer.Cancelled()) {
        *cancelled = true;
        return std::nullopt;
      }
      if (statement.Int(blocked) != 0) continue;
      std::string blob = statement.Blob(password);
      if (blob.empty()) continue;
      Credential credential;
      const ValueDecrypt decrypted = DecryptChromiumValue(blob, key, std::string_view(), &credential.password);
      Wipe(blob);
      if (decrypted == ValueDecrypt::kKeyring) {
        ++out->keyring;
        continue;
      }
      if (decrypted == ValueDecrypt::kPortal) {
        ++out->portal;
        continue;
      }
      if (decrypted == ValueDecrypt::kFailed) {
        ++out->undecryptable;
        continue;
      }
      if (credential.password.empty()) continue;
      const std::string origin_url = statement.Text(origin);
      credential.realm = statement.Text(realm);
      credential.url = origin_url.empty() ? credential.realm : origin_url;
      credential.username = statement.Text(username);
      credential.created = FromWebKit(statement.Int(created));
      credential.last_used = FromWebKit(statement.Int(last_used));
      credential.times_used = ClampInt(statement.Int(times_used));
      if (credential.url.empty()) continue;
      if (out->credentials.size() >= kMaxCredentials) break;
      out->credentials.push_back(std::move(credential));
    }
    if (!statement.ok() || statement.failed()) return Failure{"unreadable", "Couldn't read the saved passwords"};
  }
  if (!found) return Failure{"empty", "No saved passwords in this profile"};
  RemoveDuplicateCredentials(out->credentials, false);
  return std::nullopt;
}

std::string ExtensionName(const std::string& profile, const base::DictValue& entry) {
  std::optional<base::DictValue> loaded;
  const base::DictValue* manifest = entry.FindDict("manifest");
  std::string folder;
  const std::string* relative = entry.FindString("path");
  if (relative && ValidProfileId(*relative)) {
    folder = Join(Join(profile, "Extensions"), *relative);
    if (!manifest) {
      loaded = ReadJsonDict(Join(folder, "manifest.json"), kMaxIniBytes);
      if (loaded) manifest = &*loaded;
    }
  }
  const std::string* raw = manifest ? manifest->FindString("name") : nullptr;
  if (!raw) return {};
  std::string name(*raw);
  while (!name.empty() && name.front() == ' ') name.erase(name.begin());
  while (!name.empty() && name.back() == ' ') name.pop_back();
  if (name.size() < 8 || name.rfind("__MSG_", 0) != 0 || name.substr(name.size() - 2) != "__") return name;
  std::string key = name.substr(6, name.size() - 8);
  std::transform(key.begin(), key.end(), key.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
  const std::string* locale = manifest->FindString("default_locale");
  if (folder.empty() || !locale || !ValidProfileId(*locale) || locale->find('/') != std::string::npos) return {};
  std::optional<base::DictValue> messages = ReadJsonDict(Join(Join(Join(folder, "_locales"), *locale), "messages.json"), kMaxIniBytes);
  if (!messages) return {};
  for (const auto [message_key, value] : *messages) {
    std::string lowered = message_key;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
    if (lowered != key || !value.is_dict()) continue;
    const std::string* message = value.GetDict().FindString("message");
    return message ? *message : std::string();
  }
  return {};
}

bool IsExtensionId(std::string_view id) {
  return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) { return c >= 'a' && c <= 'p'; });
}

std::string LowerName(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
  return text;
}

}

const BrowserDefinition* FindBrowser(std::string_view id) {
  for (const BrowserDefinition& browser : kBrowsers) {
    if (id == browser.id) return &browser;
  }
  return nullptr;
}

std::vector<const BrowserDefinition*> AllBrowsers() {
  std::vector<const BrowserDefinition*> browsers;
  for (const BrowserDefinition& browser : kBrowsers) browsers.push_back(&browser);
  return browsers;
}

Environment MakeEnvironment(const char* home, const char* config_home, const char* temp_root,
                            std::string own_data_directory) {
  Environment environment;
  if (home && home[0] == '/') environment.home = home;
  if (config_home && config_home[0] == '/') environment.config_home = config_home;
  else if (!environment.home.empty()) environment.config_home = Join(environment.home, ".config");
  environment.temp_root = temp_root && temp_root[0] == '/' ? temp_root : "/tmp";
  environment.own_data_directory = std::move(own_data_directory);
  return environment;
}

std::string DataDirectory(const Environment& environment, const BrowserDefinition& browser) {
  const std::string& base = browser.under_config ? environment.config_home : environment.home;
  return base.empty() ? std::string() : Join(base, browser.path);
}

std::optional<std::string> ProfileDirectory(const Environment& environment, const BrowserDefinition& browser,
                                            std::string_view profile_id) {
  if (!ValidProfileId(profile_id)) return std::nullopt;
  const std::string root = DataDirectory(environment, browser);
  if (root.empty()) return std::nullopt;
  const std::string directory = Join(root, profile_id);
  if (!IsDirectory(directory)) return std::nullopt;
  return directory;
}

base::ListValue ListBrowsers(const Environment& environment, Keyring* keyring) {
  base::ListValue sources;
  const std::string own = RealPath(environment.own_data_directory);
  for (const BrowserDefinition& browser : kBrowsers) {
    const std::string root = DataDirectory(environment, browser);
    if (!IsDirectory(root)) continue;
    if (!own.empty() && RealPath(root) == own) continue;
    std::vector<base::DictValue> profiles = browser.family == Family::kChromium
        ? ChromiumProfiles(root, browser, std::nullopt)
        : FirefoxProfiles(root);
    if (profiles.empty()) continue;
    base::ListValue list;
    for (base::DictValue& profile : profiles) list.Append(std::move(profile));
    base::DictValue source;
    source.Set("id", browser.id);
    source.Set("name", browser.name);
    source.Set("family", browser.family == Family::kChromium ? "chromium" : "firefox");
    source.Set("requiresExport", false);
    bool keychain = false;
    if (browser.family == Family::kChromium && keyring) {
      if (std::optional<SafeStorageItem> item = SafeStorageFor(browser.id)) {
        keychain = keyring->Exists(*item, kKeyringProbeTimeoutMs) == KeyringStatus::kOk;
        if (keychain) source.Set("keychainService", item->kwallet_key);
      }
    }
    source.Set("needsKeychain", keychain);
    source.Set("needsFullDiskAccess", false);
    source.Set("profiles", std::move(list));
    sources.Append(std::move(source));
  }
  return sources;
}

int RemoveStaleScratch(const std::string& temp_root, int64_t now, int64_t max_age_seconds) {
  int removed = 0;
  for (const std::string& name : ListDirectory(temp_root)) {
    if (name.rfind("ardia-import-", 0) != 0) continue;
    const std::string path = Join(temp_root, name);
    struct stat info;
    if (lstat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode) || info.st_uid != geteuid()) continue;
    if (static_cast<int64_t>(info.st_mtime) > now - max_age_seconds) continue;
    for (const std::string& entry : ListDirectory(path)) unlink(Join(path, entry).c_str());
    if (rmdir(path.c_str()) == 0) ++removed;
  }
  return removed;
}

FileRead ReadFileLimited(const std::string& path, size_t max_bytes, std::string* contents) {
  contents->clear();
  const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) return errno == ENOENT ? FileRead::kMissing : FileRead::kUnreadable;
  struct stat info;
  if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
    close(fd);
    return FileRead::kUnreadable;
  }
  if (static_cast<uint64_t>(info.st_size) > max_bytes) {
    close(fd);
    return FileRead::kTooLarge;
  }
  contents->reserve(static_cast<size_t>(info.st_size));
  std::vector<char> buffer(64 * 1024);
  while (true) {
    const ssize_t got = read(fd, buffer.data(), buffer.size());
    if (got < 0 && errno == EINTR) continue;
    if (got < 0) {
      close(fd);
      Wipe(*contents);
      return FileRead::kUnreadable;
    }
    if (got == 0) break;
    if (contents->size() + static_cast<size_t>(got) > max_bytes) {
      close(fd);
      Wipe(*contents);
      return FileRead::kTooLarge;
    }
    contents->append(buffer.data(), static_cast<size_t>(got));
  }
  close(fd);
  return FileRead::kOk;
}

Decrypted DecryptChromiumPassword(std::string_view blob, std::string* password) {
  switch (DecryptChromiumValue(blob, nullptr, std::string_view(), password)) {
    case ValueDecrypt::kOk:
      return Decrypted::kOk;
    case ValueDecrypt::kKeyring:
    case ValueDecrypt::kPortal:
      return Decrypted::kKeyring;
    case ValueDecrypt::kFailed:
      return Decrypted::kFailed;
  }
  return Decrypted::kFailed;
}

ImportOutcome::ImportOutcome() = default;
ImportOutcome::ImportOutcome(ImportOutcome&&) = default;
ImportOutcome& ImportOutcome::operator=(ImportOutcome&&) = default;
ImportOutcome::~ImportOutcome() = default;

ImportOutcome ReadBrowserProfile(const Environment& environment, std::string_view browser_id,
                                 std::string_view profile_id, const std::vector<std::string>& kinds,
                                 const ImportOptions& options, ImportObserver& observer) {
  ImportOutcome outcome;
  const BrowserDefinition* browser = FindBrowser(browser_id);
  if (!browser) {
    outcome.error_code = "notFound";
    outcome.error = "Unknown browser";
    return outcome;
  }
  std::optional<std::string> directory = ProfileDirectory(environment, *browser, profile_id);
  const std::string own = RealPath(environment.own_data_directory);
  if (!directory || (!own.empty() && RealPath(DataDirectory(environment, *browser)) == own)) {
    outcome.error_code = "notFound";
    outcome.error = "Profile not found";
    return outcome;
  }
  const bool chromium = browser->family == Family::kChromium;
  const SecretBytes* chromium_key = options.chromium_key ? &*options.chromium_key : nullptr;
  ScratchDirectory scratch(environment.temp_root);
  base::DictValue result;
  result.Set("browserId", browser->id);
  result.Set("profileId", profile_id);
  if (chromium) {
    std::vector<base::DictValue> profiles = ChromiumProfiles(DataDirectory(environment, *browser), *browser, profile_id);
    if (!profiles.empty()) {
      base::DictValue suggestion;
      if (const std::string* name = profiles[0].FindString("name")) suggestion.Set("name", *name);
      if (const std::string* color = profiles[0].FindString("color")) suggestion.Set("color", *color);
      if (const std::string* avatar = profiles[0].FindString("avatarPath")) suggestion.Set("avatarPath", *avatar);
      result.Set("profile", std::move(suggestion));
    }
  }
  base::ListValue history;
  int history_count = 0;
  base::ListValue failed;
  base::ListValue warnings;
  auto warn = [&warnings](std::string_view kind, std::string_view code, std::string message) {
    base::DictValue warning;
    warning.Set("kind", kind);
    warning.Set("code", code);
    warning.Set("message", std::move(message));
    warnings.Append(std::move(warning));
  };

  for (const char* kind : kKindOrder) {
    if (std::find(kinds.begin(), kinds.end(), kind) == kinds.end()) continue;
    if (observer.Cancelled()) {
      outcome.cancelled = true;
      return outcome;
    }
    const std::string_view name = kind;
    if (name != "history") observer.Progress(name, "start", 0, std::nullopt);
    int count = 0;
    bool cancelled = false;
    bool succeeded = false;
    std::optional<Failure> failure;
    if (name == "bookmarks") {
      base::DictValue root;
      failure = chromium ? ReadChromiumBookmarks(*directory, &root, &count)
                         : ReadFirefoxBookmarks(*directory, scratch, observer, &root, &count, &cancelled);
      if (!failure && !cancelled) result.Set("bookmarks", std::move(root));
    } else if (name == "history") {
      failure = ReadHistory(Join(*directory, chromium ? "History" : "places.sqlite"), !chromium, options, scratch,
                            observer, &history, &count, &cancelled);
      history_count = count;
    } else if (name == "passwords" && chromium) {
      LoginsRead logins;
      failure = ReadChromiumLogins(*directory, scratch, observer, chromium_key, &logins, &cancelled);
      if (!failure && !cancelled) {
        count = static_cast<int>(logins.credentials.size());
        const int skipped = logins.keyring + logins.portal + logins.undecryptable;
        if (logins.keyring > 0) {
          warn(name, "locked",
               Plural(logins.keyring, "password") + " protected by the system keyring " +
                   (logins.keyring == 1 ? "wasn't" : "weren't") + " imported");
        }
        if (logins.portal > 0) {
          warn(name, "undecryptable",
               Plural(logins.portal, "password") + " protected by the desktop portal " +
                   (logins.portal == 1 ? "wasn't" : "weren't") + " imported");
        }
        if (logins.undecryptable > 0) {
          warn(name, "undecryptable", Plural(logins.undecryptable, "password") + " couldn't be decrypted");
        }
        if (logins.credentials.empty() && skipped > 0) {
          failure = Failure{"locked", "None of " + std::string(browser->name) + "'s saved passwords could be decrypted"};
        }
        outcome.credentials = std::move(logins.credentials);
      }
    } else if (name == "passwords") {
      FirefoxLoginsRead logins;
      const std::string_view primary =
          options.firefox_primary_password ? std::string_view(options.firefox_primary_password->value()) : std::string_view();
      failure = ReadFirefoxLogins(*directory, scratch, observer, primary, &logins, &cancelled);
      if (!failure && !cancelled) {
        count = static_cast<int>(logins.credentials.size());
        if (logins.undecryptable > 0) {
          warn(name, "undecryptable", Plural(logins.undecryptable, "password") + " couldn't be decrypted");
        }
        outcome.credentials = std::move(logins.credentials);
      }
    } else if (name == "tabs") {
      OpenTabs tabs;
      failure = chromium ? ReadChromiumTabs(*directory, chromium_key, &tabs) : ReadFirefoxTabs(*directory, &tabs);
      if (!failure) {
        count = tabs.count;
        result.Set("tabs", std::move(tabs.tabs));
        result.Set("tabGroups", std::move(tabs.groups));
      }
    } else if (name == "cookies") {
      CookieRead cookies;
      failure = chromium ? ReadChromiumCookies(*directory, scratch, observer, chromium_key, options.now_ms, &cookies, &cancelled)
                         : ReadFirefoxCookies(*directory, scratch, observer, options.now_ms, &cookies, &cancelled);
      if (!failure && !cancelled) {
        count = static_cast<int>(cookies.cookies.size());
        if (cookies.keyring > 0) {
          warn(name, "locked", "Unlock " + std::string(browser->name) + " to import " + Plural(cookies.keyring, "cookie"));
        }
        if (cookies.portal > 0) {
          warn(name, "undecryptable", Plural(cookies.portal, "cookie") + " protected by the desktop portal " +
                                          (cookies.portal == 1 ? "wasn't" : "weren't") + " imported");
        }
        if (cookies.undecryptable > 0) {
          warn(name, "undecryptable", Plural(cookies.undecryptable, "cookie") + " couldn't be decrypted");
        }
        if (cookies.skipped > 0) {
          warn(name, "skipped", Plural(cookies.skipped, "cookie") + " from containers or private windows " +
                                    (cookies.skipped == 1 ? "wasn't" : "weren't") + " imported");
        }
        if (cookies.cookies.empty() && cookies.keyring + cookies.portal + cookies.undecryptable > 0) {
          failure = Failure{"locked", "None of " + std::string(browser->name) + "'s cookies could be decrypted"};
        }
        outcome.cookies = std::move(cookies.cookies);
      }
    } else if (name == "autofill" && chromium) {
      AutofillRead autofill;
      failure = ReadChromiumAutofill(*directory, scratch, observer, chromium_key, &autofill, &cancelled);
      if (!failure && !cancelled) {
        count = static_cast<int>(autofill.addresses.size() + autofill.cards.size());
        if (autofill.locked_cards > 0) {
          warn(name, "locked", "Unlock " + std::string(browser->name) + " to import " + Plural(autofill.locked_cards, "saved card"));
        }
        if (autofill.portal > 0) {
          warn(name, "undecryptable", Plural(autofill.portal, "card") + " protected by the desktop portal " +
                                          (autofill.portal == 1 ? "wasn't" : "weren't") + " imported");
        }
        if (autofill.undecryptable > 0) {
          warn(name, "undecryptable", Plural(autofill.undecryptable, "card") + " couldn't be decrypted");
        }
        outcome.addresses = std::move(autofill.addresses);
        outcome.cards = std::move(autofill.cards);
      }
    } else {
      failure = Failure{"unsupported", std::string(browser->name) + " has no " + std::string(name) + " to import"};
    }
    if (cancelled || observer.Cancelled()) {
      outcome.cancelled = true;
      outcome.credentials.clear();
      outcome.cookies.clear();
      outcome.addresses.clear();
      outcome.cards.clear();
      return outcome;
    }
    if (failure && failure->code == "empty") {
      warn(name, "empty", failure->message);
    } else if (failure) {
      failed.Append(name);
      warn(name, failure->code, failure->message);
    } else {
      succeeded = true;
    }
    if (name != "history" || !succeeded) observer.Progress(name, "end", count, count);
  }

  result.Set("history", std::move(history));
  result.Set("historyCount", history_count);
  if (!result.FindList("tabs")) result.Set("tabs", base::ListValue());
  if (!result.FindList("tabGroups")) result.Set("tabGroups", base::ListValue());
  result.Set("credentials", base::ListValue());
  result.Set("cookieCount", static_cast<int>(outcome.cookies.size()));
  result.Set("addressCount", static_cast<int>(outcome.addresses.size()));
  result.Set("cardCount", static_cast<int>(outcome.cards.size()));
  result.Set("spaces", base::ListValue());
  result.Set("favorites", base::ListValue());
  result.Set("favicons", base::ListValue());
  result.Set("failed", std::move(failed));
  result.Set("warnings", std::move(warnings));
  outcome.result = std::move(result);
  return outcome;
}

base::ListValue ListExtensions(const Environment& environment, std::string_view browser_id,
                               std::string_view profile_id) {
  base::ListValue out;
  const BrowserDefinition* browser = FindBrowser(browser_id);
  if (!browser || browser->family != Family::kChromium) return out;
  std::optional<std::string> directory = ProfileDirectory(environment, *browser, profile_id);
  if (!directory) return out;
  std::map<std::string, base::DictValue> settings;
  for (const char* file : {"Preferences", "Secure Preferences"}) {
    std::optional<base::DictValue> root = ReadJsonDict(Join(*directory, file), kMaxJsonBytes);
    const base::DictValue* extensions = root ? root->FindDict("extensions") : nullptr;
    const base::DictValue* found = extensions ? extensions->FindDict("settings") : nullptr;
    if (!found) continue;
    for (const auto [id, value] : *found) {
      if (!value.is_dict()) continue;
      base::DictValue& merged = settings[id];
      for (const auto [key, field] : value.GetDict()) merged.Set(key, field.Clone());
    }
  }
  std::vector<std::pair<std::string, std::string>> listed;
  for (const auto& [id, entry] : settings) {
    if (!IsExtensionId(id) || id == "cjpalhdlnbpafiamejdnhcphjbkeiagm" || id == "ddkjiahejlhfcafbddmgiahcphecmpfh") continue;
    if (entry.FindBool("from_webstore") != true) continue;
    if (std::optional<int> location = entry.FindInt("location"); location && *location != 1) continue;
    std::string name = ExtensionName(*directory, entry);
    listed.emplace_back(name.empty() ? id : std::move(name), id);
  }
  std::sort(listed.begin(), listed.end(), [](const auto& a, const auto& b) { return LowerName(a.first) < LowerName(b.first); });
  for (auto& [name, id] : listed) {
    base::DictValue extension;
    extension.Set("id", id);
    extension.Set("name", std::move(name));
    extension.Set("webStoreUrl", "https://chromewebstore.google.com/detail/" + id);
    out.Append(std::move(extension));
  }
  return out;
}

}
