import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src'
HOST = SOURCE / 'ardia/linux/ac_linux_host.cc'
FAVICONS = SOURCE / 'ardia/linux/ac_linux_favicons.cc'

CASES = r'''
#include "ardia/linux/ac_linux_favicon_policy.h"
#include <cassert>
#include <string>
using namespace ardiacore::linux_favicons;
int main() {
  assert(Allowed("https://example.test/favicon.ico", Source::kNetwork));
  assert(Allowed("HTTP://example.test/a.png", Source::kNetwork));
  assert(!Allowed("data:image/png;base64,AAAA", Source::kNetwork));
  assert(Allowed("data:image/png;base64,AAAA", Source::kPage));
  assert(!Allowed("data:text/html,x", Source::kPage));
  assert(!Allowed("file:///etc/passwd", Source::kPage) && !Allowed("chrome://settings", Source::kPage));
  assert(!Allowed("javascript:alert(1)", Source::kPage) && !Allowed("", Source::kNetwork));
  assert(!Allowed(std::string(kMaxUrlLength + 1, 'h'), Source::kNetwork));
  assert(Pick({}) == -1 && Pick({{0, 0}, {-1, 5}}) == -1);
  assert(Pick({{16, 16}, {32, 32}, {48, 48}}) == 2);
  assert(Pick({{16, 16}, {256, 256}, {96, 96}, {64, 64}}) == 3);
  assert(Pick({{128, 128}, {96, 96}}) == 1);
  assert(Pick({{16, 16}, {0, 0}}) == 0);
  assert(Fit(16, 16) == std::make_pair(16, 16));
  assert(Fit(64, 64) == std::make_pair(64, 64));
  assert(Fit(256, 128) == std::make_pair(64, 32));
  assert(Fit(4000, 1) == std::make_pair(64, 1));
  assert(Fit(0, 10) == std::make_pair(0, 0) && Fit(10, -1) == std::make_pair(0, 0));
  return 0;
}
'''


class FaviconPolicyTests(unittest.TestCase):
    def test_bounded_schemes_size_choice_and_fit(self):
        with tempfile.TemporaryDirectory(prefix='ardia-favicon-policy-') as directory:
            source = Path(directory) / 'cases.cc'
            source.write_text(CASES)
            binary = Path(directory) / 'cases'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I', str(SOURCE), str(source), '-o', str(binary)], check=True, timeout=60)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_cache_miss_paths_use_the_page_profile(self):
        host = HOST.read_text()
        favicons = FAVICONS.read_text()
        self.assertIn('ardiacore::DownloadLinuxFavicon(\n        contents, GURL(values[0].GetString())', host)
        self.assertIn('Profile* page = page_profile();\n    if (!favicon_fetcher_ || favicon_fetcher_->profile() != page)', host)
        fetch = host[host.index('if (name == "favicons.fetch") {'):]
        fetch = fetch[:fetch.index('if (ardiacore::IsLinuxZoomDomainRoute(name)) {')]
        self.assertIn('Profile* page = host_->page_profile();', fetch)
        self.assertIn('page->IsOffTheRecord()', fetch)
        self.assertIn('kWindowMismatch', fetch)
        self.assertIn('GetDefaultStoragePartition()->GetURLLoaderFactoryForBrowserProcess()', favicons)
        self.assertIn('params.set_max_download_size(policy::kMaxBytes);', favicons)
        self.assertIn('contents->DownloadImage(\n      url, true,', favicons)
        self.assertIn('if (!current || !current.Run()) return PageDownloads().Finish(key, base::Value());', favicons)
        self.assertNotIn('SystemNetworkContextManager', favicons)


if __name__ == '__main__':
    unittest.main()
