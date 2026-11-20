"""Real portable-package integrity and safe entry-point regressions; never starts the game."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / 'tools/package-templates/Start-WSM.cmd'
PACKAGE = Path(os.environ['WSM_PACKAGE_TEST_DIR']) if os.environ.get('WSM_PACKAGE_TEST_DIR') else None


def run_start(directory, keys, **environment):
    # Fixed command source even when the package or user input contains cmd metacharacters.
    return subprocess.run(['cmd.exe', '/d', '/v:off', '/c', 'Start-WSM.cmd'], cwd=directory,
                          env={**os.environ, **environment}, input=keys, capture_output=True,
                          text=True, errors='replace', timeout=15)


class EntryPointTests(unittest.TestCase):
    def test_template_exists_and_declines_by_default(self):
        self.assertTrue(TEMPLATE.is_file(), 'Missing extract-and-run entry point')
        text = TEMPLATE.read_text(encoding='utf-8').lower()
        self.assertIn('/c yn /n /d n /t 60', text)

    def test_no_or_no_consent_exits_before_launcher(self):
        for keys in ('N', '\n', ''):
            with self.subTest(keys=keys):
                result = run_start(TEMPLATE.parent, keys, WSM_GAME='invalid')
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn('Continue with non-isolated saves and Steam Cloud?', result.stdout)
                self.assertNotIn('Launcher exited', result.stdout)
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
        game = str(PACKAGE.resolve() / 'nonexistent %PATH% ^ & echo WSM_INJECTION_TEST!')
        result = run_start(PACKAGE, 'Y', WSM_GAME=game, WSM_STEAM_ROOT='invalid')
        combined = result.stdout + result.stderr
        self.assertEqual(result.returncode, 1, combined)
        self.assertIn('WSM:', combined)
        self.assertIn(game, combined)
        self.assertNotIn('\nWSM_INJECTION_TEST!', combined)
        self.assertNotIn('Owned game PID:', combined)

    def test_no_game_after_consent_cancels(self):
        result = run_start(PACKAGE, 'Y', WSM_GAME='', WSM_STEAM_ROOT='invalid')
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn('Game not found in Steam', result.stdout)
        self.assertNotIn('Owned game PID:', result.stdout)

    def test_alternate_library_detected_without_accepting_target(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            apps = root / 'steamapps'
            apps.mkdir()
            library = root / 'Games & 100% !'
            game = library / 'steamapps/common/Warhammer Survivors'
            metadata = game / 'WarhammerSurvivors_Data/il2cpp_data/Metadata/global-metadata.dat'
            metadata.parent.mkdir(parents=True)
            for file in (metadata, game/'WarhammerSurvivors.exe', game/'UnityPlayer.dll', game/'GameAssembly.dll'):
                file.touch()  # No executable in this fixture.
            (apps/'libraryfolders.vdf').write_text('"libraryfolders"\n{\n"1"\n{\n"path" ' +
                                                  json.dumps(str(library), ensure_ascii=False) + '\n}\n}', encoding='utf-8')
            result = run_start(PACKAGE, 'Y', WSM_GAME='', WSM_STEAM_ROOT=str(root))  # EOF at selection.
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn('Steam game folder: ' + str(game), result.stdout)
            self.assertNotIn('Owned game PID:', result.stdout)


if __name__ == '__main__':
    unittest.main()
