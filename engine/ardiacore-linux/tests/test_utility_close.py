from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class UtilityCloseTests(unittest.TestCase):
    def test_native_close_waits_for_save_and_survives_retries_and_destruction(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        source = (Path(__file__).resolve().parents[1] / 'src/ardia/linux/ac_linux_host.cc').read_text()

        def method(name):
            start = source.index('void ShellHostView::' + name + '(')
            brace = source.index('{', start)
            depth = 1
            end = brace + 1
            while depth:
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            return source[start:end]

        methods = '\n'.join(method(name) for name in ['ArmUtilityClose', 'RequestUtilityClose', 'CloseUtilityAfterSave'])
        prelude = r'''
        #include <cassert>
        #include <functional>
        #include <map>
        #include <memory>
        #include <string>
        #include <string_view>
        #include <tuple>
        #include <type_traits>
        #include <utility>
        #include <vector>
        #define FROM_HERE 0
        namespace base {
        template<class T> struct WeakPtr {
          T* pointer = nullptr;
          std::shared_ptr<bool> alive;
          explicit operator bool() const { return pointer && alive && *alive; }
          T* operator->() const { assert(bool(*this)); return pointer; }
          T& operator*() const { return *operator->(); }
        };
        template<class F, class... Args> auto BindOnce(F function, Args... args) {
          return [function, ...args = std::move(args)](auto&&... rest) mutable {
            if constexpr (std::is_member_function_pointer_v<F>) {
              if (!std::get<0>(std::tie(args...))) return;
            }
            std::invoke(function, args..., std::forward<decltype(rest)>(rest)...);
          };
        }
        struct SingleThreadTaskRunner {
          std::vector<std::function<void()>> tasks;
          static SingleThreadTaskRunner* GetCurrentDefault() {
            static SingleThreadTaskRunner runner;
            return &runner;
          }
          void PostTask(int, std::function<void()> task) { tasks.push_back(std::move(task)); }
          void Drain() {
            while (!tasks.empty()) {
              auto batch = std::move(tasks);
              tasks.clear();
              for (auto& task : batch) task();
            }
          }
        };
        struct DictValue {
          std::map<std::string, std::string> strings;
          DictValue& Set(const char* key, const char* value) { strings[key] = value; return *this; }
          DictValue& Set(const char* key, const std::string& value) { strings[key] = value; return *this; }
          template<class T> DictValue& Set(const char*, T) { return *this; }
        };
        struct Value {
          DictValue dict;
          Value() = default;
          Value(DictValue value) : dict(std::move(value)) {}
        };
        }
        namespace views {
        struct Widget {
          enum class ClosedReason { kUnspecified };
          std::shared_ptr<bool> alive = std::make_shared<bool>(true);
          std::function<void(ClosedReason)> on_close;
          std::function<void()> on_closed;
          bool closed = false;
          bool veto = false;
          int closes = 0;
          bool IsClosed() const { return closed; }
          base::WeakPtr<Widget> GetWeakPtr() { return {this, alive}; }
          void MakeCloseSynchronous(std::function<void(ClosedReason)> callback) { on_close = std::move(callback); }
          void InvokeOverride() {
            auto callback = std::move(on_close);
            on_close = {};
            if (callback) callback(ClosedReason::kUnspecified);
          }
          void Close() {
            if (on_close) return InvokeOverride();
            if (veto) { veto = false; return; }
            closed = true;
            ++closes;
            if (on_closed) on_closed();
          }
          void DestroyNativeWindow() {
            closed = true;
            InvokeOverride();
            *alive = false;
          }
        };
        }
        struct Browser {
          views::Widget widget;
          views::Widget* GetWindow() { return &widget; }
        };
        struct ShellHostView {
          Browser native;
          Browser* browser_ = &native;
          void* browser_view_ = this;
          void* handler_ = this;
          std::shared_ptr<bool> alive = std::make_shared<bool>(true);
          bool utility_ready_ = false;
          bool utility_close_pending_ = false;
          std::string window_id_ = "settings";
          std::vector<base::Value> events;
          views::Widget* GetWidget() { return &native.widget; }
          base::WeakPtr<ShellHostView> GetWeakPtr() { return {this, alive}; }
          void Event(std::string_view module, std::string_view event, base::Value value) {
            assert(module == "ArdiaShell" && event == "onCommand");
            assert(value.dict.strings.at("command") == "closeTab");
            assert(value.dict.strings.at("windowId") == "settings");
            events.push_back(std::move(value));
          }
          void Invalidate() { *alive = false; browser_ = nullptr; browser_view_ = nullptr; }
          void ArmUtilityClose();
          void RequestUtilityClose();
          void CloseUtilityAfterSave();
        };
        '''
        tests = r'''
        int main() {
          auto* runner = base::SingleThreadTaskRunner::GetCurrentDefault();
          ShellHostView waiting;
          waiting.ArmUtilityClose();
          waiting.GetWidget()->Close();
          assert(waiting.utility_close_pending_ && waiting.events.empty());
          assert(!waiting.GetWidget()->IsClosed());
          waiting.utility_ready_ = true;
          waiting.RequestUtilityClose();
          assert(waiting.events.size() == 1);
          waiting.handler_ = nullptr;
          waiting.utility_ready_ = false;
          waiting.GetWidget()->Close();
          assert(waiting.events.size() == 1 && waiting.utility_close_pending_);
          waiting.handler_ = &waiting;
          waiting.utility_ready_ = true;
          waiting.RequestUtilityClose();
          assert(waiting.events.size() == 2);
          runner->Drain();
          assert(!waiting.GetWidget()->IsClosed());
          waiting.GetWidget()->on_closed = [&] { waiting.Invalidate(); };
          waiting.CloseUtilityAfterSave();
          assert(!waiting.GetWidget()->IsClosed());
          runner->Drain();
          assert(waiting.native.widget.closes == 1);

          ShellHostView retry;
          retry.utility_ready_ = true;
          retry.ArmUtilityClose();
          retry.GetWidget()->veto = true;
          retry.CloseUtilityAfterSave();
          runner->Drain();
          assert(!retry.GetWidget()->IsClosed() && retry.GetWidget()->on_close);
          retry.GetWidget()->Close();
          assert(retry.events.size() == 1 && !retry.GetWidget()->IsClosed());
          retry.CloseUtilityAfterSave();
          runner->Drain();
          assert(retry.GetWidget()->closes == 1);

          ShellHostView destroyed;
          destroyed.utility_ready_ = true;
          destroyed.ArmUtilityClose();
          destroyed.GetWidget()->DestroyNativeWindow();
          assert(destroyed.events.empty() && !destroyed.GetWidget()->on_close);

          ShellHostView cancelled;
          cancelled.CloseUtilityAfterSave();
          cancelled.Invalidate();
          runner->Drain();
          assert(cancelled.native.widget.closes == 0);
        }
        '''
        with tempfile.TemporaryDirectory(prefix='ardia-utility-close-') as scratch:
            root = Path(scratch)
            cpp = root / 'close.cc'
            binary = root / 'close'
            cpp.write_text(prelude + methods + tests)
            result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
