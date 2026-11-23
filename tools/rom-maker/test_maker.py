"""Source packaging/ZIP validation contracts; synthetic data only."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile
import zlib

import maker


class MakerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.data = b'test-owned-synthetic-ROM-data'
        self.rules = {'regions': {'cpu': {'files': [{'name': 'test.rom', 'size': len(self.data),
                      'crc32': f'{zlib.crc32(self.data):08x}', 'sha1': hashlib.sha1(self.data).hexdigest()}]}}}

    def tearDown(self):
        self.tmp.cleanup()

    def archive(self, pairs):
        path = self.root / 'input.zip'
        with zipfile.ZipFile(path, 'w') as out:
            for name, data in pairs:
                out.writestr(name, data)
        return path

    def test_validation_accepts_nested_case_and_never_extracts(self):
        path = self.archive([('Folder/TEST.ROM', self.data), ('../../unsafe.txt', b'ignored')])
        entries, audit = maker.validate_rom(path, self.rules)
        self.assertEqual(entries, {'test.rom': self.data})
        self.assertEqual(audit[0]['sha256'], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(list(self.root.iterdir()), [path])

    def test_validation_rejects_wrong_revision_missing_duplicate_and_size(self):
        cases = [[], [('test.rom', self.data + b'x')], [('test.rom', b'X' * len(self.data))],
                 [('test.rom', self.data), ('folder/test.rom', self.data)]]
        for pairs in cases:
            with self.subTest(pairs=[n for n, _ in pairs]), self.assertRaises(ValueError):
                maker.validate_rom(self.archive(pairs), self.rules)

    def test_package_is_deterministic_and_contains_only_declared_sources(self):
        one, two = self.root / 'one.zip', self.root / 'two.zip'
        with contextlib.redirect_stdout(io.StringIO()):
            maker.package(one)
            maker.package(two)
        self.assertEqual(one.read_bytes(), two.read_bytes())
        with zipfile.ZipFile(one) as archive:
            prefix = 'Pac-Land-MD-ROM-Maker/'
            names = {n[len(prefix):] for n in archive.namelist()}
            for name in names:
                self.assertFalse(name.startswith(('build/', 'dist/', 'res/generated/', 'reference_roms/', 'node_modules/')))
                self.assertFalse(name.endswith(('.bin', '.pkl', '.wav', '.TTF')))
                self.assertNotEqual(name, 'src/recomp_objects.c')
            for name in ('Make-ROM.bat', 'make-rom.sh', 'Setup.bat', 'setup.sh', 'LICENSE', 'LICENSE-SGDK'):
                self.assertIn(name, names)
            for name in ('src/pause.c', 'include/pause.h', 'tools/presentation_lab.mjs',
                         *('tools/presentation-lab/' + p for p in maker.PRESENTATION_FILES)):
                self.assertIn(name, names)
            checks = json.loads(archive.read(prefix + 'SOURCE-SHA256.json'))
            self.assertEqual(set(checks), names - {'SOURCE-SHA256.json'})
            for name, expected in checks.items():
                self.assertEqual(maker.sha(archive.read(prefix + name)), expected)
            patches = json.loads(archive.read(prefix + 'tools/sound/fm_patches.json'))
            self.assertEqual(len(patches), 16)
            for patch in patches.values():
                self.assertLessEqual(set(patch), {'alg', 'fb', 'mul', 'tl', 'trim_db'})
            hints = json.loads(archive.read(prefix + 'tools/rom-maker/access_hints.json'))
            self.assertTrue(hints)
            self.assertTrue(all(0 <= int(site) < 0x80000 and isinstance(role, int) and 0 < role < 8
                                for site, role in hints.items()))
            build = archive.read(prefix + 'tools/build.mjs').decode()
            self.assertIn("process.env.PL_BLAST ??= '1'", build)
            self.assertIn("process.env.PL_BLAST_SHORT ??= '1'", build)
            self.assertIn("process.env.PL_BLAST_WINDOW ??= '1'", build)
            self.assertIn("process.env.PL_PRESENT_PIPELINE ??= 'adaptive'", build)
            self.assertIn('renderer_pause', archive.read(prefix + 'src/renderer.c').decode())


if __name__ == '__main__':
    unittest.main()
