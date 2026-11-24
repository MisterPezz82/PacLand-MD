import io
import shutil
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parent / 'app'))
import runtime


class DesktopTests(unittest.TestCase):
    def test_environment_cannot_inherit_developer_paths(self):
        with patch.dict(os.environ, {'LD_LIBRARY_PATH': '/wrong', 'PYTHONHOME': '/wrong',
                                    'NODE_OPTIONS': '--wrong', 'PL_BLAST': 'wrong', 'RC_TEST': 'wrong'}):
            env = runtime.clean_environment(Path('/bundle'))
        for key in ('LD_LIBRARY_PATH', 'PYTHONHOME', 'NODE_OPTIONS', 'PL_BLAST', 'RC_TEST'):
            self.assertNotIn(key, env)
        self.assertEqual(env['PYTHONPATH'], str(Path('/bundle/tools/rom-maker/offline')))

    def test_cancelled_workspace_does_not_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'source'
            source.mkdir()
            (source / 'tool').write_bytes(b'private test')
            cancel = threading.Event()
            cancel.set()
            with self.assertRaises(runtime.Cancelled):
                runtime.prepare_workspace(source, Path(folder) / 'copy', lambda x: None, cancel)

    def test_payload_archive_cannot_escape_workspace(self):
        with tempfile.TemporaryDirectory() as folder:
            archive = Path(folder) / 'payload.tar.gz'
            with tarfile.open(archive, 'w:gz') as data:
                member = tarfile.TarInfo('../escaped')
                member.size = 1
                data.addfile(member, io.BytesIO(b'x'))
            with self.assertRaises(tarfile.FilterError):
                runtime.prepare_workspace(archive, Path(folder) / 'work', lambda x: None, threading.Event())
            self.assertFalse((Path(folder) / 'escaped').exists())

    def test_process_output_and_failure_are_reported(self):
        messages = []
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaisesRegex(RuntimeError, 'code 7'):
                runtime.run_process([sys.executable, '-c', 'print("diagnostic", flush=True); raise SystemExit(7)'],
                                    Path(folder), messages.append, threading.Event(), 10)
        self.assertIn('diagnostic', messages)

    def test_failed_build_preserves_output_and_removes_workspace(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'payload'
            py = runtime.python_executable(source)
            py.parent.mkdir(parents=True)
            py.write_bytes(b'test')
            output = Path(folder) / 'output'
            output.mkdir()
            old = output / runtime.OUTPUT_FILES[0]
            old.write_bytes(b'existing cartridge')
            roots = []

            def fail(command, root, *args, **kwargs):
                roots.append(root)
                raise RuntimeError('Invalid input')

            with patch.object(runtime, 'payload_location', return_value=source), patch.object(runtime, 'run_process', side_effect=fail), patch.object(runtime.shutil, 'disk_usage', return_value=shutil._ntuple_diskusage(10**12, 0, 10**12)):
                with self.assertRaisesRegex(RuntimeError, 'Invalid input'):
                    runtime.build_rom(['bad.zip'], output, lambda x: None, threading.Event())
            self.assertEqual(old.read_bytes(), b'existing cartridge')
            self.assertTrue(roots)
            self.assertFalse(roots[0].exists())

    def test_successful_build_exports_only_completed_files(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / 'payload'
            py = runtime.python_executable(source)
            py.parent.mkdir(parents=True)
            py.write_bytes(b'test')
            output = Path(folder) / 'output'
            roots = []

            def finish(command, root, *args, **kwargs):
                roots.append(root)
                if '--output' in command:
                    dest = Path(command[command.index('--output') + 1])
                    dest.mkdir(parents=True)
                    for name in runtime.OUTPUT_FILES:
                        (dest / name).write_bytes(b'completed ' + name.encode())
                    (dest / 'private-data').write_bytes(b'private')

            with patch.object(runtime, 'payload_location', return_value=source), patch.object(runtime, 'run_process', side_effect=finish), patch.object(runtime.shutil, 'disk_usage', return_value=shutil._ntuple_diskusage(10**12, 0, 10**12)):
                result = runtime.build_rom(['rom.zip'], output, lambda x: None, threading.Event())
            self.assertEqual(result, str(output / runtime.OUTPUT_FILES[0]))
            self.assertEqual({p.name for p in output.iterdir()}, set(runtime.OUTPUT_FILES))
            self.assertFalse(roots[0].exists())

    def test_cancel_stops_descendant_process(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            heartbeat = root / 'heartbeat'
            child = root / 'child.py'
            child.write_text('import time\nfrom pathlib import Path\np=Path("heartbeat")\nwhile True:\n p.write_text(str(time.time()))\n time.sleep(.03)\n')
            parent = root / 'parent.py'
            parent.write_text('import subprocess,sys,time\nsubprocess.Popen([sys.executable,"child.py"])\nprint("ready",flush=True)\ntime.sleep(30)\n')
            cancel = threading.Event()
            timer = threading.Timer(.8, cancel.set)
            timer.start()
            try:
                with self.assertRaises(runtime.Cancelled):
                    runtime.run_process([sys.executable, parent], root, lambda x: None, cancel, 10)
            finally:
                timer.cancel()
            self.assertTrue(heartbeat.exists())
            stamp = heartbeat.read_text()
            time.sleep(.2)
            self.assertEqual(stamp, heartbeat.read_text())


if __name__ == '__main__':
    unittest.main()
