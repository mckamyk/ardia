from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class KeyboardInputTests(unittest.TestCase):
    def test_window_input_handles_shortcuts_without_a_focused_views_view(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        source = (Path(__file__).resolve().parents[1] / 'src/ardia/linux/ac_linux_host.cc').read_text()
        start = source.index('void ShellHostView::OnKeyEvent(')
        brace = source.index('{', start)
        depth = 1
        end = brace + 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        method = source[start:end]
        focus_start = source.index('void ShellHostView::FocusShell()')
        focus_end = source.index('\n}\n', focus_start) + 3
        method += source[focus_start:focus_end]
        prelude = r'''
#include <cassert>
#include <iostream>
#include <set>
#include <string>
#include "ardia/linux/ac_linux_typeahead.h"
#define LOG(level) std::cerr
namespace base {
struct CommandLine {
  static CommandLine* ForCurrentProcess() { static CommandLine value; return &value; }
  bool HasSwitch(const char*) { return false; }
};
}
namespace ui {
using KeyboardCode = int;
constexpr int VKEY_UNKNOWN = 0;
inline int DomCodeToUsLayoutKeyboardCode(int code) { return code; }
enum class EventType { kKeyPressed, kKeyReleased };
struct KeyEvent {
  EventType event_type = EventType::kKeyPressed;
  int key = 84;
  bool stopped = false;
  EventType type() const { return event_type; }
  bool IsControlDown() const { return true; }
  int key_code() const { return key; }
  int code() const { return key; }
  int flags() const { return 4; }
  void StopPropagation() { assert(!stopped); stopped = true; }
};
struct Accelerator {
  int key;
  Accelerator(int value, int) : key(value) {}
  auto operator<=>(const Accelerator&) const = default;
};
}
struct FocusManager {
  bool suspended = false;
  bool shortcut_handling_suspended() const { return suspended; }
  void* GetFocusedView() const { return nullptr; }
};
struct ShellContents {
  int focuses = 0;
  void Focus() { ++focuses; }
};
struct ShellView {
  int requests = 0;
  bool HasFocus() const { return true; }
  void RequestFocus() { ++requests; }
};
struct ShellHostView {
  ShellContents contents;
  ShellView shell_view;
  ShellView* shell_ = &shell_view;
  ShellContents* shell() { return &contents; }
  void FocusShell();
  FocusManager focus;
  FocusManager* focus_manager_ = &focus;
  bool shortcut_recording_ = false;
  bool ready = true;
  bool modal = false;
  bool consume = true;
  int dispatches = 0;
  std::string window_id_ = "main";
  std::set<ui::Accelerator> shortcuts_;
  ardia::linux_window::Typeahead<ui::KeyEvent> typeahead_;
  bool CanHandleAccelerators() const { return ready; }
  bool OwnsKeyboardEvent(const ui::KeyEvent&) const { return !modal; }
  bool AcceleratorPressed(const ui::Accelerator& accelerator) {
    assert(accelerator.key == 84);
    ++dispatches;
    return consume;
  }
  void OnKeyEvent(ui::KeyEvent* event);
};
'''
        checks = r'''
int main() {
  ShellHostView host;
  host.FocusShell();
  assert(host.shell_view.requests == 1 && host.contents.focuses == 1);
  ui::KeyEvent event;
  host.OnKeyEvent(&event);
  assert(event.stopped && host.dispatches == 1);
  host.consume = false;
  event.stopped = false;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  host.modal = true;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  host.modal = false;
  host.shortcut_recording_ = true;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  host.shortcut_recording_ = false;
  host.focus.suspended = true;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  host.focus.suspended = false;
  host.ready = false;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  host.ready = true;
  event.event_type = ui::EventType::kKeyReleased;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  event.event_type = ui::EventType::kKeyPressed;
  host.focus_manager_ = nullptr;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  host.focus_manager_ = &host.focus;
  host.consume = true;
  host.typeahead_.Start();
  host.ready = false;
  event.stopped = false;
  host.OnKeyEvent(&event);
  assert(event.stopped && host.dispatches == 2);
  event.stopped = false;
  event.event_type = ui::EventType::kKeyReleased;
  host.OnKeyEvent(&event);
  assert(event.stopped);
  event.event_type = ui::EventType::kKeyPressed;
  event.stopped = false;
  host.modal = true;
  host.OnKeyEvent(&event);
  assert(!event.stopped && host.dispatches == 2);
  host.modal = false;
  host.ready = true;
  auto held = host.typeahead_.Release();
  assert(held.size() == 2 && held[0].type() == ui::EventType::kKeyPressed && held[1].type() == ui::EventType::kKeyReleased);
  event.stopped = false;
  host.OnKeyEvent(&event);
  assert(event.stopped && host.dispatches == 3);
}
'''
        with tempfile.TemporaryDirectory(prefix='ardia-keyboard-input-') as scratch:
            cpp = Path(scratch) / 'keyboard.cc'
            cpp.write_text(prelude + method + checks)
            binary = Path(scratch) / 'keyboard'
            built = subprocess.run([compiler, '-std=c++20', '-Wall', '-Werror', '-I', str(Path(__file__).resolve().parents[1] / 'src'), str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
