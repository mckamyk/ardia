from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from document_host_fixture import ROOT, SOURCES, PRELUDE, WRITER, host_methods, values, close_invoke


SUPPORT = r'''
int Named(const ShellHostView& host, std::string_view name) {
  int count = 0;
  for (const auto& event : host.events)
    if (*std::get<2>(event).GetDict().FindString("name") == name) ++count;
  return count;
}
std::set<std::string> RequestedGenerations(const ShellHostView& host) {
  std::set<std::string> generations;
  for (const auto& event : host.events) {
    const auto& envelope = std::get<2>(event).GetDict();
    if (*envelope.FindString("name") == "shell.closeRequested")
      generations.insert(*envelope.FindDict("data")->FindString("closeGeneration"));
  }
  return generations;
}
struct Reply { bool called = false; bool ok = false; base::Value value; };
std::shared_ptr<Reply> Invoke(ShellHostView& host, std::string method, base::Value value) {
  auto reply = std::make_shared<Reply>();
  base::ListValue list; list.Append(std::move(value));
  base::DictValue args; args.Set("args", base::Value(std::move(list)));
  host.CloseInvoke(method, args, [reply](bool ok, base::Value result) {
    reply->called = true; reply->ok = ok; reply->value = std::move(result);
  });
  return reply;
}
void Commit(ShellHostView& host, int sequence, bool expect_ok) {
  bool done = false;
  host.DocumentCall("commit", CommitRequest(host.DocumentClient(), sequence), [&](auto result) {
    assert(result.ok() == expect_ok); done = true;
  });
  Drain(); assert(done);
}
auto* UI() { return base::SingleThreadTaskRunner::GetCurrentDefault(); }
'''


DELIBERATION = r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  (void)Ignore;
  if (mode == "deliberation") {
    ShellHostView host; Watch(host); host.ArmWindowClose();
    hosts[1] = &host;
    auto first = Invoke(host, "ArdiaShell.requestWindowClose", base::Value("settings"));
    assert(first->ok);
    const auto request = first->value.GetDict().Clone();
    for (auto reason : {views::Widget::ClosedReason::kWindowButton, views::Widget::ClosedReason::kCompositor,
                        views::Widget::ClosedReason::kKeyboard, views::Widget::ClosedReason::kQuit}) {
      host.native.widget.CloseWithReason(reason);
      assert(!host.native.widget.IsClosed());
    }
    assert(Invoke(host, "ArdiaShell.requestWindowClose", base::Value("settings"))->value.GetDict() == request);
    assert(host.RequestWindowClose() == request);
    assert(RequestedGenerations(host) == std::set<std::string>{*request.FindString("closeGeneration")});
    for (int minute = 0; minute < 10; ++minute) { UI()->Expire(); Drain(); }
    assert(host.close_.phase() == LinuxClose::Phase::kRequested);
    assert(host.close_.identity().close == *request.FindString("closeGeneration"));
    assert(Named(host, "shell.closeCancelled") == 0 && !host.native.widget.IsClosed());
    Commit(host, 1, true);
  }
  if (mode == "chrome-deliberation") {
    ShellHostView host; Watch(host); host.ArmWindowClose();
    int answers = 0;
    assert(host.DeferChromeClose({}, LinuxCloseStage::kPrepare, [&](bool) { ++answers; }));
    assert(host.chrome_close_token_);
    const auto token = *host.chrome_close_token_;
    const auto identity = host.close_.identity();
    for (int minute = 0; minute < 10; ++minute) { UI()->Expire(); Drain(); }
    assert(answers == 0 && host.chrome_close_token_ && *host.chrome_close_token_ == token);
    assert(host.native.discard_requests == 0 && host.close_.identity() == identity);
    assert(host.close_.phase() == LinuxClose::Phase::kRequested && !host.close_.BlocksWrites());
    assert(Named(host, "shell.closeRequested") == 1 && Named(host, "shell.closeCancelled") == 0);
  }
}
'''


DRAIN = r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  (void)Ignore;
  if (mode == "transition") {
    ShellHostView host; Watch(host); host.ArmWindowClose();
    hosts[1] = &host;
    const auto request = host.RequestWindowClose();
    auto refused = [&](base::DictValue forged) {
      auto reply = Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(std::move(forged)));
      assert(reply->called && !reply->ok);
      assert(host.close_.phase() == LinuxClose::Phase::kRequested && UI()->timers.empty());
    };
    refused(request.Clone().Set("closeGeneration", "2"));
    refused(request.Clone().Set("rendererGeneration", "4294967296"));
    refused(request.Clone().Set("rendererGeneration", base::Value(1)));
    refused(request.Clone().Set("closeGeneration", base::Value(1)));
    refused(request.Clone().Set("windowId", "main"));
    refused(request.Clone().Set("watermark", base::Value(0)));
    auto missing = request.Clone(); missing.Remove("closeGeneration"); refused(std::move(missing));
    host.handler.authorized = false; refused(request.Clone()); host.handler.authorized = true;
    auto begun = Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()));
    assert(begun->called && begun->ok && begun->value.is_dict());
    assert(begun->value.GetDict().size() == 1 && begun->value.GetDict().Find("accepted")->GetBool());
    assert(host.close_.phase() == LinuxClose::Phase::kDraining && UI()->timers.size() == 1);
    assert(files.tasks.empty() && !host.close_.BlocksWrites());
    auto again = Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()));
    assert(again->ok && host.close_.phase() == LinuxClose::Phase::kDraining && UI()->timers.size() == 1);
    assert(host.RequestWindowClose() == request && Named(host, "shell.closeRequested") == 1);
    Commit(host, 1, true);
    bool cancelled = false;
    host.CancelWindowClose(host.close_.identity(), [&](bool ok, auto) { cancelled = ok; });
    Drain(); assert(cancelled && host.close_.phase() == LinuxClose::Phase::kCancelled);
    auto stale = Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()));
    assert(!stale->ok && host.close_.phase() == LinuxClose::Phase::kCancelled);
    const auto next = host.RequestWindowClose();
    assert(*next.FindString("closeGeneration") != *request.FindString("closeGeneration"));
    assert(!Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()))->ok);
    assert(host.close_.phase() == LinuxClose::Phase::kRequested);
    UI()->timers.clear();
    assert(Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(next.Clone()))->ok);
    host.Replace();
    assert(!Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(next.Clone()))->ok);
  }
  if (mode == "seal-gate") {
    ShellHostView host; Watch(host); host.ArmWindowClose();
    hosts[1] = &host;
    const auto request = host.RequestWindowClose();
    auto early = Invoke(host, "ArdiaShell.acknowledgeWindowClose", base::Value(Ack(request, 0)));
    assert(early->called && !early->ok && *early->value.GetDict().FindString("code") == "ERR_WINDOW_CLOSE_STALE");
    assert(files.tasks.empty() && host.close_.phase() == LinuxClose::Phase::kRequested && !host.close_.BlocksWrites());
    assert(!host.close_.BeginSeal(host.close_.identity(), host.renderer_generation_, 0));
    assert(Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()))->ok);
    int acknowledged = 0;
    host.AcknowledgeWindowClose(Ack(request, 0), [&](bool ok, auto) { assert(ok); ++acknowledged; });
    host.AcknowledgeWindowClose(Ack(request, 0), [&](bool ok, auto) { assert(ok); ++acknowledged; });
    host.AcknowledgeWindowClose(Ack(request, 1), [](bool ok, auto) { assert(!ok); });
    assert(host.close_.phase() == LinuxClose::Phase::kSealing);
    assert(Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()))->ok);
    Drain(); assert(host.native.widget.IsClosed() && acknowledged == 2);
  }
  if (mode == "deadline") {
    ShellHostView host; Watch(host); host.ArmWindowClose();
    hosts[1] = &host;
    const auto request = host.RequestWindowClose();
    UI()->Expire(); Drain();
    assert(host.close_.phase() == LinuxClose::Phase::kRequested);
    assert(Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()))->ok);
    UI()->Expire(); Drain();
    assert(host.close_.phase() == LinuxClose::Phase::kCancelled && Named(host, "shell.closeCancelled") == 1);
    Commit(host, 1, true);
    const auto old = host.RequestWindowClose();
    assert(Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(old.Clone()))->ok);
    host.CancelWindowClose(host.close_.identity(), Ignore); Drain();
    const auto current = host.RequestWindowClose();
    assert(Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(current.Clone()))->ok);
    assert(UI()->timers.size() == 2);
    auto stale = std::move(UI()->timers.front()); UI()->timers.pop_front(); std::move(stale).Run(); Drain();
    assert(host.close_.phase() == LinuxClose::Phase::kDraining && Named(host, "shell.closeCancelled") == 2);
    host.native.widget.veto = true;
    int answers = 0;
    assert(host.DeferChromeClose({}, LinuxCloseStage::kPrepare, [&](bool ready) { assert(ready); ++answers; }));
    const auto token = *host.chrome_close_token_;
    bool replied = false;
    host.AcknowledgeWindowClose(Ack(current, 1), [&](bool, auto) { replied = true; });
    Drain(); assert(answers == 1 && host.close_.phase() == LinuxClose::Phase::kSealed && !replied);
    UI()->Expire(); Drain();
    assert(host.close_.phase() == LinuxClose::Phase::kSealed && host.close_.BlocksWrites());
    assert(host.native.discard_requests == 0 && *host.chrome_close_token_ == token && !replied);
    assert(Named(host, "shell.closeCancelled") == 2);
    host.CancelWindowClose(host.close_.identity(), Ignore); Drain();
    assert(replied && host.native.discard_requests == 1 && host.close_.phase() == LinuxClose::Phase::kCancelled);
    assert(Named(host, "shell.closeCancelled") == 3);
  }
  if (mode == "chrome-drain-deadline") {
    ShellHostView host; Watch(host); host.ArmWindowClose();
    hosts[1] = &host;
    int answers = 0;
    assert(host.DeferChromeClose({}, LinuxCloseStage::kPrepare, [&](bool ready) { assert(!ready); ++answers; }));
    const auto request = host.RequestWindowClose();
    assert(Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()))->ok);
    UI()->Expire(); Drain();
    assert(answers == 1 && host.native.discard_requests == 1 && !host.chrome_close_token_);
    assert(host.close_.phase() == LinuxClose::Phase::kCancelled && Named(host, "shell.closeCancelled") == 1);
    Commit(host, 1, true);
  }
  if (mode == "decision-cancel") {
    ShellHostView host; Watch(host); host.ArmWindowClose();
    hosts[1] = &host;
    int answers = 0;
    assert(host.DeferChromeClose({}, LinuxCloseStage::kPrepare, [&](bool ready) { assert(!ready); ++answers; }));
    host.native.widget.CloseWithReason(views::Widget::ClosedReason::kQuit);
    const auto request = host.RequestWindowClose();
    assert(RequestedGenerations(host).size() == 1);
    auto rejected = Invoke(host, "ArdiaShell.rejectWindowClose", base::Value(request.Clone()));
    Drain();
    assert(rejected->ok && answers == 1 && host.native.discard_requests == 1);
    assert(host.close_.phase() == LinuxClose::Phase::kCancelled && Named(host, "shell.closeCancelled") == 1);
    assert(!host.native.widget.IsClosed() && !host.close_.BlocksWrites());
    assert(!Invoke(host, "ArdiaShell.beginWindowCloseDrain", base::Value(request.Clone()))->ok);
  }
}
'''


class CloseDeliberationTests(unittest.TestCase):
    @classmethod
    def build(cls, name, main):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            raise unittest.SkipTest('C++ compiler is unavailable')
        root = Path(cls.scratch.name) / name
        (root / 'base/json').mkdir(parents=True)
        (root / 'base/values.h').write_text(values())
        (root / 'base/json/json_writer.h').write_text(WRITER)
        (root / 'main.cc').write_text(PRELUDE + host_methods() + close_invoke() + SUPPORT + main)
        binary = root / name
        result = subprocess.run([compiler, '-std=c++23', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I', str(root), '-I', str(ROOT / 'src'), *[str(ROOT / 'src/ardia/linux' / source) for source in SOURCES], str(root / 'main.cc'), '-o', str(binary)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        return binary

    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix='ardia-close-deliberation-')
        cls.binaries = {}

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def run_mode(self, program, mode):
        if program not in self.binaries:
            self.binaries[program] = self.build(program, DELIBERATION if program == 'deliberation' else DRAIN)
        result = subprocess.run([str(self.binaries[program]), mode], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_decision_longer_than_the_deadline_never_cancels_and_repeats_join(self):
        self.run_mode('deliberation', 'deliberation')

    def test_chrome_initiated_decision_keeps_its_token_and_preparation(self):
        self.run_mode('deliberation', 'chrome-deliberation')

    def test_drain_transition_is_authenticated_idempotent_and_rejects_stale_identity(self):
        self.run_mode('drain', 'transition')

    def test_seal_requires_the_draining_phase_and_keeps_watermark_idempotency(self):
        self.run_mode('drain', 'seal-gate')

    def test_drain_deadline_starts_at_drain_checks_identity_and_ends_at_seal(self):
        self.run_mode('drain', 'deadline')

    def test_drain_deadline_requests_coordinated_chrome_cancellation(self):
        self.run_mode('drain', 'chrome-drain-deadline')

    def test_cancel_decision_rejects_the_pending_attempt_through_chrome(self):
        self.run_mode('drain', 'decision-cancel')


if __name__ == '__main__':
    unittest.main()
