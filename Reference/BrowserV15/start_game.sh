#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
if command -v node >/dev/null 2>&1; then
  node server.js
elif command -v python3 >/dev/null 2>&1; then
  python3 -m http.server 8080
else
  echo "Node.js or Python 3 is required."
  exit 1
fi
