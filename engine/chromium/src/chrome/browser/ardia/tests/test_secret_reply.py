import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


TESTS = pathlib.Path(__file__).resolve().parent
ROOT = TESTS.parent
STUBS = TESTS / "secret_reply"


def body(source, start):
    begin = source.index(start)
    depth = 0
    for index in range(source.index("{", begin), len(source)):
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        if depth == 0:
            return source[begin:index + 1]
    raise AssertionError(f"unterminated {start}")


@unittest.skipUnless(shutil.which("clang++"), "clang++ is required")
class SecretReplyFixtureTests(unittest.TestCase):
    def test_serialized_secret_is_wiped_after_the_reply_callback(self):
        with tempfile.TemporaryDirectory(prefix="ardia-secret-reply-") as directory:
            executable = pathlib.Path(directory) / "secret-reply"
            compiled = subprocess.run([
                "clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-g", "-O0",
                "-fsanitize=undefined", "-fno-omit-frame-pointer", "-fno-builtin",
                "-I", str(STUBS), "-I", str(ROOT.parents[2]),
                str(ROOT / "ac_sensitive_args.cc"), str(STUBS / "scenarios.cc"), "-o", str(executable),
            ], capture_output=True, text=True, timeout=180)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("secret reply scenarios passed", result.stdout)


class SecretReplyWiringTests(unittest.TestCase):
    def test_reply_sends_secrets_through_the_wiping_serializer(self):
        header = (ROOT / "ac_engine.h").read_text()
        self.assertIn("void SendSecret(base::DictValue result);", header)
        engine = (ROOT / "ac_engine.cc").read_text()
        send = body(engine, "void Reply::SendSecret(base::DictValue result) {")
        self.assertIn("SendSecretJson(std::move(result), reply, context);", send)
        self.assertIn('#include "chrome/browser/ardia/ac_sensitive_args.h"', engine)

    def test_reveal_and_card_number_reply_with_send_secret(self):
        passwords = (ROOT / "ac_passwords.cc").read_text()
        reveal = body(passwords, "AC_ENGINE_CALL(ac_passwords_reveal) {")
        self.assertIn("reply.SendSecret(std::move(result));", reveal)
        self.assertNotIn("reply.Send(std::move(result));", reveal)
        self.assertIn("ardia::Cleanse(credential->password);", reveal)
        autofill = (ROOT / "ac_autofill.cc").read_text()
        number = body(autofill, "AC_ENGINE_CALL(ac_autofill_card_number) {")
        self.assertIn("reply.SendSecret(std::move(result));", number)
        self.assertNotIn("reply.Send(std::move(result));", number)
        self.assertNotRegex(number, r"Digits\(\s*base::UTF16ToUTF8\(")

    def test_no_comments_in_the_new_sources(self):
        for path in [ROOT / "ac_sensitive_args.cc", ROOT / "ac_sensitive_args.h", *STUBS.rglob("*.h"), STUBS / "scenarios.cc"]:
            text = path.read_text()
            lines = [line for line in text.splitlines() if not line.startswith("// Copyright")]
            self.assertIsNone(re.search(r"(^|\s)//", "\n".join(lines)), path)
            self.assertNotIn("/*", text, path)


if __name__ == "__main__":
    unittest.main()
