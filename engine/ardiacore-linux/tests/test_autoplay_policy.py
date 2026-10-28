import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/ardia/linux'
PATCH = ROOT.parents[0] / 'patches/chromium-linux-autoplay-policy.patch'
SOURCE = pathlib.Path.home() / 'ardia-linux-build-156/chromium_git/chromium/src/chrome/browser/chrome_content_browser_client.cc'


def body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class AutoplayPolicyTests(unittest.TestCase):
    def test_pages_learn_the_sites_autoplay_block_at_document_start(self):
        channel = (LINUX / 'ac_linux_page_channel.cc').read_text()
        greet = body(channel, 'void LinuxPageChannel::Greet(content::RenderFrameHost& frame) {')
        self.assertIn('LinuxAutoplayBlocked(frame.GetOutermostMainFrame()->GetLastCommittedURL(), frame.GetBrowserContext())', greet)
        self.assertIn('Options().PageConfig(frame.IsInPrimaryMainFrame(), block_autoplay)', greet)

    def test_the_block_reads_the_pages_own_profile_autoplay_setting(self):
        autoplay = (LINUX / 'ac_linux_autoplay.cc').read_text()
        blocked = body(autoplay, 'bool LinuxAutoplayBlocked(const GURL& url, content::BrowserContext* context) {')
        self.assertIn('url.SchemeIsHTTPOrHTTPS()', blocked)
        self.assertIn('HostContentSettingsMapFactory::GetForProfile(Profile::FromBrowserContext(context))', blocked)
        self.assertIn('ContentSettingsType::AUTOPLAY', blocked)
        self.assertIn('CONTENT_SETTING_BLOCK', blocked)
        install = body(autoplay, 'void InstallLinuxAutoplayPolicy() {')
        self.assertIn('g_ardia_linux_autoplay_blocked = ', install)
        self.assertIn('extern bool (*g_ardia_linux_autoplay_blocked)(content::WebContents* contents);', autoplay)
        host = (LINUX / 'ac_linux_host.cc').read_text()
        self.assertIn('InstallLinuxAutoplayPolicy();', host)
        build = (LINUX / 'BUILD.gn').read_text()
        for name in ['ac_linux_autoplay.cc', 'ac_linux_autoplay.h']:
            self.assertIn(f'"{name}"', build)

    def test_chromium_hook_skips_engagement_bypasses_for_blocked_sites(self):
        series = (ROOT.parents[0] / 'patches/series').read_text()
        self.assertIn('chromium chromium-linux-autoplay-policy.patch linux', series)
        self.assertLess(series.index('chromium-linux-navigation-hold-hook.patch'), series.index('chromium-linux-autoplay-policy.patch'))
        text = PATCH.read_text()
        added = '\n'.join(line for line in text.splitlines() if line.startswith('+') and not line.startswith('+++'))
        self.assertIn('bool (*g_ardia_linux_autoplay_blocked)(content::WebContents* contents) = nullptr;', added)
        self.assertIn('return blink::mojom::AutoplayPolicy::kDocumentUserActivationRequired;', added)
        self.assertNotRegex(added, r'(?m)^\+\s*(//|/\*)')
        if not SOURCE.exists() or shutil.which('patch') is None:
            self.skipTest('Chromium 156 source is unavailable')
        with tempfile.TemporaryDirectory(prefix='ardia-autoplay-patch-') as directory:
            target = pathlib.Path(directory) / 'chrome/browser/chrome_content_browser_client.cc'
            target.parent.mkdir(parents=True)
            target.write_text(SOURCE.read_text())
            result = subprocess.run(['patch', '-p1', '-F0', '-i', str(PATCH)], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            patched = target.read_text()
            policy = body(patched, 'blink::mojom::AutoplayPolicy GetAutoplayPolicyForWebContents(')
            self.assertLess(policy.index('g_ardia_linux_autoplay_blocked(web_contents)'), policy.index('DetermineWebContentsAutoplayPolicy('))
            self.assertLess(patched.index('bool (*g_ardia_linux_autoplay_blocked)'), patched.index('namespace {\n\nconst char kAIManagerUserDataKey'))


if __name__ == '__main__':
    unittest.main()
