#!/bin/sh
# One-time setup: downloads pinned, checksum-verified Python, Node.js, Zig and
# libraries into this folder. Nothing is installed system-wide.
set -eu
cd -- "$(dirname -- "$0")/../.."
if [ -f runtime/portable-manifest.json ] && [ -x runtime/python/bin/python3 ]; then
  echo 'Setup has already been done. Checking it still works...'
else
  echo 'Downloading the build tools into this folder (about 500 MB). This can take several minutes.'
  mkdir -p build/bootstrap
  url=$(sed -n '/"linux-x64"/,/"wheels"/p' tools/rom-maker/portable-runtimes.json | sed -n '/"python"/,/}/s/.*"url": *"\([^"]*\)".*/\1/p' | head -n 1)
  sum=$(sed -n '/"linux-x64"/,/"wheels"/p' tools/rom-maker/portable-runtimes.json | sed -n '/"python"/,/}/s/.*"sha256": *"\([^"]*\)".*/\1/p' | head -n 1)
  echo '[1/3] Downloading Python...'
  if command -v curl >/dev/null 2>&1; then curl -fL --retry 3 -o build/bootstrap/python.tar.gz "$url"
  else wget -O build/bootstrap/python.tar.gz "$url"; fi
  echo "$sum  build/bootstrap/python.tar.gz" | sha256sum -c - >/dev/null || { echo 'Python download failed its checksum. Run setup again.' >&2; exit 1; }
  tar -xzf build/bootstrap/python.tar.gz -C build/bootstrap
  echo '[2/3] Downloading Node.js, Zig and libraries, then the build packages...'
  unset NODE_OPTIONS NODE_PATH PYTHONHOME PYTHONPATH
  build/bootstrap/python/bin/python3 tools/rom-maker/portable.py --platform linux-x64 --root .
  rm -rf build/bootstrap build/portable-cache
fi
echo '[3/3] Checking the tools...'
unset PYTHONHOME NODE_PATH NODE_OPTIONS
PYTHONNOUSERSITE=1 PYTHONPATH="$PWD/tools/rom-maker/offline" runtime/python/bin/python3 tools/rom-maker/maker.py --check >/dev/null
echo 'Setup finished. Now run: sh make-rom.sh /path/to/pacland.zip'
