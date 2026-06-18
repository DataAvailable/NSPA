#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$HOME/Projects/NSPA/open-source-soft/nasm-master"
OUT_DIR="$HOME/Projects/NSPA/workspace/nasm-bc"

BC_DIR="$OUT_DIR/bc"
WRAPPER_DIR="$OUT_DIR/wrappers"
LOG_DIR="$OUT_DIR/logs"

WRAPPER_CC="$WRAPPER_DIR/clang-bc"

CLANG_BIN="${CLANG_BIN:-clang}"
LLVM_LINK_BIN="${LLVM_LINK_BIN:-llvm-link}"
LLVM_DIS_BIN="${LLVM_DIS_BIN:-llvm-dis}"
LLVM_AR_BIN="${LLVM_AR_BIN:-llvm-ar}"
LLVM_RANLIB_BIN="${LLVM_RANLIB_BIN:-llvm-ranlib}"
JOBS="${JOBS:-$(nproc)}"

CFLAGS_BASE="${NSPA_CFLAGS:--O0 -g}"

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[-] Required tool not found: $1"
        exit 1
    fi
}

require_tool "$CLANG_BIN"
require_tool "$LLVM_LINK_BIN"
require_tool "$LLVM_DIS_BIN"
require_tool "$LLVM_AR_BIN"
require_tool "$LLVM_RANLIB_BIN"
require_tool make
require_tool file
require_tool sh

if [ ! -d "$SRC_DIR" ]; then
    echo "[-] Source directory not found: $SRC_DIR"
    exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$BC_DIR" "$WRAPPER_DIR" "$LOG_DIR"

cat > "$WRAPPER_CC" <<EOF
#!/usr/bin/env bash
has_compile=0

for arg in "\$@"; do
    if [ "\$arg" = "-c" ]; then
        has_compile=1
        break
    fi
done

if [ "\$has_compile" = "1" ]; then
    exec "$CLANG_BIN" -emit-llvm -fno-discard-value-names "\$@"
else
    exec "$CLANG_BIN" "\$@"
fi
EOF

chmod +x "$WRAPPER_CC"

echo "[+] Project    : nasm"
echo "[+] Source dir : $SRC_DIR"
echo "[+] Output dir : $OUT_DIR"
echo "[+] Jobs       : $JOBS"

cd "$SRC_DIR"

echo "[+] Cleaning old build..."
if [ -f Makefile ]; then
    make distclean >/dev/null 2>&1 || make clean >/dev/null 2>&1 || true
fi

echo "[+] Preparing configure script..."
if [ ! -f configure ]; then
    if [ -f autogen.sh ]; then
        sh autogen.sh
    else
        echo "[-] configure not found and autogen.sh not found."
        exit 1
    fi
fi

chmod +x configure 2>/dev/null || true

echo "[+] Configuring with real clang..."
CC="$CLANG_BIN" \
CFLAGS="$CFLAGS_BASE" \
sh configure

echo "[+] Building NASM with clang bitcode wrapper..."
set +e
make -k -j"$JOBS" \
    CC="$WRAPPER_CC" \
    AR="$LLVM_AR_BIN" \
    RANLIB="$LLVM_RANLIB_BIN" \
    CFLAGS="$CFLAGS_BASE" \
    2>&1 | tee "$LOG_DIR/build.log"
MAKE_RET=${PIPESTATUS[0]}
set -e

if [ "$MAKE_RET" -ne 0 ]; then
    echo "[!] make returned non-zero. Continue to collect generated LLVM bitcode."
fi

echo "[+] Collecting per-source LLVM bitcode..."

find "$SRC_DIR" \
    \( -path '*/.git/*' \
       -o -path '*/.deps/*' \
       -o -path '*/autom4te.cache/*' \
       -o -path '*/test/*' \
       -o -path '*/tests/*' \) -prune -o \
    -type f \( -name '*.o' -o -name '*.bc' \) -print0 |
while IFS= read -r -d '' f; do
    if file "$f" | grep -qi 'LLVM.*bitcode'; then
        rel="${f#$SRC_DIR/}"
        out="$BC_DIR/${rel%.*}.bc"
        mkdir -p "$(dirname "$out")"
        cp "$f" "$out"
        echo "[+] collected: $rel -> ${out#$OUT_DIR/}"
    fi
done

BC_COUNT="$(find "$BC_DIR" -type f -name '*.bc' | wc -l)"

if [ "$BC_COUNT" -eq 0 ]; then
    echo "[-] No LLVM bitcode files were collected."
    exit 1
fi

echo "[+] Per-source .bc count: $BC_COUNT"

echo "[+] Linking complete project bitcode..."
mapfile -t BC_FILES < <(find "$BC_DIR" -type f -name '*.bc' | sort)

set +e
"$LLVM_LINK_BIN" "${BC_FILES[@]}" -o "$OUT_DIR/project.bc" 2>&1 | tee "$LOG_DIR/llvm-link.log"
LINK_RET=${PIPESTATUS[0]}
set -e

if [ "$LINK_RET" -ne 0 ]; then
    echo "[!] llvm-link failed for all collected bitcode."
    echo "[!] This may be caused by multiple executable targets or duplicate symbols."
    echo "[!] Per-source .bc files are still available in: $BC_DIR"
    echo "[!] Link log: $LOG_DIR/llvm-link.log"
    exit 0
fi

"$LLVM_DIS_BIN" "$OUT_DIR/project.bc" -o "$OUT_DIR/project.ll"

echo
echo "[+] Done."
echo "[+] Per-source bitcode dir : $BC_DIR"
echo "[+] Full project bitcode   : $OUT_DIR/project.bc"
echo "[+] Full project LLVM IR   : $OUT_DIR/project.ll"
echo "[+] Build log              : $LOG_DIR/build.log"