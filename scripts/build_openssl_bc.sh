#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$HOME/Projects/NSPA/open-source-soft/openssl-master"
OUT_DIR="$HOME/Projects/NSPA/workspace/openssl-bc"

BC_DIR="$OUT_DIR/bc"
LOG_DIR="$OUT_DIR/logs"
WRAPPER_DIR="$OUT_DIR/wrappers"
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

require_tool perl
require_tool make
require_tool file
require_tool "$CLANG_BIN"
require_tool "$LLVM_LINK_BIN"
require_tool "$LLVM_DIS_BIN"
require_tool "$LLVM_AR_BIN"
require_tool "$LLVM_RANLIB_BIN"

if [ ! -d "$SRC_DIR" ]; then
    echo "[-] Source directory not found: $SRC_DIR"
    exit 1
fi

if [ ! -f "$SRC_DIR/Configure" ]; then
    echo "[-] OpenSSL Configure not found: $SRC_DIR/Configure"
    echo "[-] Please check whether SRC_DIR points to the real OpenSSL source root."
    exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$BC_DIR" "$LOG_DIR" "$WRAPPER_DIR"

cat > "$WRAPPER_CC" <<EOF
#!/usr/bin/env bash
has_compile=0
has_asm_source=0
is_assembler_mode=0

prev=""
for arg in "\$@"; do
    if [ "\$arg" = "-c" ]; then
        has_compile=1
    fi

    case "\$arg" in
        *.s|*.S|*.asm)
            has_asm_source=1
            ;;
    esac

    if [ "\$prev" = "-x" ]; then
        case "\$arg" in
            assembler|assembler-with-cpp)
                is_assembler_mode=1
                ;;
        esac
    fi

    prev="\$arg"
done

if [ "\$has_compile" = "1" ] && [ "\$has_asm_source" = "0" ] && [ "\$is_assembler_mode" = "0" ]; then
    exec "$CLANG_BIN" -emit-llvm -fno-discard-value-names "\$@"
else
    exec "$CLANG_BIN" "\$@"
fi
EOF

chmod +x "$WRAPPER_CC"

echo "[+] Project    : openssl"
echo "[+] Source dir : $SRC_DIR"
echo "[+] Output dir : $OUT_DIR"
echo "[+] Jobs       : $JOBS"

cd "$SRC_DIR"

echo "[+] Cleaning old build..."
if [ -f Makefile ]; then
    make distclean >/dev/null 2>&1 || make clean >/dev/null 2>&1 || true
fi

echo "[+] Configuring OpenSSL with real clang..."
CC="$CLANG_BIN" \
AR="$LLVM_AR_BIN" \
RANLIB="$LLVM_RANLIB_BIN" \
CFLAGS="$CFLAGS_BASE" \
perl Configure \
    no-shared \
    no-asm \
    no-tests \
    --debug

echo "[+] Building OpenSSL with clang bitcode wrapper..."
set +e
make -k -j"$JOBS" build_sw \
    CC="$WRAPPER_CC" \
    AR="$LLVM_AR_BIN" \
    RANLIB="$LLVM_RANLIB_BIN" \
    CFLAGS="$CFLAGS_BASE" \
    2>&1 | tee "$LOG_DIR/build.log"
MAKE_RET=${PIPESTATUS[0]}
set -e

if [ "$MAKE_RET" -ne 0 ]; then
    echo "[!] make returned non-zero. Continue to collect generated LLVM bitcode."
    echo "[!] This may happen if final executable linking sees LLVM bitcode objects."
fi

echo "[+] Collecting per-source LLVM bitcode..."

find "$SRC_DIR" \
    \( -path '*/.git/*' \
       -o -path '*/test/*' \
       -o -path '*/fuzz/*' \
       -o -path '*/demos/*' \
       -o -path '*/doc/*' \
       -o -path '*/providers/fipsmodule.cnf' \) -prune -o \
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

echo "[+] Linking complete OpenSSL bitcode..."
mapfile -t BC_FILES < <(find "$BC_DIR" -type f -name '*.bc' | sort)

set +e
"$LLVM_LINK_BIN" "${BC_FILES[@]}" -o "$OUT_DIR/project.bc" 2>&1 | tee "$LOG_DIR/llvm-link-project.log"
LINK_RET=${PIPESTATUS[0]}
set -e

if [ "$LINK_RET" -eq 0 ]; then
    "$LLVM_DIS_BIN" "$OUT_DIR/project.bc" -o "$OUT_DIR/project.ll"
else
    echo "[!] llvm-link failed for project.bc."
    echo "[!] Per-source .bc files are still available in: $BC_DIR"
    echo "[!] Link log: $LOG_DIR/llvm-link-project.log"
fi

echo "[+] Linking libcrypto-related bitcode..."
mapfile -t CRYPTO_BC_FILES < <(
    find "$BC_DIR" -type f -name '*.bc' \
    | grep -E '/(crypto|providers|common|ssl)/' \
    | sort
)

if [ "${#CRYPTO_BC_FILES[@]}" -gt 0 ]; then
    set +e
    "$LLVM_LINK_BIN" "${CRYPTO_BC_FILES[@]}" -o "$OUT_DIR/openssl-libcrypto-like.bc" 2>&1 | tee "$LOG_DIR/llvm-link-libcrypto-like.log"
    CRYPTO_LINK_RET=${PIPESTATUS[0]}
    set -e

    if [ "$CRYPTO_LINK_RET" -eq 0 ]; then
        "$LLVM_DIS_BIN" "$OUT_DIR/openssl-libcrypto-like.bc" -o "$OUT_DIR/openssl-libcrypto-like.ll"
    else
        echo "[!] llvm-link failed for openssl-libcrypto-like.bc."
        echo "[!] Link log: $LOG_DIR/llvm-link-libcrypto-like.log"
    fi
fi

echo
echo "[+] Done."
echo "[+] Per-source bitcode dir : $BC_DIR"
echo "[+] Full project bitcode   : $OUT_DIR/project.bc  if linked successfully"
echo "[+] Full project LLVM IR   : $OUT_DIR/project.ll  if linked successfully"
echo "[+] Crypto-like bitcode    : $OUT_DIR/openssl-libcrypto-like.bc  if linked successfully"
echo "[+] Logs                   : $LOG_DIR"