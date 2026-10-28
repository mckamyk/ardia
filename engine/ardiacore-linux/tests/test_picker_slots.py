import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_renderer_contract import method

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src'
LINUX = SOURCE / 'ardia/linux'


class PickerSlotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='ardia-picker-slots-')
        cls.binary = Path(cls.directory.name) / 'picker-slots'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-g',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(SOURCE),
                        str(ROOT / 'tests/picker_slots_test.cc'), '-o', str(cls.binary)],
                       check=True, text=True, timeout=120)
        cls.files = (LINUX / 'ac_linux_files.cc').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def run_case(self, name):
        subprocess.run([str(self.binary), name], check=True, text=True, timeout=10,
                       env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})

    def test_a_chooser_open_in_one_window_leaves_the_others_free(self):
        self.run_case('per-window')

    def test_a_chooser_that_stopped_showing_without_answering_is_replaced(self):
        self.run_case('stale')

    def test_both_choosers_use_per_window_slots_instead_of_an_app_wide_flag(self):
        self.assertNotIn('FilesPickerOpen()', self.files)
        self.assertNotIn('FolderPickerOpen()', self.files)
        for name, code in (('LinuxPickFiles', 'ERR_FILE_PICKER_OPEN'), ('LinuxPickFolder', 'ERR_FOLDER_PICKER_OPEN')):
            pick = method(self.files, f'void {name}(gfx::NativeWindow window, const base::ListValue& values, Done done) {{')
            self.assertIn('PickerAdmission::kAlreadyShowing', pick)
            self.assertIn('PickerAdmission::kReplaceStale', pick)
            self.assertIn('->Abandon();', pick)
            self.assertIn(code, pick)

    def test_a_replaced_chooser_answers_its_caller_as_cancelled(self):
        for picker in ('class DownloadFolderPicker :', 'class OpenFilesPicker :'):
            body = self.files.split(picker)[1]
            abandon = method(body, '  void Abandon() {')
            self.assertIn('FileSelectionCanceled();', abandon)
            finish = method(body, '  void Finish() {')
            self.assertIn('.Release(window_, this);', finish)

    def test_the_file_selection_policy_is_honoured(self):
        self.assertIn('#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"', self.files)
        self.assertEqual(self.files.count('ui::SelectFileDialog::Create(picker, std::make_unique<ChromeSelectFilePolicy>(nullptr))'), 2)
        self.assertNotIn('ui::SelectFileDialog::Create(picker, nullptr)', self.files)
        for name in ('LinuxPickFiles', 'LinuxPickFolder'):
            pick = method(self.files, f'void {name}(gfx::NativeWindow window, const base::ListValue& values, Done done) {{')
            self.assertIn('ChromeSelectFilePolicy::FileSelectDialogsAllowed()', pick)
            self.assertIn('ERR_FILE_CHOOSER_DISABLED', pick)


if __name__ == '__main__':
    unittest.main()
