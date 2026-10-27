#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
  Trash(argv[1]);
  Ledger();
  std::puts("downloads scenarios passed");
  return 0;
}
