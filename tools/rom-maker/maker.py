"""Local source-only Pac-Land MD maker. Never uploads ROMs or uses paid APIs."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
REFERENCE_SHA256 = '68404d45d2db59210f9c46141d74c25df461c307f4f09600a213a28af57e6c80'
# Explicit source allowlist: do not walk private build/reference/asset folders.
TOOLS = '''build.mjs build_z80.mjs compile_blast.mjs romdev.mjs windows-import-hook.mjs
extract_arcade.py extract_levels.py convert_palettes.py convert_graphics.py
palette_pack.py prepare_native.py prepare_recomp.py prepare_boot.py prepare_fairy.py
prepare_door.py prepare_sprite_extras.py prepare_region_art.py prepare_player_variants.py prepare_overlay_masks.py boot_logo.txt
prepare_blast_atlas.py blast_inventory.c blast_lab.mjs recomp6809.py recomp_profile.py
closure6809.py m6809.py sim6809.py rom_layout.json capture_reference.py capture_reference.lua
prepare_hud.py prepare_title.py presentation_lab.mjs host_compiler.py'''.split()
PRESENTATION_FILES = 'adaptive.c packet.c packet_lease.c probe.c cold_bridge.s'.split()
SOUND = 'capture_tracks.py mcu.py m6801.py cosim.py cus30.py music.py fmfit.py'.split()
MAKER_FILES = '''maker.py capture_wsl.py Make-ROM.bat make-rom.sh Setup.bat setup.sh
requirements.txt test_maker.py test_portable.py coverage_host.py pack_palettes.py access_hints.json portable.py portable-runtimes.json
offline/sitecustomize.py offline/network.mjs'''.split()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source_files(root=ROOT):
    paths = ['main.c', 'LICENSE', 'LICENSE-SGDK', 'COPYING-SGDK-RUNTIME', 'package.json', 'package-lock.json',
             'docs/rom-maker.md', 'tests/recomp_host.c']
    paths += ['tools/' + p for p in TOOLS]
    paths += ['tools/presentation-lab/' + p for p in PRESENTATION_FILES]
    paths += ['tools/sound/' + p for p in SOUND]
    paths += [p.relative_to(root).as_posix() for p in (root / 'include').glob('*.h')]
    paths += [p.relative_to(root).as_posix() for p in (root / 'src').rglob('*')
              if p.suffix in ('.c', '.h', '.s') and p.name != 'recomp_objects.c']
    paths += ['tools/rom-maker/' + name for name in MAKER_FILES]
    for name in sorted(set(paths)):
        data = (root / name).read_bytes()
        if name == 'tools/build.mjs':
            text = data.decode('utf-8')
            text = re.sub(r"(?ms)^if\(process\.env\.PL_HW_PALETTE==='1'\)\{\r?\n.*?^\}\r?\n", '', text)
            if 'PL_HW_PALETTE' in text:
                raise RuntimeError('Private hardware diagnostic escaped public build filtering')
            data = text.encode('utf-8')
        yield name, data
    # Ship synthesizer settings, not captured waveforms, spectra, or music streams.
    patches = json.loads((root / 'tools/sound/fm_patches.json').read_text())
    patches = {k: {f: v[f] for f in ('alg', 'fb', 'mul', 'tl', 'trim_db') if f in v}
               for k, v in patches.items()}
    yield 'tools/sound/fm_patches.json', (json.dumps(patches, indent=2) + '\n').encode()


def package(destination):
    contents = dict(source_files())
    # Friendly launchers also live at the archive root.
    contents['Make-ROM.bat'] = b'@echo off\r\ncall "%~dp0tools\\rom-maker\\Make-ROM.bat" %*\r\n'
    contents['make-rom.sh'] = b'#!/bin/sh\nexec sh "$(dirname "$0")/tools/rom-maker/make-rom.sh" "$@"\n'
    contents['Setup.bat'] = b'@echo off\r\ncall "%~dp0tools\\rom-maker\\Setup.bat" %*\r\n'
    contents['setup.sh'] = b'#!/bin/sh\nexec sh "$(dirname "$0")/tools/rom-maker/setup.sh" "$@"\n'
    contents['START-HERE.txt'] = (
        'PAC-LAND MD SOURCE ROM MAKER\n\n'
        'Read docs/rom-maker.md first. Dependencies are required.\n'
        'Windows: install requirements, run Setup.bat, then drop your own pacland ZIP on Make-ROM.bat.\n'
        'Linux: install requirements, run sh setup.sh, then sh make-rom.sh /path/to/pacland.zip.\n'
        'No arcade ROM, generated game assets or complete game ROM is included.\n'
        'The game remains an incomplete development build.\n').encode()
    contents['SOURCE-SHA256.json'] = (json.dumps({n: sha(b) for n, b in sorted(contents.items())}, indent=2) + '\n').encode()
    destination = Path(destination).resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(contents.items()):
            info = zipfile.ZipInfo('Pac-Land-MD-ROM-Maker/' + name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if name.endswith('.sh') else 0o644) << 16
            archive.writestr(info, data)
    digest = sha(destination.read_bytes())
    destination.with_suffix('.zip.sha256').write_text(digest + '  ' + destination.name + '\n')
    print(f'Source-only kit: {destination}\nSHA256: {digest}', flush=True)


def validate_rom(rom, rules=None):
    """Read only declared small members; never extract an untrusted ZIP tree."""
    rules = rules or json.loads((ROOT / 'tools/rom_layout.json').read_text())
    required = [e for region in rules['regions'].values() for e in region['files']]
    found, audit = {}, []
    with zipfile.ZipFile(rom) as archive:
        by_name = {}
        for entry in archive.infolist():
            key = entry.filename.replace('\\', '/').rsplit('/', 1)[-1].lower()
            by_name.setdefault(key, []).append(entry)
        errors = []
        for spec in required:
            name = spec['name']
            entries = by_name.get(name.lower(), [])
            if len(entries) != 1:
                errors.append(f'{name}: ' + ('missing' if not entries else 'ambiguous duplicate filename'))
                continue
            member = entries[0]
            if member.file_size != spec['size'] or member.flag_bits & 1:
                errors.append(f'{name}: wrong size or encrypted member')
                continue
            data = archive.read(member)
            if f'{zlib.crc32(data):08x}' != spec['crc32'] or hashlib.sha1(data).hexdigest() != spec['sha1']:
                errors.append(f'{name}: wrong revision or damaged data (CRC/SHA1 mismatch)')
                continue
            found[name] = data
            audit.append({'name': name, 'size': len(data), 'sha256': sha(data)})
        if errors:
            raise ValueError('Unsupported/incomplete Pac-Land parent set:\n  ' + '\n  '.join(errors))
    return found, audit


JAPAN = [('pl1_9b.6f', 0x80768A87), ('pl1_8.6e', 0x2B20E46D),
         ('pl1_10b.7e', 0xFFD9D66E), ('pl1_11.7f', 0xC59775D8)]


def validate_japan(path):
    with zipfile.ZipFile(path) as archive:
        by_name = {}
        for member in archive.infolist():
            by_name.setdefault(member.filename.replace('\\', '/').rsplit('/', 1)[-1].lower(), []).append(member)
        out = {}
        for name, crc in JAPAN:
            entries = by_name.get(name, [])
            if len(entries) != 1 or entries[0].file_size != 0x4000:
                raise ValueError('Missing, duplicate or wrong-sized Japanese graphics member: ' + name)
            data = archive.read(entries[0])
            if zlib.crc32(data) != crc:
                raise ValueError('Wrong Japanese graphics revision: ' + name)
            out[name] = data
    return out


def split_paths(text):
    """Turn typed, pasted or dragged text into paths. Dragging several files
    gives them separated by spaces, quoted only when they contain spaces, so
    unquoted pieces are re-joined until they name something that exists."""
    text = text.strip()
    if not text:
        return []
    pieces = re.findall(r'"([^"]*)"|(\S+)', text)
    tokens = [q if q else w for q, w in pieces]
    paths, current = [], ''
    for token in tokens:
        current = token if not current else current + ' ' + token
        if Path(current).expanduser().exists():
            paths.append(current)
            current = ''
    if current:
        paths.append(current)       # reported as not found later
    return paths


def _archive_list(rom, japan=None):
    """Every ZIP given, every ZIP in a folder given, plus other pacland*.zip
    beside a given ZIP: split and non-merged MAME sets keep parent and clones
    apart. Accepts one path or several."""
    items = [rom] if isinstance(rom, (str, Path)) else list(rom)
    if japan:
        items.append(japan)
    paths = []
    for item in items:
        p = Path(item).expanduser().resolve()
        if p.suffix.lower() == '.7z':
            raise ValueError(f'{p.name}: 7-Zip archives are not supported. Re-pack the set as a .zip (MAME reads both).')
        if p.is_dir():
            found = sorted(q for q in p.iterdir() if q.suffix.lower() == '.zip')
            if not found:
                raise ValueError(f'No .zip files found in {p}')
            paths += found
        elif p.is_file():
            paths += [p] + sorted(q for q in p.parent.glob('*.zip')
                                  if q != p and q.name.lower().startswith('pacland'))
        else:
            raise ValueError(f'Not found: {p}\nCheck the file exists. You can drag one or more ZIPs, or the folder they are in.')
    unique = []
    for p in paths:
        if p not in unique:
            unique.append(p)
    return unique


def locate_inputs(rom, japan=None, rules=None, say=None):
    """Find every required file by size and checksum, whatever the ZIP or file
    names, so merged, split and non-merged MAME sets all work. Returns the
    parent members, an audit and the optional Japanese artwork (or None)."""
    say = say or (lambda line: print(line, flush=True))
    rules = rules or json.loads((ROOT / 'tools/rom_layout.json').read_text())
    required = [e for region in rules['regions'].values() for e in region['files']]
    wanted = {(e['size'], int(e['crc32'], 16)) for e in required} | {(0x4000, crc) for _, crc in JAPAN}
    names = {e['name'].lower() for e in required} | {n for n, _ in JAPAN}
    archives = _archive_list(rom, japan)
    index, same_name, unreadable = {}, {}, []
    for path in archives:
        try:
            archive = zipfile.ZipFile(path)
        except zipfile.BadZipFile:
            unreadable.append(path.name)
            continue
        for member in archive.infolist():
            if member.is_dir() or member.flag_bits & 1:
                continue
            key = (member.file_size, member.CRC)
            if key in wanted:
                index.setdefault(key, []).append((path, member))
            base = member.filename.replace('\\', '/').rsplit('/', 1)[-1].lower()
            if base in names:
                same_name.setdefault(base, path.name)
    opened = {}

    def read(path, member):
        if path not in opened:
            opened[path] = zipfile.ZipFile(path)
        return opened[path].read(member)

    def fetch(size, crc, sha1=None):
        for path, member in index.get((size, crc), []):
            data = read(path, member)
            if zlib.crc32(data) == crc and (sha1 is None or hashlib.sha1(data).hexdigest() == sha1):
                return data, path.name
        return None, None

    found, audit, missing, sources = {}, [], [], set()
    for spec in required:
        data, source = fetch(spec['size'], int(spec['crc32'], 16), spec['sha1'])
        if data is None:
            missing.append(spec)
            continue
        found[spec['name']] = data
        sources.add(source)
        audit.append({'name': spec['name'], 'size': len(data), 'sha256': sha(data), 'found_in': source})
    japanese, jsources = {}, set()
    for name, crc in JAPAN:
        data, source = fetch(0x4000, crc)
        if data is not None:
            japanese[name] = data
            jsources.add(source)
    for archive in opened.values():
        archive.close()

    say('Looking for Pac-Land ROM files by checksum (ZIP and file names do not matter).')
    say('  Searched: ' + ', '.join(p.name for p in archives))
    if unreadable:
        say('  Skipped (not a readable ZIP): ' + ', '.join(unreadable))
    if missing:
        lines = [f'Pac-Land World parent set incomplete: {len(found)} of {len(required)} required files found.',
                 "The maker needs the files of MAME's 'pacland' parent set (Namco, World).", 'Missing:']
        for spec in missing:
            note = ''
            if spec['name'].lower() in same_name:
                note = f"  (a file with this name is in {same_name[spec['name'].lower()]}, but it is a different revision or damaged)"
            lines.append(f"  {spec['name']}  CRC {spec['crc32']}{note}")
        lines += ['What to do:',
                  '  - Merged or non-merged pacland.zip: it should contain all of these; check it is the full parent set.',
                  '  - Split set: put the parent pacland.zip in the same folder as the clone ZIP (for example paclandj.zip);',
                  '    every pacland*.zip in that folder is searched automatically. You can also give the folder itself.',
                  '  - A Japanese or Midway clone ZIP on its own is not enough: those use different program ROMs.']
        raise ValueError('\n'.join(lines))
    say(f"  World parent 'pacland': all {len(required)} files found in " + ', '.join(sorted(sources)))
    if len(japanese) == len(JAPAN):
        kind = 'merged set' if jsources <= sources else 'separate clone ZIP'
        say(f"  Japanese artwork 'paclandj': all {len(JAPAN)} files found in " + ', '.join(sorted(jsources)) +
            f' ({kind}). Japanese character option enabled.')
        return found, audit, japanese
    if japanese:
        detail = f'only {len(japanese)} of {len(JAPAN)} files found ({", ".join(sorted(jsources))})'
    else:
        detail = 'not found'
    if japan:
        raise ValueError(f'Japanese artwork requested with --japan, but {detail}. Expected: ' +
                         ', '.join(f'{n} CRC {c:08x}' for n, c in JAPAN))
    say(f"  Japanese artwork 'paclandj' (optional): {detail}. The Japanese character option will use the World artwork.")
    say('    To enable it, use a merged pacland.zip or put paclandj.zip in the same folder.')
    return found, audit, None


def write_zip(path, entries):
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in sorted(entries.items()):
            archive.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data)


def command_version(cmd):
    return subprocess.check_output(cmd, text=True, stderr=subprocess.STDOUT).strip()


def preflight(args):
    portable = (ROOT / 'runtime/portable-manifest.json').exists()
    if portable and args.reprofile:
        raise RuntimeError('Portable builds use canonical source coverage. MAME diagnostics are available only in the developer source edition.')
    if sys.version_info < (3, 12):
        raise RuntimeError('Python 3.12 or newer is required (3.13 tested).')
    try:
        import PIL
        import numpy
        if int(PIL.__version__.split('.')[0]) < 10:
            raise ImportError('Pillow >=10 required')
    except ImportError as error:
        raise RuntimeError('Bundled Python libraries are missing. Extract the complete portable package again.' if portable else
                           'Run Setup.bat / sh setup.sh first (Pillow and NumPy required).') from error
    node = shutil.which(args.node)
    gcc = shutil.which(args.gcc)
    if not node or not gcc:
        raise RuntimeError('Bundled Node or compiler is missing. Extract the complete portable package again.' if portable else
                           'Node.js 24+ and native GCC must be on PATH (or use --node / --gcc). See docs/rom-maker.md.')
    node, gcc = str(Path(node).resolve()), str(Path(gcc).resolve())
    node_version = command_version([node, '--version'])
    if int(node_version.lstrip('v').split('.')[0]) < 24:
        raise RuntimeError('Node.js 24 or newer required. Found ' + node_version)
    if not (ROOT / 'node_modules/romdevtools/src/mcp/server.js').exists():
        raise RuntimeError('Bundled build libraries are missing. Extract the complete portable package again.' if portable else
                           'romdevtools is missing: run Setup.bat / sh setup.sh (npm ci --ignore-scripts).')
    if args.reprofile and os.name == 'nt':
        command_version(['wsl', '-e', 'python3', '--version'])
        command_version(['wsl', '-e', args.mame, '-help'])
        command_version(['wsl', '-e', 'xvfb-run', '--help'])
    elif args.reprofile:
        command_version([args.mame, '-help'])
        command_version(['xvfb-run', '--help'])
    compiler_version = command_version([gcc, 'version' if Path(gcc).stem.lower() == 'zig' else '--version']).splitlines()[0]
    if Path(gcc).stem.lower() == 'zig':
        compiler_version = 'Zig ' + compiler_version
    return node, gcc, {'python': sys.version.split()[0], 'pillow': PIL.__version__, 'numpy': numpy.__version__,
                       'node': node_version, 'gcc': compiler_version,
                       'platform': sys.platform}


def run_build(args):
    print('Checking your own ROM files (nothing is uploaded)...', flush=True)
    entries, audit, japanese = locate_inputs(args.rom, args.japan)
    if args.font:
        print('Note: --font is no longer needed; the boot logo is built in. Ignoring it.', flush=True)
    node, gcc, versions = preflight(args)
    print('Dependencies ready. Regenerating all game assets; this can take many minutes.', flush=True)
    stage = ROOT / 'build/rom-maker' / time.strftime('run-%Y%m%d-%H%M%S')
    stage.mkdir(parents=True)  # Never reuse stale generated data.
    sources = dict(source_files())
    for name, data in sources.items():
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (stage / 'build').mkdir()
    write_zip(stage / 'build/roms/pacland.zip', entries)
    if japanese:
        write_zip(stage / 'reference_roms/paclandj.zip', japanese)
    env = dict(os.environ)
    # Ignore development switches inherited from an unrelated shell.
    for key in list(env):
        if key.startswith('PL_') or key.startswith('RC_'):
            del env[key]
    env.update(PACLAND_ZIP=str(stage / 'build/roms/pacland.zip'), PYTHON=sys.executable, PACLAND_HOST_CC=gcc,
               PYTHONUNBUFFERED='1', PYTHONHASHSEED='0', PL_BLAST='1', PL_BLAST_SHORT='1', PL_BLAST_WINDOW='1')
    env.update(ZIG_GLOBAL_CACHE_DIR=str(stage / 'build/zig-cache'),
               ZIG_LOCAL_CACHE_DIR=str(stage / 'build/zig-local'), PYTHONDONTWRITEBYTECODE='1')
    env['PATH'] = str(Path(gcc).parent) + os.pathsep + env.get('PATH', '')
    if os.name == 'nt':
        env['NODE_OPTIONS'] = '--import=' + (stage / 'tools/windows-import-hook.mjs').as_uri()
    else:
        env.pop('NODE_OPTIONS', None)
    if (ROOT / 'runtime/portable-manifest.json').exists():
        env['NODE_OPTIONS'] = (env.get('NODE_OPTIONS', '') + ' --import=' +
                               (ROOT / 'tools/rom-maker/offline/network.mjs').as_uri()).strip()
        env['PYTHONPATH'] = str(ROOT / 'tools/rom-maker/offline')
        env['PYTHONNOUSERSITE'] = '1'
        for key in ('PYTHONHOME', 'NODE_PATH', 'CC', 'CXX', 'CPATH', 'C_INCLUDE_PATH',
                    'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'COMPILER_PATH', 'GCC_EXEC_PREFIX',
                    'SDKROOT', 'INCLUDE', 'LIB'):
            env.pop(key, None)
        env['ZIG_LIB_DIR'] = str(Path(gcc).parent / 'lib')
    log_path = stage / 'maker.log'
    print('Private working folder: ' + str(stage), flush=True)
    with log_path.open('w', encoding='utf-8') as log:
        def run(label, command):
            print('\n' + label, flush=True)
            log.write('\n' + label + '\n'); log.flush()
            result = subprocess.run([str(c) for c in command], cwd=stage, env=env, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                tail = '\n'.join(log_path.read_text(encoding='utf-8', errors='replace').splitlines()[-14:])
                raise RuntimeError(f'{label} failed (exit {result.returncode}).\n{tail}\nFull local log: {log_path}')

        def py(label, script, *rest):
            run(label, [sys.executable, 'tools/' + script, *rest])

        py('[1/10] Decoding validated graphics and palettes', 'extract_arcade.py', '--rom', 'build/roms/pacland.zip')
        py('[2/10] Extracting all 32 round definitions', 'extract_levels.py', '--rom', 'build/roms/pacland.zip')
        if args.reprofile:
            capture = ['tools/capture_reference.py', '--romdir', 'build/roms', '--out', 'res/generated/reference', '--mame', args.mame]
            if os.name == 'nt':
                linux = command_version(['wsl', '-e', 'wslpath', '-u', str(stage)])
                run('[3/10] Capturing fresh reference RAM for diagnostics (WSL MAME)',
                    ['wsl', '-e', 'python3', linux + '/tools/rom-maker/capture_wsl.py', linux, args.mame])
            else:
                run('[3/10] Capturing fresh reference RAM for diagnostics (MAME)', [sys.executable, *capture])
        else:
            print('[3/10] Using canonical source-owned memory-access hints (no saved states)', flush=True)
            hints = stage / 'res/generated/recomp/access_profile.json'
            hints.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(stage / 'tools/rom-maker/access_hints.json', hints)
        py('[4/10] Packing fresh scenery palettes in parallel (slow on a clean build)',
           'rom-maker/pack_palettes.py', '--jobs', str(args.jobs))
        py('Preparing native scenery from the fresh palette searches', 'prepare_native.py')
        if args.reprofile:
            py('[5/10] Profiling arcade object logic (diagnostic variant)', 'recomp_profile.py')
        py('Regenerating translated C', 'recomp6809.py')
        run('Compiling the deterministic object coverage harness',
            [sys.executable, 'tools/rom-maker/coverage_host.py', gcc])
        exe = 'build/recomp-coverage.exe' if os.name == 'nt' else 'build/recomp-coverage'
        py('[6/10] Preparing object palettes and sprite coverage', 'prepare_recomp.py', '--host', str(stage / exe),
           '--deterministic-coverage')
        py('[7/10] Preparing arcade HUD', 'prepare_hud.py')
        py('Capturing and converting the arcade title sequence', 'prepare_title.py')
        py('[8/10] Capturing sound requests from your MCU ROMs', 'sound/capture_tracks.py', 'build/sound',
           *[f'{i:02X}' for i in range(0x85, 0xA5)])
        py('Encoding music for the Z80 sound player', 'sound/music.py')
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0)); port = listener.getsockname()[1]
        env.update(PORT=str(port), HOST='127.0.0.1', ROMDEV_URL=f'http://127.0.0.1:{port}')
        server_log = (stage / 'server.log').open('w', encoding='utf-8')
        # Supply the toolchain's startup flags directly: otherwise its re-exec
        # wrapper leaves a child server alive when Windows terminates the wrapper.
        server = subprocess.Popen([node, '--experimental-vm-modules', '--no-wasm-code-gc',
                                   str(ROOT / 'node_modules/romdevtools/src/mcp/server.js')],
                                  cwd=stage, env=env, stdout=server_log, stderr=subprocess.STDOUT,
                                  creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        try:
            for _ in range(480):
                if server.poll() is not None:
                    raise RuntimeError('Local build server stopped; see ' + str(stage / 'server.log'))
                try:
                    urllib.request.urlopen(env['ROMDEV_URL'], timeout=1).close()
                    break
                except urllib.error.HTTPError:
                    break  # A 404 also means the local server is ready.
                except (urllib.error.URLError, TimeoutError):
                    time.sleep(.25)
            else:
                raise RuntimeError('Timed out starting local build server; see ' + str(stage / 'server.log'))
            run('[9/10] Compiling the Mega Drive cartridge locally', [node, 'tools/build.mjs'])
        finally:
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait()
            server_log.close()
    rom = (stage / 'dist/Pac-Land-MD.bin').read_bytes()
    if len(rom) < 0x200 or rom[0x100:0x110] != b'SEGA MEGA DRIVE ':
        raise RuntimeError('Compiler output is not a recognised Mega Drive cartridge.')
    checksum = sum(int.from_bytes(rom[i:i+2], 'big') for i in range(0x200, len(rom), 2)) & 0xFFFF
    if int.from_bytes(rom[0x18e:0x190], 'big') != checksum:
        raise RuntimeError('Generated Mega Drive header checksum failed.')
    digest = sha(rom)
    if args.expect_sha256 and digest != args.expect_sha256.lower():
        raise RuntimeError(f'Output differs from requested SHA256: {digest}. Private result retained at {stage / "dist"}')
    output = Path(args.output).expanduser().resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'Pac-Land-MD.bin').write_bytes(rom)
    (output / 'Pac-Land-MD.bin.sha256').write_text(digest + '  Pac-Land-MD.bin\n')
    result = {'rom': 'Pac-Land-MD.bin', 'sha256': digest, 'bytes': len(rom), 'md_checksum': f'{checksum:04x}',
              'matches_private_reference': digest == REFERENCE_SHA256, 'versions': versions,
              'parent_members': audit, 'japanese_art': bool(japanese),
              'deterministic_coverage_wait_stops': log_path.read_text(encoding='utf-8').count('Deterministic coverage stop:'),
              'source_sha256': {n: sha(b) for n, b in sources.items()},
              'notice': 'Locally generated arcade-derived cartridge. Source licence does not cover original game content.'}
    (output / 'build-manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'\n[10/10] Complete: {output / "Pac-Land-MD.bin"}\nBytes: {len(rom)}\nSHA256: {digest}', flush=True)
    print('Matches private reference build: ' + str(digest == REFERENCE_SHA256), flush=True)
    print('Keep output and build/rom-maker private; the source ZIP is the shareable artifact.', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom', nargs='*', help='Your own MAME Pac-Land ZIP(s) (merged, split or non-merged) or a folder of ZIPs')
    p.add_argument('--japan', help='Optional: paclandj ZIP elsewhere (found automatically in a merged set or beside the parent)')
    p.add_argument('--font', help=argparse.SUPPRESS)  # obsolete: the boot logo is built in
    p.add_argument('--output', default=str(ROOT / 'build/rom-maker-output'), help='Private output folder')
    portable = ROOT / 'runtime/portable-manifest.json'
    node_tool = ROOT / ('runtime/node/node.exe' if os.name == 'nt' else 'runtime/node/bin/node')
    compiler_tool = ROOT / ('runtime/zig/zig.exe' if os.name == 'nt' else 'runtime/zig/zig')
    p.add_argument('--node', default=str(node_tool) if portable.exists() else os.environ.get('ROM_MAKER_NODE', 'node'))
    p.add_argument('--gcc', default=str(compiler_tool) if portable.exists() else 'gcc')
    p.add_argument('--jobs', type=int, default=min(8, max(1, (os.cpu_count() or 2) // 2)),
                   help='Parallel cold palette searches (default up to 8)')
    p.add_argument('--mame', default='/usr/games/mame', help='Linux/WSL MAME executable')
    p.add_argument('--expect-sha256', help='Fail unless the complete cartridge matches this SHA256')
    p.add_argument('--validate-only', action='store_true', help='Audit ZIP without build dependencies or writing assets')
    p.add_argument('--check', action='store_true', help='Check build dependencies only')
    p.add_argument('--reprofile', action='store_true', help='Diagnostic variant: recapture MAME RAM and measure new access hints')
    p.add_argument('--package', metavar='ZIP', help='Create a deterministic source-only distribution ZIP')
    args = p.parse_args()
    try:
        if args.package:
            package(args.package)
        elif args.check:
            print(json.dumps(preflight(args)[2], indent=2))
        else:
            if not args.rom:
                args.rom = split_paths(input('Drag your Pac-Land ROM ZIP(s) (merged, split or non-merged) or their folder into this window, then press Enter: '))
                if not args.rom:
                    raise ValueError('No ROM ZIP given.')
            if args.validate_only:
                _, audit, _ = locate_inputs(args.rom, args.japan)
                print(f'Validated {len(audit)} required parent ROM members. No assets written.')
            else:
                run_build(args)
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile, subprocess.SubprocessError) as error:
        print('\nROM MAKER ERROR: ' + str(error), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('\nCancelled. Private working files were retained under build/rom-maker.', file=sys.stderr)
        return 130
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
