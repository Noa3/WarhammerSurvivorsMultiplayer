"""Real portable-package integrity and safe entry-point regressions; never starts the game."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / 'tools/package-templates/Start-WSM.cmd'
PACKAGE = Path(os.environ['WSM_PACKAGE_TEST_DIR']) if os.environ.get('WSM_PACKAGE_TEST_DIR') else None


class EntryPointTests(unittest.TestCase):
    def test_template_exists_and_declines_by_default(self):
        self.assertTrue(TEMPLATE.is_file(), 'Missing extract-and-run entry point')
        text = TEMPLATE.read_text(encoding='utf-8').lower()
        for fragment in ('disabledelayedexpansion', 'steam cloud', 'saves', 'choice', '/d n', 'wsm.launcher.exe', 'play', 'wsm.bridge.dll', 'localappdata'):
            self.assertIn(fragment, text)

    def test_blank_game_directory_exits_without_launch(self):
        self.assertTrue(TEMPLATE.is_file(), 'Missing extract-and-run entry point')
        result = subprocess.run(['cmd.exe', '/d', '/c', str(TEMPLATE)], input='\n', text=True, capture_output=True, timeout=15)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertNotIn('Owned game PID:', result.stdout)


@unittest.skipUnless(PACKAGE, 'Set WSM_PACKAGE_TEST_DIR to the actual completed package')
class DistributionTests(unittest.TestCase):
    def test_manifest_exactly_covers_all_files_and_hashes(self):
        manifest = json.loads((PACKAGE / 'package-manifest.json').read_text(encoding='utf-8-sig'))
        self.assertIs(manifest['inGameConnectionUi'], True)
        self.assertIs(manifest['playableMultiplayer'], False)
        self.assertIs(manifest['selfContainedDotNet'], True)
        entries = manifest['files']
        paths = [x['path'] for x in entries]
        self.assertEqual(len(paths), len(set(x.casefold() for x in paths)))
        actual = {x.relative_to(PACKAGE).as_posix() for x in PACKAGE.rglob('*') if x.is_file()} - {'package-manifest.json'}
        self.assertEqual(set(paths), actual)
        self.assertTrue({'Start-WSM.cmd', 'bridge/Wsm.Bridge.dll', 'bridge/Wsm.InGame.Runtime.dll', 'launcher/Wsm.Launcher.exe', 'lobby/Wsm.App.exe'} <= actual)
        self.assertTrue(any(x.startswith('licenses/') and '/LICENSE' in x for x in actual))
        self.assertTrue(any(x.startswith('licenses/Microsoft.NETCore.App.Runtime.NativeAOT.win-x64/') and '/LICENSE' in x for x in actual), 'Native AOT runtime pack license missing')
        forbidden = {'gameassembly.dll', 'unityplayer.dll', 'globals-metadata.dat', 'global-metadata.dat', 'warhammersurvivors.exe', 'melonloader.dll'}
        for item in entries:
            name = item['path']
            self.assertNotIn(Path(name).name.casefold(), forbidden)
            self.assertNotIn(Path(name).suffix.lower(), {'.pdb', '.lib', '.log'})
            data = (PACKAGE / name).read_bytes()
            self.assertEqual(len(data), item['bytes'], name)
            self.assertEqual(hashlib.sha256(data).hexdigest(), item['sha256'], name)
        self.assertEqual((PACKAGE / 'Start-WSM.cmd').read_bytes(), TEMPLATE.read_bytes())
        self.assertTrue((PACKAGE / 'bridge/Wsm.InGame.Runtime.dll').read_bytes().startswith(b'MZ'))

    def test_builder_refuses_existing_destination(self):
        before = (PACKAGE / 'package-manifest.json').read_bytes()
        result = subprocess.run(['pwsh', '-NoProfile', '-File', str(ROOT / 'tools/package.ps1'), '-OutputDirectory', str(PACKAGE)], capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Output already exists', result.stdout + result.stderr)
        self.assertEqual(before, (PACKAGE / 'package-manifest.json').read_bytes())

    def test_launcher_receives_untrusted_path_as_data(self):
        # Exercise the same fixed PowerShell command with the real published launcher,
        # but point it at a non-existent directory; never launch a game.
        line = next(x for x in TEMPLATE.read_text(encoding='utf-8').splitlines() if x.startswith('powershell.exe '))
        script = line.split(' -Command ', 1)[1]
        self.assertTrue(script.startswith('"') and script.endswith('"'))
        game = str(PACKAGE / 'nonexistent & echo WSM_INJECTION_TEST!')
        env = os.environ.copy()
        env.update(WSM_ROOT=str(PACKAGE), WSM_GAME=game)
        result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script[1:-1]], env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=30)
        combined = result.stdout + result.stderr
        self.assertEqual(result.returncode, 1, combined)
        self.assertIn('WSM:', combined)
        self.assertIn('nonexistent & echo WSM_INJECTION_TEST!', combined)
        self.assertNotIn('\nWSM_INJECTION_TEST!', combined)
        self.assertNotIn('Owned game PID:', combined)


if __name__ == '__main__':
    unittest.main()
