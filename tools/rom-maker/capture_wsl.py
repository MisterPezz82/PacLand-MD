"""Run Linux capture from its staging directory; arguments are never shell code."""
import os
import subprocess
import sys

os.chdir(sys.argv[1])
subprocess.run([sys.executable, 'tools/capture_reference.py', '--romdir', 'build/roms',
                '--out', 'res/generated/reference', '--mame', sys.argv[2]], check=True)
