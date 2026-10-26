from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/ardia/linux/ac_linux_host.cc'


def method(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class RendererContractTests(unittest.TestCase):
    def compile_and_run(self, code, mode):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        with tempfile.TemporaryDirectory(prefix='ardia-renderer-contract-') as scratch:
            cpp = Path(scratch) / 'renderer.cc'
            binary = Path(scratch) / 'renderer'
            cpp.write_text(code)
            result = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary), mode], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def lifecycle_fixture(self):
        source = SOURCE.read_text()
        set_start = source.index('SetHandler(HostMessageHandler* handler)')
        set_signature = source[source.rfind('\n', 0, set_start) + 1:set_start] + 'SetHandler(HostMessageHandler* handler)'
        methods = method(source, set_signature) + '\n' + method(source, '  bool ClearHandler(')
        if '  bool OwnsHandler(' in source:
            methods += '\n' + method(source, '  bool OwnsHandler(')
        if '  void RetireRenderer() {' in source:
            methods += '\n' + method(source, '  void RetireRenderer()')
        guard = method(source, 'TabBinding* ShellHostView::ClaimedBinding(')
        destructor = method(source, '  ~HostMessageHandler()').strip().replace(' override', '').replace('~HostMessageHandler()', 'HostMessageHandler::~HostMessageHandler()', 1)
        code = r'''
#include "ardia/linux/ac_linux_contract.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace base {
struct DictValue {
  std::map<std::string, int> ints;
  std::map<std::string, std::string> strings;
  std::optional<int> FindInt(const char* key) const {
    const auto found = ints.find(key);
    return found == ints.end() ? std::nullopt : std::optional(found->second);
  }
};
}
std::string String(const base::DictValue& args, const char* key) {
  const auto found = args.strings.find(key);
  return found == args.strings.end() ? "" : found->second;
}
struct ShellHostView;
struct HostMessageHandler {
  ShellHostView* host_ = nullptr;
  explicit HostMessageHandler(ShellHostView* host = nullptr) : host_(host) {}
  ~HostMessageHandler();
};
struct FocusManager { void set_shortcut_handling_suspended(bool) {} };
struct TabBinding {
  bool live = true;
  bool temporary = false;
  bool transferable = true;
  std::string claim;
  uint64_t renderer_generation = 0;
  TabBinding* contents() { return live ? this : nullptr; }
};
struct ShellHostView {
  HostMessageHandler* handler_ = nullptr;
  uint64_t renderer_generation_ = 0;
  std::map<int, std::unique_ptr<TabBinding>> bindings_;
  bool utility_ready_ = false;
  bool switcher_capture_ = false;
  std::unique_ptr<int> switcher_source_;
  FocusManager* focus_manager_ = nullptr;
  bool shortcut_recording_ = false;
  int detached = 0;
  int closed = 0;
  void DetachViewports() { ++detached; }
  ShellHostView* GetWeakPtr() { return this; }
  bool CloseTab(TabBinding* binding) { binding->live = false; ++closed; return true; }
  TabBinding* Binding(int id) {
    const auto found = bindings_.find(id);
    return found == bindings_.end() ? nullptr : found->second.get();
  }
  TabBinding* ClaimedBinding(const base::DictValue&);
  ARDIA_METHODS
};
ARDIA_GUARD
ARDIA_DESTRUCTOR
int main(int argc, char** argv) {
  assert(argc == 2);
  if (std::string(argv[1]) == "destruction") {
    ShellHostView replacement;
    auto old = std::make_unique<HostMessageHandler>(&replacement);
    replacement.SetHandler(old.get());
    auto ordinary = std::make_unique<TabBinding>();
    ordinary->claim = "old";
    ordinary->renderer_generation = replacement.renderer_generation_;
    replacement.bindings_[7] = std::move(ordinary);
    auto temporary = std::make_unique<TabBinding>();
    temporary->temporary = true;
    replacement.bindings_[8] = std::move(temporary);
    auto current = std::make_unique<HostMessageHandler>(&replacement);
    replacement.SetHandler(current.get());
    replacement.Binding(7)->claim = "current";
    replacement.Binding(7)->renderer_generation = replacement.renderer_generation_;
    const auto generation = replacement.renderer_generation_;
    old.reset();
    assert(replacement.OwnsHandler(current.get(), generation));
    assert(replacement.Binding(7)->claim == "current");
    assert(!replacement.Binding(8)->live);
    assert(replacement.closed == 1);
    current.reset();
    assert(replacement.Binding(7)->claim.empty());
    assert(replacement.Binding(7)->live);
    assert(replacement.closed == 1);
    return 0;
  }
  ShellHostView host;
  HostMessageHandler a;
  HostMessageHandler b;
  host.SetHandler(&a);
  const auto a_generation = host.renderer_generation_;
  auto ordinary = std::make_unique<TabBinding>();
  ordinary->claim = "a";
  ordinary->renderer_generation = host.renderer_generation_;
  host.bindings_[7] = std::move(ordinary);
  auto temporary = std::make_unique<TabBinding>();
  temporary->claim = "temporary";
  temporary->temporary = true;
  temporary->renderer_generation = host.renderer_generation_;
  host.bindings_[8] = std::move(temporary);
  base::DictValue args;
  args.ints["id"] = 7;
  args.strings["claim"] = "a";
  assert(host.ClaimedBinding(args));
  assert(host.OwnsHandler(&a, a_generation));
  if (std::string(argv[1]) == "replacement") {
    host.SetHandler(&b);
    assert(!host.ClearHandler(&a));
    assert(!host.OwnsHandler(&a, a_generation));
    assert(!host.OwnsHandler(&a, host.renderer_generation_));
    assert(host.OwnsHandler(&b, host.renderer_generation_));
    assert(!host.ClaimedBinding(args));
    assert(host.Binding(7)->claim.empty());
    assert(host.Binding(7)->live);
    assert(!host.Binding(8)->live);
    host.Binding(7)->claim = "b";
    host.Binding(7)->renderer_generation = host.renderer_generation_;
    args.strings["claim"] = "b";
    assert(host.ClaimedBinding(args));
    const auto generation = host.renderer_generation_;
    host.SetHandler(&b);
    assert(host.renderer_generation_ == generation);
    assert(!host.ClearHandler(&a));
    assert(host.ClaimedBinding(args));
    host.Binding(7)->renderer_generation = generation - 1;
    assert(!host.ClaimedBinding(args));
  } else {
    assert(host.ClearHandler(&a));
    assert(!host.Binding(8)->live);
    assert(host.Binding(7)->live);
    assert(host.closed == 1);
    assert(!host.ClearHandler(&a));
    assert(host.closed == 1);
    host.SetHandler(&b);
    host.Binding(7)->claim = "b";
    host.Binding(7)->renderer_generation = host.renderer_generation_;
    args.strings["claim"] = "b";
    assert(host.ClaimedBinding(args));
  }
}
'''
        return code.replace('ARDIA_METHODS', methods).replace('ARDIA_GUARD', guard).replace('ARDIA_DESTRUCTOR', destructor)

    def test_handler_replacement_retires_the_predecessor_atomically(self):
        self.compile_and_run(self.lifecycle_fixture(), 'replacement')

    def test_handler_destructors_cannot_retire_a_replacement(self):
        self.compile_and_run(self.lifecycle_fixture(), 'destruction')

    def test_renderer_teardown_closes_temporary_and_retains_logical_tabs(self):
        self.compile_and_run(self.lifecycle_fixture(), 'teardown')


if __name__ == '__main__':
    unittest.main()
