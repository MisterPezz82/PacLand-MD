import ctypes
import json
import os
from pathlib import Path
import queue
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import threading
import time


APP_NAME = 'Pac-Land MD ROM Maker'
OUTPUT_FILES = ('Pac-Land-MD.bin', 'Pac-Land-MD.bin.sha256', 'build-manifest.json')


class Cancelled(Exception):
    pass


def assets_dir():
    return Path(getattr(sys, '_MEIPASS', Path(__file__).parent)) / 'assets'


def payload_location():
    if getattr(sys, 'frozen', False):
        base = Path(sys._MEIPASS)
        candidates = [base / 'payload', base / 'payload.tar.gz',
                      Path(sys.executable).parents[1] / 'Resources/payload',
                      Path(sys.executable).parents[1] / 'share/pacland-rom-maker/payload']
    else:
        candidates = [Path(__file__).resolve().parents[3] / 'build/desktop/payload']
    for path in candidates:
        if path.exists():
            return path
    raise RuntimeError('The bundled build tools are missing. Download the complete application again.')


def python_executable(root):
    return root / ('runtime/python/python.exe' if os.name == 'nt' else 'runtime/python/bin/python3')


def clean_environment(root):
    env = dict(os.environ)
    for key in list(env):
        if key.startswith(('PL_', 'RC_', 'PYINSTALLER_', '_PYI_')) or key in (
            'PYTHONHOME', 'PYTHONPATH', 'NODE_OPTIONS', 'NODE_PATH', 'LD_LIBRARY_PATH',
            'LD_PRELOAD', 'DYLD_LIBRARY_PATH', 'DYLD_FALLBACK_LIBRARY_PATH',
            'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'SDKROOT', 'CPATH',
            'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'CC', 'CXX'):
            env.pop(key, None)
    env.update(PYTHONNOUSERSITE='1', PYTHONDONTWRITEBYTECODE='1', PYTHONUNBUFFERED='1',
               PYTHONPATH=str(root / 'tools/rom-maker/offline'),
               ZIG_GLOBAL_CACHE_DIR=str(root / 'build/zig-cache'),
               ZIG_LOCAL_CACHE_DIR=str(root / 'build/zig-local'))
    return env


def stop_process(process):
    if os.name == 'nt':
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       creationflags=subprocess.CREATE_NO_WINDOW, timeout=15, check=False)
    else:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        limit = time.monotonic() + 2
        while process.poll() is None and time.monotonic() < limit:
            time.sleep(.05)
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_process(command, root, emit, cancel, timeout=None):
    if cancel.is_set():
        raise Cancelled()
    options = {'creationflags': subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt' else {'start_new_session': True}
    if os.name == 'nt':
        ctypes.windll.kernel32.SetDllDirectoryW(None)
    try:
        process = subprocess.Popen([str(v) for v in command], cwd=root, env=clean_environment(root),
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, encoding='utf-8', errors='replace', **options)
    finally:
        if os.name == 'nt' and getattr(sys, 'frozen', False):
            ctypes.windll.kernel32.SetDllDirectoryW(str(sys._MEIPASS))
    lines = queue.Queue()

    def read():
        try:
            for line in process.stdout:
                lines.put(line.rstrip('\r\n'))
        finally:
            lines.put(None)

    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    started = time.monotonic()
    try:
        while True:
            if cancel.is_set():
                raise Cancelled()
            if timeout and time.monotonic() - started > timeout:
                raise RuntimeError('A bundled tool exceeded its startup test time limit.')
            try:
                line = lines.get(timeout=.1)
            except queue.Empty:
                continue
            if line is None:
                break
            emit(line)
        code = process.wait(timeout=10)
        if code:
            raise RuntimeError(f'Build tool exited with code {code}. See the build details below.')
    finally:
        if process.poll() is None or os.name != 'nt':
            stop_process(process)
        process.wait(timeout=15)
        reader.join(timeout=5)
        process.stdout.close()


def prepare_workspace(source, destination, emit, cancel):
    emit('Preparing a temporary workspace. No tools are installed on your computer.')
    if source.is_file():
        with tarfile.open(source, 'r:gz') as archive:
            for member in archive:
                if cancel.is_set():
                    raise Cancelled()
                archive.extract(member, destination, filter='data')
    else:
        def copy(src, dst):
            if cancel.is_set():
                raise Cancelled()
            return shutil.copy2(src, dst)
        shutil.copytree(source, destination, dirs_exist_ok=True, symlinks=False, copy_function=copy)
    if not python_executable(destination).is_file():
        raise RuntimeError('The bundled Python runtime is incomplete.')
    (destination / 'build').mkdir(exist_ok=True)


def export_diagnostics(root, emit):
    logs = sorted((root / 'build/rom-maker').glob('*/maker.log'))
    servers = sorted((root / 'build/rom-maker').glob('*/server.log'))
    for path in logs[-1:] + servers[-1:]:
        emit('\n' + path.name + '\n' + path.read_text(encoding='utf-8', errors='replace')[-64000:])


def build_rom(roms, output, emit, cancel, jobs=4):
    source = payload_location()
    with tempfile.TemporaryDirectory(prefix='pacland-md-') as folder:
        root = Path(folder) / 'maker'
        try:
            if shutil.disk_usage(folder).free < 4 * 1024**3:
                raise RuntimeError('At least 4 GB of free temporary disk space is required.')
            prepare_workspace(source, root, emit, cancel)
            command = [python_executable(root), root / 'tools/rom-maker/maker.py']
            emit('Checking your arcade ROM set...')
            run_process([*command, *roms, '--validate-only'], root, emit, cancel, timeout=120)
            emit('Checking bundled build tools...')
            run_process([*command, '--check'], root, emit, cancel, timeout=120)
            result = root / 'build/desktop-output'
            run_process([*command, *roms, '--output', result, '--jobs', str(jobs)], root, emit, cancel)
            if cancel.is_set():
                raise Cancelled()
            output = Path(output)
            output.mkdir(parents=True, exist_ok=True)
            for name in OUTPUT_FILES:
                if not (result / name).is_file():
                    raise RuntimeError('The build did not produce all expected output files.')
            with tempfile.TemporaryDirectory(prefix='.pacland-output-', dir=output) as staging:
                for name in OUTPUT_FILES:
                    shutil.copyfile(result / name, Path(staging) / name)
                for name in OUTPUT_FILES:
                    (Path(staging) / name).replace(output / name)
            emit('Saved: ' + str(output / OUTPUT_FILES[0]))
        except Exception:
            export_diagnostics(root, emit)
            raise
        finally:
            emit('Removing temporary build files...')
    return str(output / OUTPUT_FILES[0])


def self_test(report=None):
    messages = []
    cancel = threading.Event()
    with tempfile.TemporaryDirectory(prefix='pacland-test-') as folder:
        root = Path(folder) / 'maker'
        prepare_workspace(payload_location(), root, messages.append, cancel)
        py = python_executable(root)
        node = root / ('runtime/node/node.exe' if os.name == 'nt' else 'runtime/node/bin/node')
        zig = root / ('runtime/zig/zig.exe' if os.name == 'nt' else 'runtime/zig/zig')
        run_process([py, root / 'tools/rom-maker/maker.py', '--check'], root, messages.append, cancel, 120)
        code = root / 'build/hello.c'
        code.write_text('#include <stdio.h>\nint main(void) { puts("Native compiler OK"); return 0; }\n')
        binary = root / ('build/hello.exe' if os.name == 'nt' else 'build/hello')
        native_test = root / 'build/native-smoke.py'
        native_test.write_text('import subprocess,sys\nfrom pathlib import Path\nsys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))\nfrom host_compiler import command\nsubprocess.run([*command(sys.argv[1]),sys.argv[2],"-o",sys.argv[3]],check=True)\nsubprocess.run([sys.argv[3]],check=True)\n')
        run_process([py, native_test, zig, code, binary], root, messages.append, cancel, 300)
        run_process([py, '-m', 'unittest', 'discover', '-s', 'tools/rom-maker', '-p', 'test_*.py'],
                    root, messages.append, cancel, 120)
        run_process([node, root / 'tools/build_z80.mjs'], root, messages.append, cancel, 180)
        smoke = root / 'build/toolchain-smoke.mjs'
        smoke.write_text('''import { createRequire } from 'node:module';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
const req = createRequire(path.resolve('package.json'));
const base = path.dirname(req.resolve('romdevtools/package.json'));
const { runCc1m68k } = await import(pathToFileURL(path.join(base, 'src/toolchains/m68k-elf-gcc/gcc.js')).href);
const result = await runCc1m68k({source:'int add(int a, int b) { return a+b; }', headers:{}, options:['-O2']});
if (result.exitCode || !result.asmSource) throw Error(result.log || '68000 compiler failed');
console.log('68000 compiler OK');
''')
        run_process([node, smoke], root, messages.append, cancel, 180)
    result = {'ok': True, 'platform': sys.platform, 'checks': messages}
    if report:
        Path(report).write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result
