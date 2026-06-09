#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for project in bash curl ffmpeg git openssl sqlite tmux vim; do
  echo "============================================================"
  echo "[+] Building $project"
  echo "============================================================"
  "$SCRIPT_DIR/build_${project}_bc.sh"
done
