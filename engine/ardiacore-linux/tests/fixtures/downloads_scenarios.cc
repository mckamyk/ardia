#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ardia/linux/ac_linux_downloads.h"
#include "ardia/linux/ac_linux_file_drop.h"

using namespace ardia::linux_downloads;

namespace {

base::DictValue Dict(std::initializer_list<std::pair<const char*, base::Value>> entries) {
  base::DictValue dict;
  for (const auto& [key, value] : entries) dict.Set(key, value.Clone());
  return dict;
}

void Touch(const std::string& path, const std::string& text = "data") {
  std::ofstream(path) << text;
}

std::string Read(const std::string& path) {
  std::ifstream input(path);
  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool Exists(const std::string& path) {
  struct stat info;
  return lstat(path.c_str(), &info) == 0;
}

void Ids() {
  assert(ValidDownloadId("3f2504e0-4f89-41d3-9a0c-0305e82c3301"));
  assert(ValidDownloadId("3F2504E0-4F89-41D3-9A0C-0305E82C3301"));
  assert(!ValidDownloadId(""));
  assert(!ValidDownloadId("3f2504e0-4f89-41d3-9a0c-0305e82c330"));
  assert(!ValidDownloadId("3f2504e0-4f89-41d3-9a0c-0305e82c33011"));
  assert(!ValidDownloadId("3f2504e04-f89-41d3-9a0c-0305e82c3301"));
  assert(!ValidDownloadId("3f2504e0-4f89-41d3-9a0c-0305e82c330g"));
  assert(!ValidDownloadId("saved-0"));
  assert(!ValidDownloadId("../../../../etc/passwd-aaaa-bbbb-cccc"));
  assert(!ValidDownloadId(std::string("3f2504e0-4f89-41d3-9a0c-0305e82c330\0", 36)));
}

void Paths() {
  assert(ValidLocalPath("/home/me/Downloads/report.pdf"));
  assert(ValidLocalPath("/"));
  assert(!ValidLocalPath(""));
  assert(!ValidLocalPath("relative/file"));
  assert(!ValidLocalPath("/home/me/../root"));
  assert(!ValidLocalPath("/home/./me"));
  assert(!ValidLocalPath("/home//me"));
  assert(!ValidLocalPath("/home/me/"));
  assert(!ValidLocalPath(std::string("/home/me\0/x", 11)));
  assert(!ValidLocalPath("/" + std::string(kMaxPathBytes, 'a')));
  assert(ValidLocalPath("/home/me/..hidden"));
  assert(BaseName("/home/me/a b.txt") == "a b.txt");
  assert(FileUrl("/tmp/a b#?%.txt") == "file:///tmp/a%20b%23%3F%25.txt");
  assert(FileUrl("/tmp/\xc3\xa9t\xc3\xa9") == "file:///tmp/%C3%A9t%C3%A9");
  assert(PathListed("/d/a.txt", {"/d/b.txt", "/d/a.txt"}));
  assert(!PathListed("/d/c.txt", {"/d/b.txt", "/d/a.txt"}));
  assert(!PathListed("/d/../d/a.txt", {"/d/../d/a.txt"}));
  assert(!PathListed("", {""}));
}

void Requests() {
  const std::string id = "3f2504e0-4f89-41d3-9a0c-0305e82c3301";
  for (const char* verb : {"downloads.cancel", "downloads.pause", "downloads.resume", "downloads.keep", "downloads.remove"}) {
    assert(ValidateRequest(verb, Dict({{"id", base::Value(id)}})).empty());
    assert(!ValidateRequest(verb, Dict({})).empty());
    assert(!ValidateRequest(verb, Dict({{"id", base::Value("saved-3")}})).empty());
    assert(!ValidateRequest(verb, Dict({{"id", base::Value(7)}})).empty());
    assert(!ValidateRequest(verb, Dict({{"id", base::Value(id)}, {"path", base::Value("/etc/passwd")}})).empty());
  }
  assert(ValidateRequest("downloads.list", Dict({})).empty());
  assert(!ValidateRequest("downloads.list", Dict({{"all", base::Value(true)}})).empty());
  assert(ValidateRequest("downloads.settings", Dict({})).empty());
  assert(ValidateRequest("downloads.setSettings", Dict({{"folder", base::Value("/home/me/Downloads")}})).empty());
  assert(ValidateRequest("downloads.setSettings", Dict({{"askWhereToSave", base::Value(false)}})).empty());
  assert(ValidateRequest("downloads.setSettings", Dict({})).empty());
  assert(!ValidateRequest("downloads.setSettings", Dict({{"folder", base::Value("Downloads")}})).empty());
  assert(!ValidateRequest("downloads.setSettings", Dict({{"folder", base::Value("/home/me/../../etc")}})).empty());
  assert(!ValidateRequest("downloads.setSettings", Dict({{"askWhereToSave", base::Value("yes")}})).empty());
  assert(!ValidateRequest("downloads.setSettings", Dict({{"managed", base::Value(false)}})).empty());
  assert(ValidateRequest("history.query", Dict({{"anything", base::Value(1)}})).empty());
}

base::Value Entry(const std::string& id, std::optional<bool> off_the_record) {
  base::DictValue entry = Dict({{"id", base::Value(id)}, {"path", base::Value("/d/" + id)}});
  if (off_the_record) entry.Set("offTheRecord", base::Value(*off_the_record));
  return base::Value(std::move(entry));
}

void Snapshots() {
  const std::string a = "3f2504e0-4f89-41d3-9a0c-0305e82c3301";
  const std::string b = "3f2504e0-4f89-41d3-9a0c-0305e82c3302";
  const std::string c = "3f2504e0-4f89-41d3-9a0c-0305e82c3303";
  base::ListValue list;
  list.Append(Entry(a, std::nullopt));
  list.Append(Entry(b, true));
  list.Append(Entry(c, false));
  list.Append(Entry("saved-1", false));
  list.Append(base::Value("not an entry"));
  base::DictValue reply;
  reply.Set("downloads", base::Value(std::move(list)));
  const auto filtered = FilterSnapshot(base::Value(std::move(reply)));
  assert(filtered && filtered->size() == 2);
  assert(*(*filtered)[0].GetDict().FindString("id") == a);
  assert(*(*filtered)[1].GetDict().FindString("id") == c);
  assert(!FilterSnapshot(base::Value("x")));
  assert(!FilterSnapshot(base::Value(Dict({{"downloads", base::Value(7)}}))));
  base::ListValue mixed;
  mixed.Append(Entry(a, std::nullopt));
  mixed.Append(Entry(b, true));
  mixed.Append(Entry(c, false));
  base::DictValue mixed_reply;
  mixed_reply.Set("downloads", base::Value(std::move(mixed)));
  const base::Value both(std::move(mixed_reply));
  const auto regular = FilterSnapshot(both, false);
  assert(regular && regular->size() == 2);
  const auto off_the_record = FilterSnapshot(both, true);
  assert(off_the_record && off_the_record->size() == 1 && *(*off_the_record)[0].GetDict().FindString("id") == b);
}

void Routes() {
  using ardiacore::linux_profiles::ProfileError;
  using ardiacore::linux_profiles::ProfileKind;
  auto route = [](ProfileKind kind, bool page_private, bool ending, bool hosts) {
    return RouteDownloads(kind, page_private, ending, hosts);
  };
  auto regular = route(ProfileKind::kDefault, false, false, false);
  assert(regular.error == ProfileError::kNone && !regular.off_the_record);
  auto isolated = route(ProfileKind::kIsolated, false, false, true);
  assert(isolated.error == ProfileError::kNone && !isolated.off_the_record);
  assert(route(ProfileKind::kIsolated, false, false, false).error == ProfileError::kWindowMismatch);
  assert(route(ProfileKind::kIsolated, true, false, false).error == ProfileError::kWindowMismatch);
  auto own_private = route(ProfileKind::kPrivate, true, false, true);
  assert(own_private.error == ProfileError::kNone && own_private.off_the_record);
  auto this_private_window = route(ProfileKind::kDefault, true, false, false);
  assert(this_private_window.error == ProfileError::kNone && this_private_window.off_the_record);
  assert(route(ProfileKind::kPrivate, false, false, false).error == ProfileError::kWindowMismatch);
  assert(route(ProfileKind::kPrivate, false, false, true).error == ProfileError::kWindowMismatch);
  assert(route(ProfileKind::kPrivate, true, false, false).error == ProfileError::kWindowMismatch);
  assert(route(ProfileKind::kPrivate, true, true, true).error == ProfileError::kPrivateEnding);
  assert(route(ProfileKind::kDefault, true, true, false).error == ProfileError::kPrivateEnding);
  assert(IsDownloadControl("downloads.cancel") && IsDownloadControl("downloads.pause") &&
         IsDownloadControl("downloads.resume") && IsDownloadControl("downloads.keep"));
  assert(!IsDownloadControl("downloads.list") && !IsDownloadControl("downloads.settings") && !IsDownloadControl("downloads.remove"));
}

void Clears() {
  const std::string id = "3f2504e0-4f89-41d3-9a0c-0305e82c3301";
  base::ListValue keep;
  keep.Append(base::Value(id));
  base::DictValue ok;
  ok.Set("keep", base::Value(std::move(keep)));
  ok.Set("before", base::Value(1700000000000.0));
  assert(ValidateRequest("downloads.clear", ok).empty());
  base::DictValue ints;
  ints.Set("keep", base::Value(base::ListValue()));
  ints.Set("before", base::Value(5));
  assert(ValidateRequest("downloads.clear", ints).empty());
  assert(!ValidateRequest("downloads.clear", Dict({})).empty());
  assert(!ValidateRequest("downloads.clear", Dict({{"before", base::Value(1.0)}})).empty());
  base::ListValue bad;
  bad.Append(base::Value("saved-1"));
  base::DictValue invalid;
  invalid.Set("keep", base::Value(std::move(bad)));
  invalid.Set("before", base::Value(1.0));
  assert(!ValidateRequest("downloads.clear", invalid).empty());
  base::ListValue many;
  for (size_t n = 0; n <= kMaxKeptDownloads; ++n) many.Append(base::Value(id));
  base::DictValue oversized;
  oversized.Set("keep", base::Value(std::move(many)));
  oversized.Set("before", base::Value(1.0));
  assert(!ValidateRequest("downloads.clear", oversized).empty());
  base::DictValue negative;
  negative.Set("keep", base::Value(base::ListValue()));
  negative.Set("before", base::Value(-1.0));
  assert(!ValidateRequest("downloads.clear", negative).empty());
  assert(KeptIds(ok) == std::vector<std::string>{id});
}

void FileGates() {
  const std::vector<std::string> targets = {"/d/a.zip", "/d/b c.pdf"};
  assert(IconPathAllowed("a.zip", targets));
  assert(IconPathAllowed("file.bin", targets));
  assert(IconPathAllowed("/d/a.zip", targets));
  assert(!IconPathAllowed("/Applications/Arc.app", targets));
  assert(!IconPathAllowed("dir/a.zip", targets));
  assert(!IconPathAllowed("", targets));
  assert(!IconPathAllowed(std::string("a\0b", 3), targets));
  assert(ListedPaths({"/d/a.zip", "/etc/passwd", "/d/b c.pdf", "relative"}, targets) ==
         (std::vector<bool>{true, false, true, false}));
  assert(ChooseOpenBackend(true) == OpenBackend::kDesktop);
  assert(ChooseOpenBackend(false) == OpenBackend::kUnavailable);
  assert(ChooseDragStart(true, false, true) == DragStart::kStart);
  assert(ChooseDragStart(true, false, false) == DragStart::kButtonReleased);
  assert(ChooseDragStart(true, true, true) == DragStart::kDragActive);
  assert(ChooseDragStart(false, false, true) == DragStart::kWindowClosed);
  for (const char* capability : kFileCapabilities) std::printf("capability %s\n", capability);
}

void Reveal() {
  assert(ChooseRevealBackend(true, true) == RevealBackend::kDesktop);
  assert(ChooseRevealBackend(true, false) == RevealBackend::kDesktop);
  assert(ChooseRevealBackend(false, true) == RevealBackend::kDesktop);
  assert(ChooseRevealBackend(false, false) == RevealBackend::kUnavailable);
}

void Trash(const std::string& scratch) {
  assert(TrashDirectory("/x/data", "/home/me") == "/x/data/Trash");
  assert(TrashDirectory("relative", "/home/me") == "/home/me/.local/share/Trash");
  assert(TrashDirectory("", "/home/me") == "/home/me/.local/share/Trash");
  assert(TrashDirectory(nullptr, nullptr).empty());
  assert(TrashDirectory(nullptr, "home").empty());

  const std::string downloads = scratch + "/Downloads";
  const std::string trash = scratch + "/data/Trash";
  mkdir(downloads.c_str(), 0700);
  const std::string file = downloads + "/report 1.pdf";
  Touch(file, "first");
  const std::time_t when = 1700000000;
  auto moved = MoveToTrash(file, trash, when);
  assert(moved.ok);
  assert(moved.trashed == trash + "/files/report 1.pdf");
  assert(!Exists(file));
  assert(Read(moved.trashed) == "first");
  const std::string info = Read(trash + "/info/report 1.pdf.trashinfo");
  assert(info == "[Trash Info]\nPath=" + FileUrl(file).substr(7) + "\nDeletionDate=2023-11-14T22:13:20\n");

  Touch(file, "second");
  Touch(trash + "/files/report 1.2.pdf", "orphan without info");
  auto again = MoveToTrash(file, trash, when);
  assert(again.ok);
  assert(again.trashed == trash + "/files/report 1.3.pdf");
  assert(Read(trash + "/files/report 1.2.pdf") == "orphan without info");
  assert(!Exists(trash + "/info/report 1.2.pdf.trashinfo"));
  assert(Exists(trash + "/info/report 1.3.pdf.trashinfo"));

  const std::string plain = downloads + "/README";
  Touch(plain);
  Touch(downloads + "/elsewhere");
  assert(MoveToTrash(plain, trash, when).ok);
  Touch(plain);
  assert(MoveToTrash(plain, trash, when).trashed == trash + "/files/README.2");

  assert(MoveToTrash(downloads + "/missing.zip", trash, when).code == "ERR_FILE_MISSING");
  assert(MoveToTrash(downloads, trash, when).code == "ERR_FILE_TYPE");
  const std::string link_path = downloads + "/link";
  assert(symlink((downloads + "/elsewhere").c_str(), link_path.c_str()) == 0);
  assert(MoveToTrash(link_path, trash, when).code == "ERR_FILE_TYPE");
  assert(Exists(downloads + "/elsewhere"));
  assert(MoveToTrash(downloads + "/elsewhere", "", when).code == "ERR_TRASH_UNAVAILABLE");
  assert(MoveToTrash(downloads + "/elsewhere", "relative/Trash", when).code == "ERR_TRASH_UNAVAILABLE");
  assert(MoveToTrash("relative", trash, when).code == "ERR_FILE_PATH");
  assert(MoveToTrash(downloads + "/../Downloads/elsewhere", trash, when).code == "ERR_FILE_PATH");
  const std::string blocked = scratch + "/blocked";
  Touch(blocked);
  assert(MoveToTrash(downloads + "/elsewhere", blocked + "/Trash", when).code == "ERR_TRASH_UNAVAILABLE");
  assert(Exists(downloads + "/elsewhere"));
}

void Ledger() {
  LinuxDropLedger ledger;
  assert(!ledger.Take({}, 0));
  assert(ledger.Record({"/home/me/a.txt", "/home/me/b c.png"}, 1000));
  assert(ledger.pending());
  auto taken = ledger.Take({"b c.png", "a.txt"}, 2000);
  assert(taken && taken->size() == 2 && (*taken)[0] == "/home/me/a.txt");
  assert(!ledger.pending());
  assert(!ledger.Take({"b c.png", "a.txt"}, 2000));

  assert(ledger.Record({"/home/me/a.txt"}, 1000));
  assert(!ledger.Take({"other.txt"}, 1001));
  assert(!ledger.Take({"a.txt"}, 1002));

  assert(ledger.Record({"/home/me/a.txt"}, 1000));
  assert(!ledger.Take({"a.txt", "a.txt"}, 1001));

  assert(ledger.Record({"/home/me/a.txt"}, 1000));
  assert(!ledger.Take({"a.txt"}, 1000 + LinuxDropLedger::kLifetimeMs + 1));

  assert(ledger.Record({"/home/me/a.txt"}, 5000));
  assert(!ledger.Take({"a.txt"}, 4000));

  assert(ledger.Record({"/home/me/old.txt"}, 1000));
  assert(!ledger.Record({"relative.txt"}, 1000));
  assert(!ledger.pending());
  assert(!ledger.Record({"/"}, 1000));
  assert(!ledger.Record({}, 1000));
  std::vector<std::string> many(LinuxDropLedger::kMaxFiles + 1, "/home/me/x");
  assert(!ledger.Record(many, 1000));
  many.pop_back();
  assert(ledger.Record(many, 1000));
  assert(ledger.Take(std::vector<std::string>(LinuxDropLedger::kMaxFiles, "x"), 1000));

  assert(ledger.Record({"/home/me/first.txt"}, 1000));
  assert(ledger.Record({"/home/me/second.txt"}, 1100));
  assert(!ledger.Take({"first.txt"}, 1200));
  ledger.Clear();
  assert(!ledger.pending());
}

}

int main(int argc, char** argv) {
  assert(argc == 2);
  setenv("TZ", "UTC", 1);
  tzset();
  Ids();
  Paths();
  Requests();
  Reveal();
  Snapshots();
  Routes();
  Clears();
  FileGates();
  Trash(argv[1]);
  Ledger();
  std::puts("downloads scenarios passed");
  return 0;
}
