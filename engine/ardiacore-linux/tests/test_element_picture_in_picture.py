from pathlib import Path
import re
import unittest

LINUX = Path(__file__).resolve().parents[1] / 'src/ardia/linux'


def body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class ElementPictureInPictureTests(unittest.TestCase):
    agent = (LINUX / 'renderer/ac_linux_page_agent.cc').read_text()
    channel = (LINUX / 'ac_linux_page_channel.cc').read_text()
    media = (LINUX / 'ac_linux_page_media.cc').read_text()
    mojom = (LINUX / 'mojom/page.mojom').read_text()

    def test_the_agent_answers_two_document_bound_steps(self):
        self.assertIn('PictureInPictureDocument() => (string? document);', self.mojom)
        self.assertIn('EnterPictureInPictureForUser(string document) => (bool opened);', self.mojom)

    def test_candidates_are_shown_usable_videos_with_the_playing_and_largest_first(self):
        scripts = re.sub(r'"\s*\n\s*"', '', self.agent)
        self.assertIn("v.readyState > 0 && !v.disablePictureInPicture && v.clientWidth * v.clientHeight > 0", scripts)
        self.assertIn("(b.paused ? 0 : 1) - (a.paused ? 0 : 1) || b.clientWidth * b.clientHeight - a.clientWidth * a.clientHeight", scripts)
        self.assertIn("document.pictureInPictureElement === videos[0]", scripts)

    def test_the_document_answer_comes_only_from_a_main_frame_with_a_candidate(self):
        answer = body(self.agent, '  void PictureInPictureDocument(PictureInPictureDocumentCallback callback) override {')
        self.assertIn('IsOutermostMainFrame()', answer)
        self.assertIn('HasPictureInPictureCandidate(frame)', answer)
        self.assertIn('std::move(callback).Run(std::nullopt)', answer)

    def test_entering_checks_the_document_grants_activation_once_and_consumes_it(self):
        enter = body(self.agent, '  void EnterPictureInPictureForUser(const std::string& document,')
        self.assertLess(enter.index('Token().ToString() != document'), enter.index('NotifyUserActivation('))
        self.assertLess(enter.index('NotifyUserActivation('), enter.index('RequestExecuteScript('))
        self.assertLess(enter.index('RequestExecuteScript('), enter.index('ConsumeTransientUserActivation()'))
        self.assertIn('ISOLATED_WORLD_ID_CHROME_INTERNAL', enter)
        self.assertIn('blink::mojom::PromiseResultOption::kAwait', enter)

    def test_the_host_enters_only_while_the_pressed_document_is_primary(self):
        start = body(self.channel, 'void LinuxPageChannel::EnterPictureInPictureForUser(')
        self.assertIn('GetWeakDocumentPtr()', start)
        self.assertIn('PictureInPictureDocument(', start)
        answer = body(self.channel, 'void LinuxPageChannel::PictureInPictureDocumentAnswered(')
        self.assertIn('AsRenderFrameHostIfValid()', answer)
        self.assertIn('web_contents()->GetPrimaryMainFrame()', answer)
        self.assertIn('EnterPictureInPictureForUser(*token', answer)

    def test_pages_without_a_session_video_take_the_element_route(self):
        request = body(self.media, 'void LinuxPageMedia::RequestPictureInPicture(')
        self.assertIn('case LinuxPictureInPictureRoute::kElement:', request)
        self.assertIn('EnterPictureInPictureForUser(', request)
        self.assertNotIn('kNoVideo', request)
        answered = body(self.media, 'void LinuxPageMedia::ElementPictureInPictureAnswered(')
        self.assertIn('request != picture_in_picture_request_', answered)


if __name__ == '__main__':
    unittest.main()
