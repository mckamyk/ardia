import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

from test_renderer_contract import method as extract

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src'
LINUX = SOURCE / 'ardia' / 'linux'
HOST = (LINUX / 'ac_linux_host.cc').read_text()

LEDGER = r'''#include <cassert>
#include <string>
#include <vector>
#include "ardia/linux/ac_linux_typeahead.h"

using ardia::linux_window::Typeahead;

static void NothingIsHeldBeforeStartOrAfterRelease() {
  Typeahead<std::string> typeahead;
  assert(!typeahead.capturing());
  assert(!typeahead.Hold("a"));
  typeahead.Start();
  assert(typeahead.capturing());
  assert(typeahead.Hold("g"));
  assert(typeahead.Hold("h"));
  assert((typeahead.Release() == std::vector<std::string>{"g", "h"}));
  assert(!typeahead.capturing());
  assert(!typeahead.Hold("i"));
  assert(typeahead.Release().empty());
}

static void CapturingStartsOnlyOnce() {
  Typeahead<std::string> typeahead;
  typeahead.Start();
  typeahead.Release();
  typeahead.Start();
  assert(!typeahead.capturing());
}

static void TheBufferIsBounded() {
  Typeahead<int> typeahead;
  typeahead.Start();
  for (size_t i = 0; i < Typeahead<int>::kMaxEvents; ++i) assert(typeahead.Hold(static_cast<int>(i)));
  const auto under = typeahead;
  assert(typeahead.Hold(-1));
  assert(typeahead.capturing());
  assert(typeahead.Hold(-2));
  assert(typeahead.Release().empty());
  assert(!typeahead.capturing());
  assert(!typeahead.Hold(-3));
  auto full = under;
  const auto held = full.Release();
  assert(held.size() == Typeahead<int>::kMaxEvents);
  assert(held.front() == 0 && held.back() == static_cast<int>(Typeahead<int>::kMaxEvents - 1));
}

int main() {
  NothingIsHeldBeforeStartOrAfterRelease();
  CapturingStartsOnlyOnce();
  TheBufferIsBounded();
}
'''


class TypeaheadLedgerTests(unittest.TestCase):
    def test_ledger(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        with tempfile.TemporaryDirectory(prefix='ardia-typeahead-') as scratch:
            root = pathlib.Path(scratch)
            (root / 'ledger.cc').write_text(LEDGER)
            binary = root / 'ledger'
            built = subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(SOURCE),
                                    str(root / 'ledger.cc'), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


class TypeaheadHostTests(unittest.TestCase):
    def test_regular_windows_hold_keys_from_attach_with_a_deadline_that_discards(self):
        initialize = extract(HOST, 'void ShellHostView::Initialize()')
        self.assertIn('typeahead_.Start();', initialize)
        self.assertIn('typeahead_timer_.Start(', initialize)
        self.assertIn('&ShellHostView::DiscardTypeahead', initialize)
        self.assertNotIn('&ShellHostView::ReplayTypeahead', initialize)
        discard = extract(HOST, 'void ShellHostView::DiscardTypeahead()')
        self.assertIn('typeahead_.Release()', discard)
        self.assertNotIn('DispatchKeyEvent', discard)
        self.assertLess(initialize.index('if (!is_utility())'), initialize.index('typeahead_.Start();'))

    def test_held_keys_are_taken_before_shortcut_handling_and_only_for_this_window(self):
        handler = extract(HOST, 'void ShellHostView::OnKeyEvent(ui::KeyEvent* event)')
        hold = handler.index('typeahead_.Hold(')
        self.assertLess(hold, handler.index('CanHandleAccelerators()'))
        self.assertIn('typeahead_.capturing() && OwnsKeyboardEvent(*event)', handler)
        self.assertIn('ui::EventType::kKeyReleased', handler[:hold])
        self.assertIn('event->StopPropagation();', handler[hold:hold + 120])

    def test_replay_only_reaches_the_shells_own_text_field(self):
        replay = extract(HOST, 'void ShellHostView::ReplayTypeahead(bool field)')
        self.assertLess(replay.index('typeahead_.Release()'), replay.index('if (!field) return;'))
        self.assertLess(replay.index('if (!ShellHoldsTextField()) return;'), replay.index('DispatchKeyEvent(&event)'))
        holds = extract(HOST, 'bool ShellHostView::ShellHoldsTextField()')
        self.assertIn('shell_->HasFocus()', holds)
        self.assertIn('GetTextInputClient()', holds)
        self.assertIn('ui::TEXT_INPUT_TYPE_NONE', holds)

    def test_replay_goes_through_the_input_method_and_stops_if_the_window_goes(self):
        replay = extract(HOST, 'void ShellHostView::ReplayTypeahead(bool field)')
        self.assertIn('typeahead_.Release()', replay)
        self.assertIn('typeahead_timer_.Stop();', replay)
        self.assertIn('GetInputMethod()', replay)
        self.assertIn('DispatchKeyEvent(&event)', replay)
        self.assertIn('if (!weak) return;', replay)

    def test_ready_is_posted_from_the_windows_own_renderer(self):
        branch = extract(HOST, '  if (method == "ArdiaShell.typeaheadReady")')
        self.assertIn('is_utility()', branch)
        self.assertIn('values.size() != 1 || !values[0].is_bool()', branch)
        self.assertIn('PostTask(', branch)
        self.assertIn('&ShellHostView::ReplayTypeahead, GetWeakPtr(), values[0].GetBool()', branch)
        self.assertIn('.Append("ArdiaShell.typeaheadReady")', HOST)

    def test_helper_is_comment_free_and_built(self):
        self.assertIsNone(re.search(r'//|/\*', (LINUX / 'ac_linux_typeahead.h').read_text()))
        self.assertIn('"ac_linux_typeahead.h"', (LINUX / 'BUILD.gn').read_text())


if __name__ == '__main__':
    unittest.main()
