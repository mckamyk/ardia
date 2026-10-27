import pathlib
import unittest

from test_downloads import compile_and_run
from test_renderer_contract import method

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/ardia/linux'


class AutoPictureInPictureProfileTests(unittest.TestCase):
    def test_the_preference_reaches_every_regular_profile_and_later_loads(self):
        output = compile_and_run(self, 'auto-pip-preference', [ROOT / 'tests/fixtures/auto_pip_preference_scenarios.cc'])
        self.assertIn('auto pip preference scenarios passed', output)

    def test_the_host_sets_the_preference_for_all_profiles_not_only_the_app_profile(self):
        host = (LINUX / 'ac_linux_host.cc').read_text()
        branch = method(host, '  if (method == "webView.setAutoPictureInPicture") {')
        self.assertIn('ardiacore::SetLinuxAutoPictureInPicture(enabled);', branch)
        self.assertNotIn('GetForProfile(profile())', branch)

    def test_the_service_applies_to_loaded_profiles_and_observes_new_ones(self):
        service = (LINUX / 'ac_linux_auto_pip_service.cc').read_text()
        self.assertIn('public ProfileManagerObserver', service)
        self.assertIn('manager->GetLoadedProfiles()', service)
        added = method(service, '  void OnProfileAdded(Profile* profile) override {')
        self.assertIn('preference_.Added(profile);', added)
        regular = method(service, '  static bool Regular(Profile* profile) {')
        self.assertIn('profile->IsRegularProfile() && !profile->ShutdownStarted()', regular)
        self.assertIn('ContentSettingsType::AUTO_PICTURE_IN_PICTURE', service)
        self.assertIn('OnProfileManagerDestroying', service)
        gn = (LINUX / 'BUILD.gn').read_text()
        for name in ('"ac_linux_auto_pip.h",', '"ac_linux_auto_pip_service.cc",', '"ac_linux_auto_pip_service.h",'):
            self.assertIn(name, gn)


if __name__ == '__main__':
    unittest.main()
