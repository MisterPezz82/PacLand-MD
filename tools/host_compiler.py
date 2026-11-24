import os
import platform
from pathlib import Path
import sys


def command(compiler=None):
    compiler = compiler or os.environ.get('PACLAND_HOST_CC', 'gcc')
    result = [compiler]
    if Path(compiler).stem.lower() == 'zig':
        machine = platform.machine().lower()
        arch = 'aarch64' if machine in ('arm64', 'aarch64') else 'x86_64'
        target = f'{arch}-windows-gnu' if os.name == 'nt' else (
            f'{arch}-macos-none' if sys.platform == 'darwin' else f'{arch}-linux-musl')
        result += ['cc', '-target', target, '-fno-sanitize=all']
        if sys.platform.startswith('linux'):
            result += ['-static']
    return result
