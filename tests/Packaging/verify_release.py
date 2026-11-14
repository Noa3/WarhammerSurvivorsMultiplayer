"""Extract and verify the real release pair; no game or runtime execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile
import zipfile
import xml.etree.ElementTree as ET


def verify(archive, expected, prefix, destination):
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        assert len(names) == len(set(n.casefold() for n in names)), 'Duplicate ZIP entries'
        for name in names:
            parts = PurePosixPath(name).parts
            assert not name.startswith('/') and '\\' not in name and '..' not in parts
        assert z.testzip() is None, 'ZIP CRC failure'
        payload = {prefix + name for name in expected} | {prefix + 'package-manifest.json', prefix + 'README.txt'}
        extras = {'fomod/ModuleConfig.xml', 'fomod/info.xml'} if prefix == 'payload/' else set()
        assert set(names) == payload | extras, 'Unexpected archive entry set'
        z.extractall(destination)
        extracted = destination / prefix
        assert json.loads((extracted / 'package-manifest.json').read_text(encoding='utf-8-sig'))['files'] == list(expected.values())
        for name, meta in expected.items():
            data = (extracted / name).read_bytes()
            assert len(data) == meta['bytes']
            assert hashlib.sha256(data).hexdigest() == meta['sha256'], name
        if extras:
            root = ET.fromstring((destination / 'fomod/ModuleConfig.xml').read_bytes())
            mappings = root.findall('./requiredInstallFiles/folder')
            assert len(mappings) == 1 and mappings[0].attrib == {'source': 'payload', 'destination': 'WSM'}
            assert not root.findall('./requiredInstallFiles/file')
            # The extension's published rule detects a fomod folder and config;
            # model its resulting source-to-destination mapping for every file.
            mapped = {'WSM/' + n[len('payload/'):] for n in names if n.startswith('payload/')}
            assert mapped == {'WSM/' + n[len(prefix):] for n in payload}
        return len(names)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('input_dir', type=Path)
    parser.add_argument('output_dir', type=Path)
    args = parser.parse_args()
    manifest = json.loads((args.input_dir / 'package-manifest.json').read_text(encoding='utf-8-sig'))
    expected = {entry['path']: entry for entry in manifest['files']}
    assert len(expected) == len(manifest['files'])
    with tempfile.TemporaryDirectory(dir=os.environ.get('WSM_TEST_SCRATCH')) as temp:
        root = Path(temp)
        for variant, prefix in [('portable', 'WSM/'), ('vortex', 'payload/')]:
            dest = root / variant
            dest.mkdir()
            archive = args.output_dir / f'WSM-{variant}-win-x64.zip'
            count = verify(archive, expected, prefix, dest)
            print(f'{archive}: {count} entries extracted; {len(expected)} manifest files match')

if __name__ == '__main__':
    main()
