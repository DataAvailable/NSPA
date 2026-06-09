#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="vim"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/vim-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/vim-bc}"

source "$SCRIPT_DIR/build_common.sh"

init_project_build
cd "$SRC_DIR"
run_make_clean
if [ ! -f src/configure ]; then
  echo "[-] Vim src/configure not found: $SRC_DIR/src/configure"
  exit 1
fi
chmod +x configure src/configure src/auto/configure
configure_autotools --disable-gui --without-x
build_with_make
finish_build
