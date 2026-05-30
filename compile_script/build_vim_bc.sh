#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Build Vim with clang and collect LLVM bitcode files.
#
# Expected layout:
#   NSPA/
#   ├── compile_script/
#   │   └── build_vim_bc.sh
#   ├── open-source-soft/
#   │   └── vim-master/
#   └── workspace/
#       └── vim-bc/
#
# Usage:
#   cd /path/to/NSPA
#   bash compile_script/build_vim_bc.sh
#
# Optional:
#   bash compile_script/build_vim_bc.sh /path/to/vim-master /path/to/vim-bc
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="vim"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/vim-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/${PROJECT_NAME}-bc}"
LOG_DIR="$NSPA_ROOT/workspace/logs"

CLANG_BIN="${CLANG_BIN:-clang}"

mkdir -p "$LOG_DIR"

echo "[+] NSPA root     : $NSPA_ROOT"
echo "[+] Project       : $PROJECT_NAME"
echo "[+] Source dir    : $SRC_DIR"
echo "[+] Output bc dir : $OUT_DIR"
echo "[+] Build log     : $LOG_DIR/${PROJECT_NAME}_build.log"
echo "[+] Clang         : $CLANG_BIN"

if [ ! -d "$SRC_DIR" ]; then
  echo "[-] Source directory not found: $SRC_DIR"
  exit 1
fi

cd "$SRC_DIR"

echo "[+] Cleaning old build files..."
make distclean >/dev/null 2>&1 || make clean >/dev/null 2>&1 || true

if [ ! -f configure ]; then
  echo "[-] configure not found. Please check Vim source tree: $SRC_DIR"
  exit 1
fi

if [ ! -f src/configure ]; then
  echo "[-] src/configure not found. Please check Vim source tree: $SRC_DIR"
  exit 1
fi

chmod +x configure src/configure

echo "[+] Configuring Vim..."

CC="$CLANG_BIN" \
CFLAGS="-O0 -g" \
bash configure \
  --disable-gui \
  --without-x

echo "[+] Building Vim LLVM bitcode objects..."

make -j"$(nproc)" \
  CC="$CLANG_BIN" \
  CFLAGS="-O0 -g -emit-llvm -fno-discard-value-names" \
  2>&1 | tee "$LOG_DIR/${PROJECT_NAME}_build.log"

echo "[+] Collecting .bc files..."

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"

if [ ! -d objects ]; then
  echo "[-] objects/ directory not found. Vim build may have failed."
  exit 1
fi

BC_COUNT=0

for f in objects/*.o; do
  [ -f "$f" ] || continue

  if file "$f" | grep -qi "LLVM.*bitcode"; then
    out="$OUT_DIR/$(basename "${f%.o}.bc")"
    cp "$f" "$out"
    BC_COUNT=$((BC_COUNT + 1))
  else
    echo "[!] Skip non-bitcode object: $f"
  fi
done

echo "[+] Done."
echo "[+] Output directory : $OUT_DIR"
echo "[+] Number of .bc    : $BC_COUNT"

if [ "$BC_COUNT" -eq 0 ]; then
  echo "[-] No .bc files collected. Please check whether objects/*.o are LLVM bitcode."
  exit 1
fi

echo "[+] Sample .bc files:"
find "$OUT_DIR" -name "*.bc" | head -n 20

echo "[+] File type check:"
find "$OUT_DIR" -name "*.bc" | head -n 5 | xargs -r file