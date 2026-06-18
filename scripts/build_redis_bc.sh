#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$HOME/Projects/NSPA/open-source-soft/redis-master"
OUT_DIR="$HOME/Projects/NSPA/workspace/redis-bc"

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

require_tool "$CLANG_BIN"
require_tool "$LLVM_LINK_BIN"
require_tool "$LLVM_DIS_BIN"
require_tool "$LLVM_AR_BIN"
require_tool "$LLVM_RANLIB_BIN"
require_tool make
require_tool file

if [ ! -d "$SRC_DIR" ]; then
    echo "[-] Source directory not found: $SRC_DIR"
    exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$BC_DIR" "$LOG_DIR" "$WRAPPER_DIR"

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

echo "[+] Project    : redis"
echo "[+] Source dir : $SRC_DIR"
echo "[+] Output dir : $OUT_DIR"
echo "[+] Jobs       : $JOBS"

cd "$SRC_DIR"

echo "[+] Cleaning old build..."
make distclean >/dev/null 2>&1 || make clean >/dev/null 2>&1 || true

echo "[+] Building Redis with clang bitcode wrapper..."
set +e
make -k -j"$JOBS" \
    CC="$WRAPPER_CC" \
    AR="$LLVM_AR_BIN" \
    RANLIB="$LLVM_RANLIB_BIN" \
    MALLOC=libc \
    CFLAGS="$CFLAGS_BASE" \
    2>&1 | tee "$LOG_DIR/build.log"
MAKE_RET=${PIPESTATUS[0]}
set -e

if [ "$MAKE_RET" -ne 0 ]; then
    echo "[!] make returned non-zero. Continue to collect generated LLVM bitcode."
    echo "[!] This is expected if final executable linking sees LLVM bitcode objects."
fi

echo "[+] Collecting per-source LLVM bitcode..."

find "$SRC_DIR" \
    \( -path '*/.git/*' \
       -o -path '*/.deps/*' \
       -o -path '*/tests/*' \
       -o -path '*/deps/jemalloc/*' \) -prune -o \
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

# Redis 会同时构建 redis-server、redis-cli、redis-benchmark 等多个目标。
# 直接把全部 .bc 链在一起容易出现多个 main 或重复符号。
# 因此这里默认生成 redis-server 的完整 IR，并保留所有逐文件 .bc。
echo "[+] Linking redis-server bitcode..."

mapfile -t SERVER_BC_FILES < <(
    find "$BC_DIR" -type f -name '*.bc' \
    | grep '/src/' \
    | grep -v '/src/redis-cli.bc$' \
    | grep -v '/src/redis-benchmark.bc$' \
    | grep -v '/src/redis-check-aof.bc$' \
    | grep -v '/src/redis-check-rdb.bc$' \
    | grep -v '/src/redis-sentinel.bc$' \
    | sort
)

if [ "${#SERVER_BC_FILES[@]}" -eq 0 ]; then
    echo "[-] No Redis server bitcode inputs found under $BC_DIR/src"
    exit 1
fi

set +e
"$LLVM_LINK_BIN" "${SERVER_BC_FILES[@]}" -o "$OUT_DIR/redis-server.bc" 2>&1 | tee "$LOG_DIR/llvm-link-server.log"
LINK_RET=${PIPESTATUS[0]}
set -e

if [ "$LINK_RET" -ne 0 ]; then
    echo "[!] llvm-link failed for redis-server.bc."
    echo "[!] Per-source .bc files are still available in: $BC_DIR"
    echo "[!] Link log: $LOG_DIR/llvm-link-server.log"
    exit 0
fi

"$LLVM_DIS_BIN" "$OUT_DIR/redis-server.bc" -o "$OUT_DIR/redis-server.ll"

# 可选：尝试生成 redis-cli 的 IR
echo "[+] Linking redis-cli bitcode..."

mapfile -t CLI_BC_FILES < <(
    find "$BC_DIR" -type f -name '*.bc' \
    | grep '/src/' \
    | grep -E '/src/(redis-cli|anet|adlist|dict|zmalloc|release|ae|redisassert|siphash|crc64|crcspeed|monotonic|mt19937-64|cli_common|connection|sds)\.bc$' \
    | sort
)

if [ "${#CLI_BC_FILES[@]}" -gt 0 ]; then
    set +e
    "$LLVM_LINK_BIN" "${CLI_BC_FILES[@]}" -o "$OUT_DIR/redis-cli.bc" 2>&1 | tee "$LOG_DIR/llvm-link-cli.log"
    CLI_LINK_RET=${PIPESTATUS[0]}
    set -e

    if [ "$CLI_LINK_RET" -eq 0 ]; then
        "$LLVM_DIS_BIN" "$OUT_DIR/redis-cli.bc" -o "$OUT_DIR/redis-cli.ll"
    else
        echo "[!] llvm-link failed for redis-cli.bc. See $LOG_DIR/llvm-link-cli.log"
    fi
fi

echo
echo "[+] Done."
echo "[+] Per-source bitcode dir : $BC_DIR"
echo "[+] Redis server bitcode   : $OUT_DIR/redis-server.bc"
echo "[+] Redis server LLVM IR   : $OUT_DIR/redis-server.ll"
echo "[+] Redis CLI bitcode      : $OUT_DIR/redis-cli.bc  (if linked successfully)"
echo "[+] Build log              : $LOG_DIR/build.log"