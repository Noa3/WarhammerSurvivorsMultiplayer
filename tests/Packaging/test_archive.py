"""Archive format and rejection tests; fixtures are bytes, not runtime binaries."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/archive.ps1'
SCRATCH = os.environ.get('WSM_TEST_SCRATCH')

class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=SCRATCH)
        self.root = Path(self.temp.name)
        self.src = self.root / 'input'
        self.src.mkdir()
        self.output = self.root / 'out'
        self.data = {'lobby/Wsm.App.exe': b'fixture', 'licenses/runtime/LICENSE': b'license'}
        for name, data in self.data.items():
            path = self.src / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.manifest = {'target': 'win-x64', 'selfContainedDotNet': True, 'playableMultiplayer': False,
                         'files': [dict(path=k, bytes=len(v), sha256=hashlib.sha256(v).hexdigest()) for k,v in self.data.items()]}
        self.save()
    def tearDown(self):
        self.temp.cleanup()
    def save(self):
        (self.src / 'package-manifest.json').write_text(json.dumps(self.manifest), encoding='utf-8')
    def run_builder(self):
        return subprocess.run(['pwsh', '-NoProfile', '-File', str(SCRIPT), '-InputDirectory', str(self.src), '-OutputDirectory', str(self.output)], text=True, encoding='utf-8', errors='replace', capture_output=True)
    def test_archives_extract_and_map(self):
        result = self.run_builder()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.output / 'WSM-portable-win-x64.zip') as portable, zipfile.ZipFile(self.output / 'WSM-vortex-win-x64.zip') as vortex:
            for name, data in self.data.items():
                self.assertEqual(portable.read('WSM/' + name), data)
                self.assertEqual(vortex.read('payload/' + name), data)
            root = ET.fromstring(vortex.read('fomod/ModuleConfig.xml'))
            folder = root.find('./requiredInstallFiles/folder')
            self.assertEqual(folder.attrib['source'], 'payload')
            self.assertEqual(folder.attrib['destination'], 'WSM')
            self.assertEqual(set(n.removeprefix('WSM/') for n in portable.namelist()), set(n.removeprefix('payload/') for n in vortex.namelist() if n.startswith('payload/')))

    def test_ingame_entry_point_is_preserved_exactly(self):
        name = 'Start-WSM.cmd'
        data = (ROOT / 'tools/package-templates/Start-WSM.cmd').read_bytes()
        (self.src / name).write_bytes(data)
        self.manifest['inGameConnectionUi'] = True
        self.manifest['files'].append(dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        self.save()
        result = self.run_builder()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.output / 'WSM-portable-win-x64.zip') as archive:
            self.assertEqual(archive.read('WSM/' + name), data)

    def test_rejects_tampering_and_extra_files(self):
        for kind in ('hash', 'size', 'extra', 'missing', 'case', 'duplicate', 'bool'):
            with self.subTest(kind=kind):
                if kind == 'hash': self.manifest['files'][0]['sha256'] = '0' * 64
                elif kind == 'size': self.manifest['files'][0]['bytes'] += 1
                elif kind == 'extra': (self.src / 'lobby/extra.dll').write_bytes(b'extra')
                elif kind == 'missing': (self.src / 'lobby/Wsm.App.exe').unlink()
                elif kind == 'case': self.manifest['files'][0]['path'] = 'LOBBY/Wsm.App.exe'
                elif kind == 'duplicate': self.manifest['files'].append(dict(self.manifest['files'][0], path='LOBBY/Wsm.App.exe'))
                elif kind == 'bool': self.manifest['playableMultiplayer'] = 'false'
                self.save()
                result = self.run_builder()
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse(self.output.exists())
                self.tearDown()
                self.setUp()

    def test_rejects_unsafe_or_private_paths(self):
        for name in ('../outside', '/absolute', 'lobby/../evil', 'lobby/x:stream',
                     'lobby/CON.txt', 'lobby/hello.', 'lobby/GameAssembly.dll',
                     'lobby/UnityPlayer.dll', 'lobby/WarhammerSurvivors.exe',
                     'lobby/global-metadata.dat', 'lobby/interop/generated.dll',
                     'lobby/.local/private', 'lobby/.idea/workspace.xml', 'lobby/execplan.md', 'lobby/credentials.json',
                     'lobby/secret.pfx', 'lobby/debug.pdb', 'lobby/nested.zip'):
            with self.subTest(name=name):
                self.manifest['files'][0]['path'] = name
                self.save()
                result = self.run_builder()
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertRegex(result.stdout + result.stderr, r'Unsafe|Excluded|Unexpected')
                self.assertNotIn('Unexpected file or case mismatch', result.stdout + result.stderr)
                self.assertFalse(self.output.exists())

    def test_rejects_duplicate_json_keys(self):
        text = (self.src / 'package-manifest.json').read_text(encoding='utf-8')
        text = text.replace('"target": "win-x64"', '"target": "other", "target": "win-x64"')
        (self.src / 'package-manifest.json').write_text(text, encoding='utf-8')
        result = self.run_builder()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.output.exists())

    def test_existing_output_is_never_modified(self):
        self.output.mkdir()
        marker = self.output / 'existing'
        marker.write_bytes(b'keep')
        self.assertNotEqual(self.run_builder().returncode, 0)
        self.assertEqual(marker.read_bytes(), b'keep')

    def test_junction_is_rejected(self):
        target = self.root / 'target'
        target.mkdir()
        (target / 'file.dll').write_bytes(b'private')
        junction = self.src / 'lobby/link'
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(target)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(self.run_builder().returncode, 0)
        self.assertFalse(self.output.exists())
        junction.rmdir()

if __name__ == '__main__':
    unittest.main()
