import pathlib
import re
import unittest

from test_renderer_contract import method

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
PATCH = REPO / 'engine/patches/chromium-linux-private-links.patch'
HOST = ROOT / 'src/ardia/linux/ac_linux_host.cc'


class PrivateLinkTests(unittest.TestCase):
    def test_the_patch_hands_off_the_record_navigations_to_the_linux_host_before_chrome_opens_a_window(self):
        patch = PATCH.read_text()
        self.assertTrue(patch.startswith('--- a/chrome/browser/ui/navigator/browser_navigator.cc\n'))
        added = '\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+') and not line.startswith('+++'))
        self.assertIn('bool (*g_ardia_linux_off_the_record_navigation)(WebContents* source,', added)
        self.assertEqual(added.count('#if BUILDFLAG(IS_LINUX)'), 2)
        hook = added[added.index('if (params->disposition == WindowOpenDisposition::OFF_THE_RECORD &&'):]
        self.assertIn('!params->contents_to_insert && params->source_contents &&', hook)
        self.assertIn('!params->is_renderer_initiated && params->user_gesture &&', hook)
        self.assertIn('return nullptr;', hook)
        hunks = re.split(r'^@@.*$', patch, flags=re.M)
        hunk = next(part for part in hunks if 'OFF_THE_RECORD' in part)
        context = [line[1:] for line in hunk.splitlines() if line.startswith(' ')]
        self.assertIn('        params->browser->GetTabStripModel()->GetActiveWebContents();', context)
        self.assertIn('  WebContents* contents_to_navigate_or_insert =', context)
        self.assertNotRegex(added, r'(^|\s)//')

    def test_the_patch_is_registered_for_linux_only(self):
        series = (REPO / 'engine/patches/series').read_text().splitlines()
        self.assertIn('chromium chromium-linux-private-links.patch linux', series)

    def test_the_host_turns_the_navigation_into_an_ardia_private_window_request(self):
        host = HOST.read_text()
        self.assertIn('extern bool (*g_ardia_linux_off_the_record_navigation)(content::WebContents*, const GURL&);', host)
        opened = method(host, '  bool OpenPrivateLink(content::WebContents* contents, const GURL& url) {')
        self.assertIn('binding->contents() != contents || !binding->page()', opened)
        self.assertIn('ardiacore::LinuxProfileName(page->GetOriginalProfile())', opened)
        self.assertIn('.Set("disposition", "incognito")', opened)
        self.assertNotIn('adoptId', opened)
        start = host[host.index('g_ardia_linux_off_the_record_navigation = [](content::WebContents* contents, const GURL& url) {'):]
        start = start[:start.index('\n  };\n')]
        self.assertIn('if (host->OpenPrivateLink(contents, url)) return true;', start)
        self.assertIn('return false;', start)


if __name__ == '__main__':
    unittest.main()
