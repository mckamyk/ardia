import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_renderer_contract import method


ROOT = Path(__file__).resolve().parents[1]
HOST = Path(os.environ.get('ARDIA_PROFILE_HOST_SOURCE', ROOT / 'src/ardia/linux/ac_linux_host.cc'))


def branch(source, marker):
    return method(source, marker)


class ProfileHostTests(unittest.TestCase):
    def setUp(self):
        self.source = HOST.read_text()

    def test_refused_window_is_reported_closed_to_every_renderer_before_chrome_closes_it(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        broadcast = method(self.source, 'void BroadcastWindowClosed(')
        refuse = method(self.source, 'void RefuseRequestedWindow(')
        code = r'''
#include <cassert>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
std::vector<std::string> log;
namespace base {
struct DictValue {
  std::map<std::string, std::string> values;
  DictValue&& Set(const char* key, std::string value) && { values[key] = std::move(value); return std::move(*this); }
};
struct Value {
  DictValue dict;
  explicit Value(DictValue value) : dict(std::move(value)) {}
};
}
struct Host {
  std::string name;
  void Event(std::string_view module, std::string_view event, base::Value payload) {
    log.push_back(name + ":" + std::string(module) + "." + std::string(event) + ":" +
                  payload.dict.values["type"] + ":" + payload.dict.values["id"]);
  }
};
std::map<int, Host*>& Hosts() { static std::map<int, Host*> hosts; return hosts; }
struct RequestedWindow { std::string id; };
std::map<int, RequestedWindow>& RequestedWindows() { static std::map<int, RequestedWindow> windows; return windows; }
struct Window { void Close() { log.push_back("chrome:close"); } };
struct BrowserWindowInterface {
  Window window;
  Window* GetWindow() { return &window; }
};
ARDIA_NATIVE
int main() {
  Host opener{"opener"};
  Host other{"other"};
  Hosts()[1] = &opener;
  Hosts()[2] = &other;
  RequestedWindows()[7] = {"w-private"};
  RequestedWindows()[8] = {"w-kept"};
  BrowserWindowInterface browser;
  RefuseRequestedWindow(&browser, RequestedWindows().find(7));
  assert(!RequestedWindows().contains(7) && RequestedWindows().contains(8));
  assert((log == std::vector<std::string>{
      "opener:ArdiaShell.onWindowEvent:close:w-private",
      "other:ArdiaShell.onWindowEvent:close:w-private",
      "chrome:close"}));
}
'''
        with tempfile.TemporaryDirectory(prefix='ardia-profile-host-') as scratch:
            cpp = Path(scratch) / 'refuse.cc'
            binary = Path(scratch) / 'refuse'
            cpp.write_text(code.replace('ARDIA_NATIVE', broadcast + '\n' + refuse))
            result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_attach_refusal_and_private_close_use_the_broadcast(self):
        attach = method(self.source, '  void Attach(base::WeakPtr<BrowserWindowInterface> browser, bool startup) {')
        self.assertIn('return RefuseRequestedWindow(browser.get(), requested);', attach)
        self.assertNotIn('->Close()', attach)
        closed = method(self.source, '  void OnBrowserClosed(BrowserWindowInterface* browser) override {')
        self.assertIn('if (private_window) BroadcastWindowClosed(closed_window);', closed)

    def test_private_windows_are_hidden_from_and_protected_against_other_renderers(self):
        ids = branch(self.source, '  if (method == "ArdiaShell.windowIds") {')
        self.assertIn('WindowVisibleTo(host->access(), window_id_)', ids)
        request = branch(self.source, '  if (method == "ArdiaShell.requestWindowClose") {')
        self.assertIn('WindowClosableBy(target->access(), window_id_)', request)
        direct = branch(self.source, '    } else if (method == "ArdiaShell.focusWindow") {')
        self.assertIn('WindowClosableBy(target->access(), window_id_)', self.source[self.source.index(direct):self.source.index(direct) + len(direct) + 400])
        open_window = branch(self.source, '  if (method == "ArdiaShell.openWindow") {')
        self.assertIn('opening.opener = window_id_;', open_window)

    def test_downloads_and_files_use_the_window_page_profile_and_refuse_private_windows(self):
        request = method(self.source, '  void Request(const base::ListValue& arguments) {')
        downloads = request[request.index('if (LinuxDownloadsHandles(name)) {'):]
        downloads = downloads[:downloads.index('return LinuxDownloadsCall(')]
        self.assertIn('Profile* page = host_->page_profile();', downloads)
        self.assertIn('page->IsOffTheRecord()', downloads)
        self.assertIn('ProfileError::kPrivateService', downloads)
        self.assertIn('!host_->HostsProfile(profile)', downloads)
        self.assertIn('return LinuxDownloadsCall(page, name, input,', request)
        self.assertNotIn('LinuxDownloadsCall(host_->profile()', request)
        invoke = method(self.source, 'void ShellHostView::Invoke(')
        files = invoke[invoke.index('if (LinuxFilesHandles(method)) {'):]
        files = files[:files.index('\n  }\n') + 4]
        self.assertIn('page_profile()->IsOffTheRecord()', files)
        self.assertIn('ProfileError::kPrivateService', files)
        self.assertIn('LinuxFilesInvoke(page_profile(), shell_, method, values, std::move(done))', files)

    def test_window_openings_are_profile_bound_and_bounded(self):
        open_window = branch(self.source, '  if (method == "ArdiaShell.openWindow") {')
        self.assertIn('OpeningWindows().Begin(id, profile_name->id, reply)', open_window)
        self.assertIn('OpeningAdmission::kMismatch', open_window)
        self.assertIn('request.page_profile != profile_name->id', open_window)
        self.assertIn('base::BindOnce(&ExpireWindowOpening, id, ticket.generation), kWindowOpeningLimit', open_window)
        opening = method(self.source, 'void OpenNativeWindow(')
        self.assertTrue(opening.split('{', 1)[1].lstrip().startswith('if (!OpeningWindows().Current(opening.id, generation)) return;'))
        self.assertNotIn('std::set<std::string>& OpeningWindows()', self.source)


class ProfileServiceTests(unittest.TestCase):
    def setUp(self):
        self.source = Path(os.environ.get('ARDIA_PROFILE_SERVICE_SOURCE', ROOT / 'src/ardia/linux/ac_linux_profile_service.cc')).read_text()

    def test_dying_profiles_are_never_observed_and_private_creation_waits_for_them(self):
        watch = method(self.source, '  bool Watch(Profile* profile) {')
        self.assertIn('if (!profile || profile->ShutdownStarted()) return false;', watch)
        existing = method(self.source, '  Profile* ExistingOffTheRecord(Profile* original) override {')
        self.assertIn('GetPrimaryOTRProfile(false)', existing)
        self.assertNotIn('Watch(', existing)
        create = method(self.source, '  Profile* CreateOffTheRecord(Profile* original) override {')
        self.assertIn('return Watch(otr) ? otr : nullptr;', create)
        later = method(self.source, '  void Later(std::function<void()> task) override {')
        self.assertIn('PostDelayedTask', later)
        self.assertNotIn('OffTheRecord(Profile* original) override {\n    if (!Usable(original) || original->IsOffTheRecord()) return nullptr;\n    Profile* otr = original->GetPrimaryOTRProfile(true);\n    if (otr) Watch(otr);', self.source)

    def test_app_profile_stays_the_startup_profile(self):
        pin = method(self.source, 'void PinStartupProfile(const std::string& app_directory) {')
        for pref in ('prefs::kProfileLastUsed', 'prefs::kProfilesLastActive', 'prefs::kBrowserShowProfilePickerOnStartup'):
            self.assertIn(pref, pin)
        self.assertIn('linux_profiles::PinnedStartupProfiles(current, app_directory)', pin)
        registrar = method(self.source, 'void PinStartupProfileFor(Profile* app) {')
        for pref in ('prefs::kProfileLastUsed, prefs::kProfilesLastActive,', 'prefs::kBrowserShowProfilePickerOnStartup'):
            self.assertIn(pref, registrar)
        self.assertIn('registrar->Add(pref, base::BindRepeating(&PinStartupProfile, app_directory));', registrar)
        self.assertIn('PinStartupProfile(app_directory);', registrar)
        app = method(self.source, 'bool SetLinuxAppProfile(Profile* profile) {')
        self.assertIn('PinStartupProfileFor(profile);', app)


if __name__ == '__main__':
    unittest.main()
