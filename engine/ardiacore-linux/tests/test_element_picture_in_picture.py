import contextlib
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
LINUX = ROOT / 'src/ardia/linux'
PATCH = ROOT.parents[0] / 'patches/chromium-linux-media-player-pip.patch'
CHROMIUM = pathlib.Path.home() / 'ardia-linux-build-156/chromium_git/chromium/src'
FILES = [
    'content/browser/media/media_web_contents_observer.cc',
    'content/browser/media/media_web_contents_observer.h',
    'content/browser/media/session/media_session_controllers_manager.cc',
    'content/browser/media/session/media_session_controllers_manager.h',
]


def body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def committed(name):
    result = subprocess.run(['git', '-C', str(CHROMIUM), 'show', f'HEAD:{name}'], capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else None


@contextlib.contextmanager
def patched_tree():
    sources = {name: committed(name) for name in FILES} if CHROMIUM.exists() else {}
    if not sources or any(text is None for text in sources.values()) or shutil.which('patch') is None:
        raise unittest.SkipTest('Chromium 156 source is unavailable')
    with tempfile.TemporaryDirectory(prefix='ardia-media-player-pip-') as directory:
        for name, text in sources.items():
            target = pathlib.Path(directory) / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        result = subprocess.run(['patch', '-p1', '-F0', '-i', str(PATCH)], cwd=directory, capture_output=True, text=True)
        if result.returncode != 0:
            raise AssertionError(result.stdout + result.stderr)
        yield directory


class ElementPictureInPictureTests(unittest.TestCase):
    def test_nothing_on_linux_grants_a_page_user_activation(self):
        for path in [*LINUX.glob('*.cc'), *LINUX.glob('renderer/*.cc')]:
            self.assertNotIn('NotifyUserActivation', path.read_text(), path.name)
        mojom = (LINUX / 'mojom/page.mojom').read_text()
        self.assertNotIn('EnterPictureInPictureForUser', mojom)
        self.assertNotIn('PictureInPictureDocument', mojom)

    def test_the_patch_requests_pip_for_one_available_player_of_the_primary_main_frame(self):
        series = (ROOT.parents[0] / 'patches/series').read_text()
        self.assertIn('chromium chromium-linux-media-player-pip.patch linux', series)
        text = PATCH.read_text()
        added = '\n'.join(line for line in text.splitlines() if line.startswith('+') and not line.startswith('+++'))
        self.assertNotRegex(added, r'(?m)^\+\s*/\*|^\+\s*//(?! nogncheck)')
        with patched_tree() as directory:
            observer = (pathlib.Path(directory) / FILES[0]).read_text()
            candidates = body(observer, 'MediaWebContentsObserver::ArdiaLinuxPictureInPictureCandidates(')
            self.assertIn('has_video()', candidates)
            self.assertIn('ArdiaLinuxPictureInPictureAvailable(id)', candidates)
            self.assertIn('id.frame_routing_id != frame', candidates)
            request = body(observer, 'bool MediaWebContentsObserver::ArdiaLinuxRequestPictureInPicture(')
            self.assertIn('media_player_remotes_.find(id)', request)
            self.assertIn('RequestEnterPictureInPicture(std::nullopt)', request)
            for name in FILES[1::2]:
                self.assertIn('#if BUILDFLAG(IS_LINUX)', (pathlib.Path(directory) / name).read_text())

    def test_the_patch_lists_main_frame_videos_that_have_played(self):
        with patched_tree() as directory:
            observer = (pathlib.Path(directory) / FILES[0]).read_text()
            self.assertIn('has_played_ = true;', body(observer, 'void SetIsPlaying()'))
            played = body(observer, 'MediaWebContentsObserver::ArdiaLinuxPlayedVideoPlayers(')
            self.assertIn('id.frame_routing_id != frame', played)
            self.assertIn('!info->has_video()', played)
            self.assertIn('!info->has_played()', played)
            self.assertIn('info->is_playing()', played)
            self.assertRegex(observer, r'ArdiaLinuxPlayedVideoPlayers\(\s*WebContents\* contents\) \{')
        hooks = (LINUX / 'ac_linux_content_hooks.h').read_text()
        self.assertIn('ArdiaLinuxPlayedVideoPlayers(WebContents* contents);', hooks)

    def test_the_media_entry_is_seeded_only_from_videos_that_have_played(self):
        media = (LINUX / 'ac_linux_page_media.cc').read_text()
        created = body(media, 'LinuxPageMedia::LinuxPageMedia(')
        self.assertIn('content::ArdiaLinuxPlayedVideoPlayers(contents)', created)
        self.assertNotIn('ArdiaLinuxPictureInPictureCandidates', created)

    def test_the_host_chooses_among_available_players_and_waits_for_the_window(self):
        media = (LINUX / 'ac_linux_page_media.cc').read_text()
        request = body(media, 'void LinuxPageMedia::RequestPictureInPicture(')
        self.assertIn('content::ArdiaLinuxPictureInPictureCandidates(', request)
        self.assertIn('ChooseLinuxPictureInPicturePlayer(', request)
        self.assertIn('content::ArdiaLinuxRequestPictureInPicture(', request)
        self.assertIn('picture_in_picture_timer_.Start(', request)
        self.assertIn('video_sizes_[id] = size', body(media, 'void LinuxPageMedia::MediaResized('))


if __name__ == '__main__':
    unittest.main()
