from pathlib import Path
import re
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'src/ardia/linux/ac_linux_page_channel.cc'


def body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class PageChannelContractTests(unittest.TestCase):
    source = SOURCE.read_text()

    def test_activation_greets_every_frame_of_the_new_primary_page(self):
        changed = body(self.source, 'void LinuxPageChannel::PrimaryPageChanged(')
        self.assertIn('ForEachRenderFrameHost', changed)
        self.assertIn('Greet(', changed)

    def test_flood_budget_belongs_to_the_sending_document(self):
        post = body(self.source, 'void LinuxPageChannel::Post(')
        self.assertIn('GetOrCreateForCurrentDocument(&frame)', post)
        self.assertNotIn('budget_', self.source)
        self.assertIn('DOCUMENT_USER_DATA_KEY_IMPL(LinuxPageDocumentBudget)', self.source)

    def test_scripts_never_run_in_the_main_world(self):
        self.assertNotIn('ISOLATED_WORLD_ID_GLOBAL', self.source)
        self.assertIsNone(re.search(r'->ExecuteJavaScript\(', self.source))
        self.assertIn('ISOLATED_WORLD_ID_ARDIA_PAGE', body(self.source, 'void RunLinuxPageEvaluation('))


if __name__ == '__main__':
    unittest.main()
