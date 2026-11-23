"""Maintainer packaging: assemble pinned, offline Windows/Linux x64 builders.

Network access is used only while assembling the distribution. Make-ROM never
downloads or installs dependencies. Third-party archives retain their notices.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.parse
import urllib.request
import zipfile
import time
import maker

LOCK = json.loads((Path(__file__).with_name('portable-runtimes.json')).read_text())


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(asset, cache):
    path = cache / urllib.parse.unquote(asset['url'].rsplit('/', 1)[-1])
    if not path.exists() or digest(path) != asset['sha256']:
        part = path.with_suffix(path.suffix + '.part')
        request = urllib.request.Request(asset['url'], headers={'User-Agent': 'PacLand-ROM-Maker/1.0'})
        with urllib.request.urlopen(request, timeout=120) as response, part.open('wb') as stream:
            shutil.copyfileobj(response, stream)
        if digest(part) != asset['sha256']:
            raise RuntimeError('Runtime archive checksum mismatch: ' + path.name)
        part.replace(path)
    return path


def unpack(archive, destination, sha):
    marker = destination / '.archive-sha256'
    if marker.exists() and marker.read_text().strip() == sha:
        return
    if destination.exists():
        raise RuntimeError('Refusing to overwrite an unrelated runtime directory: ' + str(destination))
    scratch = destination.with_name(destination.name + '-extract')
    scratch.mkdir(parents=True)
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as data:
            data.extractall(scratch)
    else:
        with tarfile.open(archive) as data:
            data.extractall(scratch, filter='data')
    children = list(scratch.iterdir())
    if len(children) != 1 or not children[0].is_dir():
        raise RuntimeError('Runtime archive has no unique root directory: ' + str(archive))
    for attempt in range(10):
        try:
            children[0].rename(destination)
            break
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(.5)  # Windows scanner can briefly hold extracted headers.
    scratch.rmdir()
    marker.write_text(sha + '\n')


def linux_path(value):
    value = str(value)
    if re.match(r'^[A-Za-z]:[\\/]', value):
        return '/mnt/' + value[0].lower() + '/' + value[3:].replace('\\', '/')
    return value


def run(platform, argv, cwd):
    if platform == 'linux-x64' and os.name == 'nt':
        argv = ['wsl', '--cd', linux_path(cwd), '-e', *map(linux_path, argv)]
    subprocess.run([str(a) for a in argv], cwd=cwd, check=True)


def source_tree(root):
    for name, data in maker.source_files():
        target = root / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (root / 'Make-ROM.bat').write_bytes(b'@echo off\r\ncall "%~dp0tools\\rom-maker\\Make-ROM.bat" %*\r\n')
    (root / 'make-rom.sh').write_text('#!/bin/sh\nexec sh "$(dirname "$0")/tools/rom-maker/make-rom.sh" "$@"\n', newline='\n')


def assemble(platform, root, cache):
    root.mkdir(parents=True, exist_ok=True)
    cache.mkdir(parents=True, exist_ok=True)
    source_tree(root)
    spec = LOCK['platforms'][platform]
    runtime = root / 'runtime'
    runtime.mkdir(exist_ok=True)
    for tool in ('python', 'node', 'zig'):
        item = spec[tool]
        print('Bundling ' + tool + ' ' + item['version'], flush=True)
        unpack(download(item, cache), runtime / tool, item['sha256'])
    py = runtime / 'python' / ('python.exe' if platform == 'windows-x64' else 'bin/python3')
    node = runtime / 'node' / ('node.exe' if platform == 'windows-x64' else 'bin/node')
    zig = runtime / 'zig' / ('zig.exe' if platform == 'windows-x64' else 'zig')
    site = runtime / 'python' / ('Lib/site-packages' if platform == 'windows-x64' else 'lib/python3.13/site-packages')
    site.mkdir(parents=True, exist_ok=True)
    # Pinned wheels are importable directly; no pip executable or install step
    # is required on the recipient's machine. Preserve dist-info/licenses/.libs.
    for item in spec['wheels']:
        with zipfile.ZipFile(download(item, cache)) as wheel:
            for entry in wheel.infolist():
                installed = site / entry.filename
                # Re-packaging must not rewrite unchanged DLLs that an active
                # local verification build has loaded on Windows.
                if not entry.is_dir() and installed.is_file() and installed.read_bytes() == wheel.read(entry):
                    continue
                wheel.extract(entry, site)
    npm = (runtime / 'node' / 'node_modules/npm/bin/npm-cli.js' if platform == 'windows-x64'
           else runtime / 'node/lib/node_modules/npm/bin/npm-cli.js')
    npm_stamp = runtime / '.npm-lock-sha256'
    lock_sha = digest(root / 'package-lock.json')
    if not npm_stamp.exists() or npm_stamp.read_text().strip() != lock_sha:
        run(platform, [node, '--use-system-ca', npm, 'ci', '--ignore-scripts', '--no-audit', '--no-fund',
                       '--cache', cache / 'npm'], root)
        npm_stamp.write_text(lock_sha + '\n')
    manifest = {'platform': platform, 'runtimes': spec, 'npm_lock_sha256': lock_sha,
                'reference_rom_sha256': maker.REFERENCE_SHA256,
                'policy': 'No game assets included; no downloads during a normal ROM build.'}
    (runtime / 'portable-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    readme = f'''PAC-LAND MD — PORTABLE ROM MAKER ({platform})

Extract the entire package into a writable folder. Keep runtime and node_modules
beside Make-ROM. No Python, Node, GCC, Visual Studio, pip or npm installation is
needed. A normal build works offline and never downloads ROMs or dependencies.

Windows x64: drop your own complete pacland.zip onto Make-ROM.bat, or double-click
it and enter the ROM path. Linux x64: sh make-rom.sh /path/to/pacland.zip

Linux requires an x86_64 Linux system with glibc 2.28 or newer; Windows
requires 64-bit Windows 10/11. These are separate packages, not ARM/macOS builds.
Allow several GB of free disk space and time for the first full build. Progress
and diagnostic logs stay in this folder. No administrator access is required.

The supported MAME parent-set members are listed in tools/rom_layout.json.
Japanese artwork is found automatically in a merged set or a paclandj.zip beside
the parent; --japan can point to it elsewhere.
--output changes the private output directory. Runtime integrity can be checked
with Make-ROM.bat --check, or sh make-rom.sh --check.

Build output is under build/rom-maker-output. Share this original portable
package, not your private build folder or generated cartridge. Arcade ROMs,
graphics, music and translated game program are not bundled.

Includes adaptive presentation, Trip1-5 selection and Namco menu lettering,
the pause window, Exit to Title and the selected waka1 boot sound.
Title Start opens arcade Trip selection: Left/Right and A/C navigate; Jump
(B by default) or Start confirms; Up returns. With any-button jump, any ABC
button confirms instead of navigating.
Options Start opens the same selector. The last trip is saved. With trip shuffle,
the selected actual trip comes first, followed by the seeded remaining order.
Scene loads reveal after a complete first field; the original trip card remains.
Title restarts hide partial redraws, also with demos disabled. The new scene
transition correction awaits console play-testing. With demos disabled, only the
title/high-score sequence plays: recorded level flashes are removed and the
Start/B prompt stays visible.
The earlier title-DMA wait correction was confirmed stable on the user's console;
this scoped result does not certify every console.
Column-DMA stride is corrected across ROM bank boundaries. Springboards launch
slightly higher by design; ordinary jumps, maps and timed airborne taps remain
unchanged. Both large-queen animation groups keep consistent scene colours.
Forest transparency is completely rolled back for performance; no ghost masks,
overlays, tagging or clipping remain. The large queen scene palette is corrected;
small-fairy art is unchanged. Other queen shades may be nearest-colour matches.
New console testing and bundled-tool corresponding-source/licensing review are
pending. Packaging verification and notices are not legal clearance.
This remains a development build with occasional missed frames. See LICENSE,
LICENSE-SGDK, COPYING-SGDK-RUNTIME and THIRD-PARTY-NOTICES.txt. Developer source
instructions and optional re-profiling are in docs/source-build.md; ordinary
portable builds do not use MAME, WSL, system compilers or setup scripts.
'''
    original_guide = (root / 'docs/rom-maker.md').read_bytes()
    (root / 'docs/source-build.md').write_bytes(original_guide)
    (root / 'docs/rom-maker.md').write_text(readme, encoding='utf-8')
    (root / 'START-HERE.txt').write_text(readme, encoding='utf-8')
    notices = '''Bundled third-party runtimes and build tools

CPython portable builds: https://github.com/astral-sh/python-build-standalone
Python licences and component notices are retained under runtime/python.
Node.js: https://nodejs.org/ — licence retained under runtime/node/LICENSE.
Zig / Clang / libc: https://ziglang.org/ — runtime/zig/LICENSE and bundled notices.
Pillow and NumPy: licences retained in their site-packages dist-info directories.
romdevtools and locked npm dependencies: package.json, LICENSE/COPYING and source
files remain under node_modules. The full pinned dependency list, versions and
declared licences are in package-lock.json. SDK notices are at the archive root.

Original runtime download URLs and SHA256 hashes: runtime/portable-manifest.json.
This package grants no rights to arcade ROMs or locally generated game assets.
'''
    (root / 'THIRD-PARTY-NOTICES.txt').write_text(notices)
    run(platform, [py, '-c', 'import PIL,numpy,sys;print(sys.version);print(PIL.__version__,numpy.__version__)'], root)
    run(platform, [node, '--version'], root)
    run(platform, [zig, 'version'], root)
    print('Portable runtime assembly complete: ' + str(root), flush=True)


def archive(root, output):
    root, output = root.resolve(), output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    excluded = {'build', 'dist', 'reference_roms', 'res', '.venv', '__pycache__'}
    files = []
    for directory, folders, names in os.walk(root):
        # Prune private build trees before visiting them. Do not follow any
        # directory aliases into a developer's shared dependency directories.
        folders[:] = [n for n in folders if n != '__pycache__' and
                      not (Path(directory) == root and n in excluded)]
        files.extend(Path(directory) / n for n in names if not n.endswith('.pyc'))
    # Absolute/external links must never pull private working files into a kit.
    for p in files:
        if p.is_symlink() and not p.resolve().is_relative_to(root):
            raise RuntimeError('External distribution link refused: ' + str(p))
    checks = {p.relative_to(root).as_posix(): digest(p) for p in files
              if p.name != 'PORTABLE-SHA256.json'}
    inventory = root / 'PORTABLE-SHA256.json'
    inventory.write_text(json.dumps(checks, indent=2) + '\n')
    if inventory not in files:
        files.append(inventory)
    prefix = 'Pac-Land-MD-ROM-Maker'
    if output.suffix == '.zip':
        with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as out:
            for p in sorted(files):
                info = zipfile.ZipInfo(prefix + '/' + p.relative_to(root).as_posix(), (2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o755 if p.suffix == '.sh' else 0o644) << 16
                out.writestr(info, p.read_bytes())
    else:
        with tarfile.open(output, 'w:gz', compresslevel=6) as out:
            for p in sorted(files):
                info = out.gettarinfo(str(p), prefix + '/' + p.relative_to(root).as_posix())
                info.mtime = 0;info.uid = info.gid = 0;info.uname = info.gname = ''
                rel = p.relative_to(root).as_posix()
                info.mode = 0o755 if p.suffix == '.sh' or '/bin/' in rel or rel == 'runtime/zig/zig' else 0o644
                if info.isfile():
                    with p.open('rb') as stream:
                        out.addfile(info, stream)
                else:
                    out.addfile(info)
    output.with_suffix(output.suffix + '.sha256').write_text(digest(output) + '  ' + output.name + '\n')
    print('Portable distribution: ' + str(output), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=LOCK['platforms'], required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--cache', type=Path, default=maker.ROOT / 'build/portable-cache')
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--archive-only', action='store_true')
    args = parser.parse_args()
    if not args.archive_only:
        assemble(args.platform, args.root.resolve(), args.cache.resolve())
    if args.archive:
        if args.platform == 'linux-x64' and os.name == 'nt':
            # npm creates WSL symlinks that Win32 stat cannot follow. Let the
            # Linux runtime preserve and validate them with Linux semantics.
            run(args.platform, [args.root.resolve() / 'runtime/python/bin/python3',
                                Path(__file__).resolve(), '--platform', args.platform,
                                '--root', args.root.resolve(), '--archive', args.archive.resolve(),
                                '--archive-only'], maker.ROOT)
        else:
            archive(args.root, args.archive)
