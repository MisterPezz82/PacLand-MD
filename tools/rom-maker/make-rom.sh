#!/bin/sh
set -eu
maker_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
maker_root=$(CDPATH= cd -- "$maker_dir/../.." && pwd)
if [ -f "$maker_root/runtime/portable-manifest.json" ] && [ ! -x "$maker_root/runtime/python/bin/python3" ]; then
  echo 'Portable runtime missing. Extract the complete builder package.' >&2
  exit 1
fi
if [ -x "$maker_root/runtime/python/bin/python3" ]; then
  unset PYTHONHOME NODE_PATH NODE_OPTIONS
  export PYTHONNOUSERSITE=1
  export PYTHONPATH="$maker_root/tools/rom-maker/offline"
  exec "$maker_root/runtime/python/bin/python3" "$maker_dir/maker.py" "$@"
fi
if [ -x "$maker_root/.venv/bin/python" ]; then
  exec "$maker_root/.venv/bin/python" "$maker_dir/maker.py" "$@"
fi
exec python3 "$maker_dir/maker.py" "$@"
