import pathlib
import shutil
import subprocess
import tempfile
import unittest


SOURCE = pathlib.Path(__file__).resolve().parents[1] / 'src'


class HostPolicyTests(unittest.TestCase):
    def test_resource_and_document_names_cannot_escape_their_roots(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler is unavailable')
        code = '''#include <cassert>
#include <string>
#include "ardia/linux/ac_linux_policy.h"
int main() {
  using namespace ardia::linux_policy;
  assert(ValidDocumentName("bookmarks-cache.json"));
  assert(ValidDocumentName("session.json"));
  assert(!ValidDocumentName("../session.json"));
  assert(!ValidDocumentName("/etc/passwd"));
  assert(!ValidDocumentName(".."));
  assert(!ValidDocumentName("."));
  assert(!ValidDocumentName(""));
  assert(!ValidDocumentName(std::string("state\\0.json", 11)));
  assert(!ValidDocumentName(std::string(129, 'a')));
  assert(ValidAssetPath("index.html"));
  assert(ValidAssetPath("assets/index-A8x.js"));
  assert(ValidAssetPath("favicons/en.wikipedia.org.png"));
  assert(!ValidAssetPath("../index.html"));
  assert(!ValidAssetPath("assets/../../index.html"));
  assert(!ValidAssetPath("assets/%2e%2e/index.html"));
  assert(!ValidAssetPath("assets/%2fetc/passwd"));
  assert(!ValidAssetPath("assets//index.js"));
  assert(!ValidAssetPath("assets/"));
  assert(!ValidAssetPath("/assets/index.js"));
  assert(!ValidAssetPath("assets\\\\index.js"));
  assert(!ValidAssetPath("assets/index.js?url=file:///etc/passwd"));
}
'''
        with tempfile.TemporaryDirectory(prefix='ardia-host-policy-test-') as scratch:
            scratch = pathlib.Path(scratch)
            cpp = scratch / 'policy.cc'
            cpp.write_text(code)
            binary = scratch / 'policy'
            subprocess.run([compiler, '-std=c++20', '-Wall', '-Werror', '-I', str(SOURCE), str(cpp), '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
