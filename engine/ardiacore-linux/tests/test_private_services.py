import pathlib
import re
import unittest

from test_downloads import compile_and_run
from test_renderer_contract import method

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/ardia/linux'
REPO = ROOT.parents[1]


def private_calls():
    header = (LINUX / 'ac_linux_private_services.h').read_text()
    table = header[header.index('kPrivateSessionCalls[] = {'):]
    table = table[:table.index('};')]
    return dict(re.findall(r'\{"([^"]+)", "([^"]+)"\}', table))


class PrivateServiceHelperTests(unittest.TestCase):
    def test_routing_and_engine_failures(self):
        output = compile_and_run(self, 'private-services', [ROOT / 'tests/fixtures/private_services_scenarios.cc'])
        self.assertIn('private services scenarios passed', output)

    def test_private_session_calls_match_the_macos_bridge_policy(self):
        bridge = (REPO / 'packages/ardiacore/ios/ArdiaEngineBridge.mm').read_text()
        policy = bridge[bridge.index('PrivateSessionPolicy() {'):]
        policy = policy[:policy.index('};')]
        served = set(re.findall(r'@"(ac_[a-z_]+)" : kServesPrivateSession', policy))
        self.assertTrue(served)
        self.assertEqual(set(private_calls().values()), served)

    def test_every_private_service_is_marked_off_the_record_capable_in_the_engine(self):
        engine = ROOT.parents[0] / 'chromium/src/chrome/browser/ardia'
        sources = '\n'.join(path.read_text() for path in engine.glob('ac_*.cc'))
        for service in private_calls().values():
            start = sources.index(f'AC_ENGINE_CALL({service})')
            self.assertIn('OffTheRecord::kAllow', sources[start:start + 400], service)

    def test_zoom_routes_a_hosted_private_session_through_the_engine_off_the_record_call(self):
        host = (LINUX / 'ac_linux_host.cc').read_text()
        request = method(host, '  void Request(const base::ListValue& arguments) {')
        zoom = request[request.index('if (ardiacore::IsLinuxZoomDomainRoute(name)) {'):]
        zoom = zoom[:zoom.index('\n      }\n') + 8]
        self.assertIn('ardiacore::linux_private_services::RoutePrivateCall(name,', zoom)
        self.assertIn('page && host_->HostsProfile(profile)', zoom)
        self.assertIn('page && page->ShutdownStarted()', zoom)
        self.assertIn('prepared->args.Set(ardiacore::linux_private_services::kOffTheRecordKey, true);', zoom)
        self.assertIn('const std::string path = page->GetOriginalProfile()->GetPath().value();', zoom)
        self.assertLess(zoom.index('page->GetOriginalProfile()'), zoom.index('ResolveLinuxProfile(profile,'))
        generic = request[request.index('const auto* route = ardiacore::linux_contract::FindRoute(name);'):]
        generic = generic[:generic.index('ardiacore::ResolveLinuxProfile(profile,')]
        self.assertIn('ProfileError::kPrivateService', generic)

    def test_engine_errors_name_the_private_session_failure(self):
        host = (LINUX / 'ac_linux_host.cc').read_text()
        replied = method(host, '  static void Replied(void* context, const char* json) {')
        self.assertIn('ardiacore::linux_private_services::EngineFailure(', replied)
        controls = (LINUX / 'ac_linux_site_controls.cc').read_text()
        complete = method(controls, 'std::variant<base::Value, Failure> CompleteServiceCall(')
        self.assertIn('ardiacore::linux_private_services::EngineFailure(', complete)

    def test_gn_lists_the_helper(self):
        self.assertIn('"ac_linux_private_services.h",', (LINUX / 'BUILD.gn').read_text())


if __name__ == '__main__':
    unittest.main()
