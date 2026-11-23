"""Native diagnostic compilers; cartridge compilation uses the bundled WASM SDK."""
import os
from pathlib import Path


def command(compiler=None):
    compiler = compiler or os.environ.get('PACLAND_HOST_CC', 'gcc')
    result = [compiler]
    if Path(compiler).stem.lower() == 'zig':
        # Select bundled libc, avoiding Visual Studio or a Linux development SDK.
        result += ['cc', '-target', 'x86_64-windows-gnu' if os.name == 'nt' else 'x86_64-linux-musl',
                   '-fno-sanitize=all']
        if os.name != 'nt':
            result += ['-static']
    return result
