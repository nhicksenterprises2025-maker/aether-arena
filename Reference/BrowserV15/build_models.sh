#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
if ! command -v blender >/dev/null 2>&1; then
  echo "Blender is not in PATH. Run blender/generate_models.py from Blender's Scripting workspace instead."
  exit 1
fi
blender --background --python-exit-code 1 --python blender/generate_models.py
