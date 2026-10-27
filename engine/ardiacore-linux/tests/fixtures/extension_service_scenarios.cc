#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

Profile regular;
Profile isolated;
Profile incognito;
std::vector<extensions::ExtensionInstallPromptClient::Result> answers;

struct Outcome {
  bool replied = false;
  Profile* profile = nullptr;
  std::string code;
};

Outcome Resolve(const ardia::LinuxExtensionWindow& window, const std::string& name, std::string_view requested) {
  Outcome outcome;
  ardia::CallProfile(window, name, requested, base::OnceCallback<void(Profile*, std::string)>([&](Profile* profile, std::string code) {
    outcome = {true, profile, std::move(code)};
  }));
  assert(outcome.replied);
  return outcome;
}

struct Reply {
  bool ok = false;
  std::string code;
};

Reply Answer(const ardia::LinuxExtensionWindow& window, const std::string& request, bool accepted) {
  Outcome profile = Resolve(window, "extensions.resolveInstallPrompt", "");
  if (!profile.profile) return {false, profile.code};
  Reply reply;
  ardia::ResolvePrompt(window, profile.profile, base::DictValue().Set("requestId", request).Set("accepted", accepted),
                       ardia::Done([&](bool ok, base::Value value) {
                         reply.ok = ok;
                         if (const std::string* code = value.is_dict() ? value.GetDict().FindString("code") : nullptr) reply.code = *code;
                       }));
  return reply;
}

std::string Prompt(Profile* profile, int window, base::WeakPtrFactory<Profile>& weak) {
  auto request = ardia::Prompts().Add(ardia::ProfileId(profile), window, base::DictValue(),
      ardia::PendingInstall{ardia::InstallDone([](extensions::ExtensionInstallPromptClient::DoneCallbackPayload payload) {
        answers.push_back(payload.result);
      }), weak.GetWeakPtr()});
  assert(request);
  return *request;
}

}

int main() {
  incognito.off_the_record = true;
  ardiacore::g_profiles = {{"", &regular}, {"work", &isolated}};
  base::WeakPtrFactory<Profile> regular_weak(&regular);
  base::WeakPtrFactory<Profile> isolated_weak(&isolated);
  content::WebContents isolated_tab;
  isolated_tab.context = &isolated;
  content::WebContents regular_tab;
  regular_tab.context = &regular;

  ardia::LinuxExtensionWindow work;
  work.profile = &isolated;
  work.window = 7;
  work.tab = base::RepeatingCallback<content::WebContents*(std::string_view)>([&](std::string_view key) -> content::WebContents* {
    return key == "work-tab" ? &isolated_tab : key == "regular-tab" ? &regular_tab : nullptr;
  });

  for (const char* call : {"extensions.actionStates", "extensions.sidePanelUrl", "extensions.resolveInstallPrompt"}) {
    const Outcome outcome = Resolve(work, call, "");
    assert(outcome.profile == &isolated && outcome.code.empty());
  }
  assert(Resolve(work, "extensions.list", "").profile == &regular);
  assert(Resolve(work, "extensions.list", "work").profile == &isolated);
  assert(Resolve(work, "extensions.list", "incognito:w1@").code == "ERR_PROFILE_PRIVATE");
  assert(Resolve(work, "extensions.list", "gone").code == "ERR_PROFILE_UNAVAILABLE");

  std::string code;
  assert(ardia::ProfileTab(work, &isolated, "work-tab", &code) == &isolated_tab && code.empty());
  assert(!ardia::ProfileTab(work, &isolated, "regular-tab", &code) && code == "ERR_PROFILE_WINDOW");
  code.clear();
  assert(!ardia::ProfileTab(work, &isolated, "missing", &code) && code == "ERR_TAB_UNAVAILABLE");

  const std::string accepted = Prompt(&isolated, 7, isolated_weak);
  Reply reply = Answer(work, accepted, true);
  assert(reply.ok);
  base::RunPostedTasks();
  assert(answers.size() == 1 && answers[0] == extensions::ExtensionInstallPromptClient::Result::ACCEPTED);
  assert(Answer(work, accepted, true).code == "ERR_EXTENSION_PROMPT_STALE");

  const std::string cancelled = Prompt(&isolated, 7, isolated_weak);
  ardia::LinuxExtensionWindow other = work;
  other.window = 8;
  assert(Answer(other, cancelled, true).code == "ERR_EXTENSION_PROMPT_STALE");
  assert(Answer(work, cancelled, false).ok);
  base::RunPostedTasks();
  assert(answers.size() == 2 && answers[1] == extensions::ExtensionInstallPromptClient::Result::USER_CANCELED);

  const std::string foreign = Prompt(&regular, 7, regular_weak);
  assert(Answer(work, foreign, true).code == "ERR_EXTENSION_PROMPT_STALE");
  ardia::LinuxExtensionWindow plain = work;
  plain.profile = &regular;
  assert(Answer(plain, foreign, true).ok);
  base::RunPostedTasks();
  assert(answers.size() == 3);

  const std::string orphan = Prompt(&isolated, 7, isolated_weak);
  isolated_weak.InvalidateWeakPtrs();
  assert(Answer(work, orphan, true).ok);
  base::RunPostedTasks();
  assert(answers.size() == 3);

  ardia::LinuxExtensionWindow private_window = work;
  private_window.profile = &incognito;
  assert(Resolve(private_window, "extensions.actionStates", "").code == "ERR_PROFILE_PRIVATE");
  ardia::LinuxExtensionWindow closed = work;
  closed.profile = nullptr;
  assert(Resolve(closed, "extensions.sidePanelUrl", "").code == "ERR_WINDOW_CLOSED");
  std::printf("extension service scenarios passed\n");
}
