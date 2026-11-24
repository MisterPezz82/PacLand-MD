import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
APP = Path(__file__).resolve().parent
NAME = 'PacLand-ROM-Maker'


def run(*args, **kwargs):
    subprocess.run([str(a) for a in args], cwd=ROOT, check=True, **kwargs)


def icons():
    source = Image.open(APP / 'assets/icon.png').convert('RGBA')
    source.save(APP / 'assets/icon.ico', sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    if sys.platform == 'darwin':
        folder = ROOT / 'build/desktop/icon.iconset'
        folder.mkdir(parents=True, exist_ok=True)
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                suffix = '@2x' if scale == 2 else ''
                source.resize((size * scale, size * scale), Image.Resampling.LANCZOS).save(folder / f'icon_{size}x{size}{suffix}.png')
        run('iconutil', '-c', 'icns', folder, '-o', ROOT / 'build/desktop/icon.icns')


def build(target, assemble=True):
    expected = {'windows-x64': ('win32', ('amd64', 'x86_64')),
                'linux-x64': ('linux', ('x86_64', 'amd64')),
                'macos-arm64': ('darwin', ('arm64', 'aarch64'))}
    system, machines = expected[target]
    if sys.platform != system or platform.machine().lower() not in machines:
        raise RuntimeError('Build this package on its matching native runner: ' + target)
    payload = ROOT / 'build/desktop/payload'
    if assemble:
        sys.path.insert(0, str(APP.parent))
        import portable
        portable.assemble(target, payload, ROOT / 'build/portable-cache')
    if not (payload / 'runtime/portable-manifest.json').is_file():
        raise RuntimeError('Assemble the portable tool bundle first.')
    icons()
    if target == 'windows-x64':
        archive = ROOT / 'build/desktop/payload.tar.gz'
        with tarfile.open(archive, 'w:gz', compresslevel=1) as out:
            def include(info):
                parts = Path(info.name).parts
                return None if '__pycache__' in parts or (parts and parts[0] in ('build', 'dist', 'res', 'reference_roms')) else info
            for child in sorted(payload.iterdir()):
                out.add(child, arcname=child.name, filter=include)
    run(sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
        '--distpath', ROOT / 'dist/desktop', '--workpath', ROOT / 'build/pyinstaller',
        APP / 'desktop.spec')
    if target == 'linux-x64':
        appdir = ROOT / 'build/desktop/AppDir'
        if appdir.exists():
            shutil.rmtree(appdir)
        shutil.copytree(ROOT / f'dist/desktop/{NAME}', appdir / 'usr/bin', symlinks=True)
        shutil.copytree(payload, appdir / 'usr/share/pacland-rom-maker/payload', symlinks=True)
        shutil.copyfile(APP / 'pacland-rom-maker.desktop', appdir / 'pacland-rom-maker.desktop')
        image = Image.open(APP / 'assets/icon.png')
        image.resize((256, 256), Image.Resampling.LANCZOS).save(appdir / 'pacland-rom-maker.png')
        (appdir / 'AppRun').symlink_to('usr/bin/' + NAME)
    print('Application built for ' + target)


def checksums(folder):
    folder = Path(folder)
    lines = []
    for path in sorted(folder.iterdir()):
        if path.suffix.lower() not in ('.exe', '.appimage', '.dmg'):
            continue
        with path.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        lines.append(digest + '  ' + path.name)
    if len(lines) != 3:
        raise RuntimeError('Expected exactly three release packages.')
    (folder / 'SHA256SUMS.txt').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--platform', choices=('windows-x64', 'linux-x64', 'macos-arm64'))
    parser.add_argument('--reuse-payload', action='store_true')
    parser.add_argument('--checksums')
    args = parser.parse_args()
    if args.checksums:
        checksums(args.checksums)
    elif args.platform:
        build(args.platform, not args.reuse_payload)
    else:
        parser.error('--platform or --checksums is required')
