"""Build a deterministically bounded host, without altering cartridge C.

Some arcade waits spin inside a translated frame forever. Legacy generation
samples the host until a three-second timeout. The maker instead budgets two
million translated instructions per completed frame: a stalled run exits with
only its completed RAM snapshots, consistently on fast and slow machines.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from host_compiler import command as host_compiler


def build(root, gcc):
    root = Path(root)
    source = (root / 'src/recomp_objects.c').read_text()
    pattern = r'(?m)^((?:L[0-9A-F]{4}:)?)(/\*[0-9A-F]{4}(?: |:))'
    instrumented, count = re.subn(pattern, r'\1maker_coverage_tick();\2', source)
    if count < 1000:
        raise RuntimeError('Translated instruction format changed; coverage instrumentation refused')
    path = root / 'build/recomp_coverage.c'
    path.write_text('extern void maker_coverage_tick(void);\n' + instrumented)
    harness = (root / 'tests/recomp_host.c').read_text()
    old = 'static void dump(FILE *f) {'
    if harness.count(old) != 1:
        raise RuntimeError('Host dump format changed; coverage instrumentation refused')
    harness = harness.replace(old, old + ' maker_coverage_left=2000000;')
    prefix = '''#include <stdlib.h>
#include <stdio.h>
static unsigned long maker_coverage_left=2000000;
void maker_coverage_tick(void) {
    if (--maker_coverage_left == 0) {
        fputs("Deterministic coverage stop: arcade wait exceeded instruction budget\\n", stderr);
        exit(0);
    }
}
'''
    host = root / 'build/coverage_host.c'
    host.write_text(prefix + harness)
    exe = root / ('build/recomp-coverage.exe' if os.name == 'nt' else 'build/recomp-coverage')
    compiler = host_compiler(gcc)
    subprocess.run([*compiler, '-DRC_HOST', '-O1', '-w', '-iquote', str(root / 'include'), str(path),
                    str(root / 'src/recomp_rt.c'), str(host), '-o', str(exe)], cwd=root, check=True)
    print(f'Coverage host: instrumented {count} translated instruction sites', flush=True)
    return exe


if __name__ == '__main__':
    build(Path(__file__).resolve().parents[2], sys.argv[1] if len(sys.argv) > 1 else 'gcc')
