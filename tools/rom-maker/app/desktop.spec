from pathlib import Path
from PyInstaller.building.datastruct import Tree
import sys

app_dir = Path(SPEC).resolve().parent
root = app_dir.parents[2]
build_dir = root / 'build/desktop'
name = 'PacLand-ROM-Maker'
data = [(str(app_dir / 'assets'), 'assets')]
if sys.platform == 'win32':
    data.append((str(build_dir / 'payload.tar.gz'), '.'))
analysis = Analysis([str(app_dir / 'main.py')], pathex=[str(app_dir)], binaries=[],
                    datas=data, hiddenimports=[], hookspath=[], hooksconfig={},
                    runtime_hooks=[], excludes=['tkinter'], noarchive=False)
archive = PYZ(analysis.pure)
options = dict(name=name, debug=False, bootloader_ignore_signals=False,
               strip=False, upx=False, console=False)
if sys.platform == 'win32':
    options['icon'] = str(app_dir / 'assets/icon.ico')
    executable = EXE(archive, analysis.scripts, analysis.binaries, analysis.datas, [], **options)
else:
    if sys.platform == 'darwin':
        options['target_arch'] = 'arm64'
    executable = EXE(archive, analysis.scripts, [], exclude_binaries=True, **options)
    extra = [Tree(str(build_dir / 'payload'), prefix='payload', typecode='DATA')] if sys.platform == 'darwin' else []
    collection = COLLECT(executable, analysis.binaries, analysis.datas, *extra,
                         strip=False, upx=False, name=name)
    if sys.platform == 'darwin':
        bundle = BUNDLE(collection, name=name + '.app',
                        icon=str(build_dir / 'icon.icns'),
                        bundle_identifier='org.paclandmd.rommaker',
                        version='1.0.0',
                        info_plist={'LSMinimumSystemVersion': '14.0',
                                    'NSHighResolutionCapable': True})
