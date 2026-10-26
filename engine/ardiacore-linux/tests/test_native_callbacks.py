from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class NativeCallbackTests(unittest.TestCase):
    def test_real_callbacks_adopt_tabs_and_close_without_breaking_transfers(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        source = (Path(__file__).resolve().parents[1] / 'src/ardia/linux/ac_linux_host.cc').read_text()

        def method(name):
            start = source.index('void TabBinding::' + name + '(')
            brace = source.index('{', start)
            depth = 1
            end = brace + 1
            while depth:
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            return source[start:end]

        methods = '\n'.join(method(name) for name in ['Rebind', 'MoveTo', 'DidOpenRequestedURL', 'WebContentsDestroyed'])
        prelude = r'''
        #include <cassert>
        #include <map>
        #include <string>
        #include <string_view>
        #include <utility>
        #include <vector>
        namespace base {
        struct DictValue {
          std::map<std::string, std::string> strings;
          std::map<std::string, DictValue> objects;
          DictValue& Set(const char* key, std::string_view value) { strings[key] = value; return *this; }
          DictValue& Set(const char* key, DictValue value) { objects[key] = std::move(value); return *this; }
        };
        std::string NumberToString(int value) { return std::to_string(value); }
        }
        template<typename T> struct raw_ptr {
          T* pointer;
          raw_ptr(T* value = nullptr) : pointer(value) {}
          raw_ptr& operator=(T* value) { pointer = value; return *this; }
          operator T*() const { return pointer; }
          T* operator->() const { return pointer; }
          T* get() const { return pointer; }
        };
        class TabBinding;
        namespace content {
        struct RenderFrameHost {};
        struct Referrer {};
        struct WebContents {
          int id;
          void* context;
          TabBinding* observer = nullptr;
          void* GetBrowserContext() { return context; }
          void Destroy();
        };
        }
        namespace views {
        struct WebView {
          bool visible = true;
          content::WebContents* contents = nullptr;
          int detachments = 0;
          bool GetVisible() { return visible; }
          void SetWebContents(content::WebContents* value) { contents = value; if (!value) ++detachments; }
        };
        }
        namespace ui { enum PageTransition { PAGE_TRANSITION_LINK }; }
        enum class WindowOpenDisposition { NEW_POPUP, NEW_WINDOW, NEW_FOREGROUND_TAB, NEW_BACKGROUND_TAB, CURRENT_TAB, OFF_THE_RECORD, SAVE_TO_DISK };
        struct GURL {
          std::string url;
          const std::string& spec() const { return url; }
        };
        struct ShellHostView {
          std::vector<std::pair<int, base::DictValue>> events;
          int removed_views = 0;
          void OfferTab(content::WebContents*) {}
          void ReportTab(int id, base::DictValue event) { events.emplace_back(id, std::move(event)); }
          void RemoveChildViewT(views::WebView*) { ++removed_views; }
        };
        int TabId(content::WebContents* contents) { return contents->id; }
        class TabBinding {
         public:
          bool presented = false;
          bool presentation_applied = false;
          bool presentation_attached = false;
          bool presentation_departed = false;
          int cancelled_auto_pip_until = 0;
          int requested_auto_pip_until = 0;
          void RememberPendingAutoPictureInPicture() {}
          TabBinding(ShellHostView* owner, content::WebContents* contents, views::WebView* viewport = nullptr)
              : owner_(owner), contents_(contents), viewport_(viewport) { Observe(contents); }
          ~TabBinding() { Observe(nullptr); }
          void Observe(content::WebContents* value) {
            if (observed_ && observed_->observer == this) observed_->observer = nullptr;
            observed_ = value;
            if (observed_) observed_->observer = this;
          }
          void Rebind(content::WebContents*);
          void MoveTo(ShellHostView*);
          void DidOpenRequestedURL(content::WebContents*, content::RenderFrameHost*, const GURL&, const content::Referrer&, WindowOpenDisposition, ui::PageTransition, bool, bool);
          void WebContentsDestroyed();
          raw_ptr<ShellHostView> owner_;
          raw_ptr<content::WebContents> contents_;
          raw_ptr<views::WebView> viewport_;
          content::WebContents* observed_ = nullptr;
        };
        void content::WebContents::Destroy() { if (observer) observer->WebContentsDestroyed(); }
        '''
        tests = r'''
        void TestOpen() {
          ShellHostView host;
          int profile = 0;
          int other_profile = 0;
          content::WebContents source{11, &profile};
          content::WebContents target{12, &profile};
          content::WebContents private_target{13, &other_profile};
          content::WebContents invalid{0, &profile};
          TabBinding binding(&host, &source);
          const std::vector<std::pair<WindowOpenDisposition, std::string>> supported = {
            {WindowOpenDisposition::NEW_FOREGROUND_TAB, "foreground"},
            {WindowOpenDisposition::NEW_BACKGROUND_TAB, "background"},
            {WindowOpenDisposition::NEW_POPUP, "popup"},
            {WindowOpenDisposition::NEW_WINDOW, "window"},
          };
          for (const auto& [disposition, expected] : supported) {
            host.events.clear();
            binding.DidOpenRequestedURL(&target, nullptr, {"https://example.test/new"}, {}, disposition, ui::PAGE_TRANSITION_LINK, false, true);
            assert(host.events.size() == 1);
            const auto& [id, event] = host.events.front();
            assert(id == 11);
            assert(event.strings.at("type") == "openWindow");
            const auto& value = event.objects.at("value");
            assert(value.strings.at("disposition") == expected);
            assert(value.strings.at("adoptId") == "tab:12");
            assert(value.strings.at("url") == "https://example.test/new");
            assert(value.strings.at("profile").empty());
          }
          host.events.clear();
          for (auto disposition : {WindowOpenDisposition::CURRENT_TAB, WindowOpenDisposition::OFF_THE_RECORD, WindowOpenDisposition::SAVE_TO_DISK}) {
            binding.DidOpenRequestedURL(&target, nullptr, {"https://example.test"}, {}, disposition, ui::PAGE_TRANSITION_LINK, false, true);
          }
          for (auto* opened : {&private_target, &invalid, static_cast<content::WebContents*>(nullptr)}) {
            binding.DidOpenRequestedURL(opened, nullptr, {"https://example.test"}, {}, WindowOpenDisposition::NEW_FOREGROUND_TAB, ui::PAGE_TRANSITION_LINK, false, true);
          }
          assert(host.events.empty());
        }
        void TestClose() {
          ShellHostView first;
          ShellHostView second;
          int profile = 0;
          content::WebContents original{20, &profile};
          content::WebContents replacement{20, &profile};
          views::WebView viewport;
          TabBinding binding(&first, &original, &viewport);
          binding.Rebind(&replacement);
          original.Destroy();
          assert(first.events.empty());
          binding.presented = true;
          binding.presentation_applied = true;
          binding.presentation_attached = true;
          binding.presentation_departed = true;
          binding.MoveTo(&second);
          assert(!binding.presented);
          assert(!binding.presentation_applied);
          assert(!binding.presentation_attached);
          assert(!binding.presentation_departed);
          assert(first.events.empty());
          assert(second.events.empty());
          assert(first.removed_views == 1);
          assert(binding.viewport_.get() == nullptr);
          replacement.Destroy();
          assert(first.events.empty());
          assert(second.events.size() == 1);
          assert(second.events.front().first == 20);
          assert(second.events.front().second.strings.at("type") == "windowClose");
          assert(binding.contents_.get() == nullptr);
          replacement.Destroy();
          assert(second.events.size() == 1);
          content::WebContents direct{30, &profile};
          views::WebView direct_view;
          TabBinding direct_binding(&first, &direct, &direct_view);
          direct.Destroy();
          assert(direct_view.detachments == 1);
          assert(first.events.size() == 1);
          assert(first.events.front().second.strings.at("type") == "windowClose");
        }
        int main(int argc, char** argv) {
          if (argc == 1 || std::string(argv[1]) == "open") TestOpen();
          if (argc == 1 || std::string(argv[1]) == "close") TestClose();
        }
        '''
        with tempfile.TemporaryDirectory(prefix='ardia-native-events-') as scratch:
            root = Path(scratch)
            cpp = root / 'events.cc'
            binary = root / 'events'
            cpp.write_text(prelude + methods + tests)
            result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
