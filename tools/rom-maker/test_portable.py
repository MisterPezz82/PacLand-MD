"""Portable distribution boundaries and offline runtime smoke checks."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

import portable


class PortableTests(unittest.TestCase):
    def test_source_tree_writes_posix_launchers_without_carriage_returns(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            portable.source_tree(root)
            for name in ('make-rom.sh', 'tools/rom-maker/make-rom.sh', 'tools/rom-maker/setup.sh'):
                content = (root / name).read_bytes()
                self.assertTrue(content.startswith(b'#!/bin/sh\n'), name)
                self.assertNotIn(b'\r', content, name)

    def test_archives_exclude_private_outputs_and_preserve_runtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'kit'
            for name in ('runtime/python/bin/python3', 'runtime/zig/zig', 'node_modules/compiler/lib.wasm',
                         'make-rom.sh', 'build/private.bin', 'dist/private.bin', 'res/private.wav',
                         'reference_roms/private.zip', 'tools/__pycache__/private.pyc'):
                p = root / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(b'synthetic')
            for suffix in ('.zip', '.tar.gz'):
                dest = Path(tmp) / ('kit' + suffix)
                with contextlib.redirect_stdout(io.StringIO()):
                    portable.archive(root, dest)
                prefix = 'Pac-Land-MD-ROM-Maker/'
                if suffix == '.zip':
                    with zipfile.ZipFile(dest) as data:
                        contents = {n[len(prefix):]: data.read(n) for n in data.namelist()}
                else:
                    with tarfile.open(dest) as data:
                        contents = {p.name[len(prefix):]: data.extractfile(p).read() for p in data.getmembers()}
                        for name in ('runtime/python/bin/python3', 'runtime/zig/zig', 'make-rom.sh'):
                            self.assertTrue(data.getmember(prefix + name).mode & 0o111)
                expected = {'runtime/python/bin/python3', 'runtime/zig/zig',
                            'node_modules/compiler/lib.wasm', 'make-rom.sh'}
                self.assertEqual(set(contents), expected | {'PORTABLE-SHA256.json'})
                checks = json.loads(contents['PORTABLE-SHA256.json'])
                self.assertEqual(set(checks), expected)
                for name, digest in checks.items():
                    self.assertEqual(hashlib.sha256(contents[name]).hexdigest(), digest)

    def test_python_offline_guard_allows_loopback_and_refuses_outbound(self):
        env = dict(os.environ, PYTHONPATH=str(Path(__file__).parent / 'offline'), PYTHONNOUSERSITE='1')
        code = '''import socket
with socket.socket() as server:
 server.bind(('127.0.0.1', 0)); server.listen()
 with socket.create_connection(server.getsockname(), timeout=2):
  connection, _ = server.accept(); connection.close()
for action in (lambda: socket.getaddrinfo('example.invalid', 443),
               lambda: socket.create_connection(('192.0.2.1', 443), timeout=1)):
 try: action()
 except OSError as error: assert 'blocks external' in str(error), error
 else: raise AssertionError('Outbound connection was allowed')
print('Loopback succeeds; outbound DNS and direct sockets blocked')
'''
        result = subprocess.run([sys.executable, '-c', code], env=env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
