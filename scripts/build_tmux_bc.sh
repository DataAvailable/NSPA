#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="tmux"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/tmux-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/tmux-bc}"

source "$SCRIPT_DIR/build_common.sh"

init_project_build
cd "$SRC_DIR"
run_make_clean
if [ -n "${LIBEVENT_PREFIX:-}" ]; then
  BASE_CPPFLAGS="$BASE_CPPFLAGS -I$LIBEVENT_PREFIX/include"
  BASE_LDFLAGS="$BASE_LDFLAGS -L$LIBEVENT_PREFIX/lib"
fi
configure_autotools
build_with_make
finish_build
