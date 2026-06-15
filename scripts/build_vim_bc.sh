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

obj_printer="$OUT_DIR/vim-print-objects.mk"
cat > "$obj_printer" <<'MAKE_EOF'
print-OBJ:
	@printf '%s\n' $(OBJ)
MAKE_EOF

mapfile -t vim_objects < <("$MAKE_BIN" --no-print-directory -C src -f Makefile -f "$obj_printer" print-OBJ)
if [ "${#vim_objects[@]}" -eq 0 ]; then
  echo "[-] Failed to expand Vim object list from src/Makefile"
  exit 1
fi

log_file="$LOG_DIR/build.log"
set +e
"$MAKE_BIN" --no-print-directory -C src -k -j"$JOBS" \
  CC="$WRAPPER_CC" \
  CXX="$WRAPPER_CXX" \
  TMPDIR="$TMPDIR" \
  CPPFLAGS="$BASE_CPPFLAGS" \
  CFLAGS="$BASE_CFLAGS" \
  CXXFLAGS="$BASE_CXXFLAGS" \
  LDFLAGS="$BASE_LDFLAGS" \
  "${vim_objects[@]}" \
  2>&1 | tee "$log_file"
make_ret=${PIPESTATUS[0]}
set -e
if [ "$make_ret" -ne 0 ]; then
  echo "[!] make returned non-zero while building Vim objects."
fi

finish_build
