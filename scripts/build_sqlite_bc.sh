#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/sqlite-master}"
BUILD_DIR="$SRC_DIR/build"

OUT_DIR="${2:-$NSPA_ROOT/workspace/sqlite-bc}"
BC_DIR="$OUT_DIR/bc"

cd "$SRC_DIR"

chmod +x configure 2>/dev/null || true
if [ -d autosetup ]; then
  find autosetup -type f -exec chmod +x {} \;
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CC=clang ../configure --disable-shared --disable-readline --disable-amalgamation

# 触发生成 tsrc/
make sqlite3.c

mkdir -p "$BC_DIR"

COMMON_FLAGS="-O0 -g -emit-llvm -fno-discard-value-names -D_HAVE_SQLITE_CONFIG_H -DBUILD_sqlite"
INCLUDES="-I. -Itsrc -I../src -I../ext/rtree -I../ext/icu -I../ext/fts3 -I../ext/session -I../ext/misc"

for f in tsrc/*.c; do
    base="$(basename "$f" .c)"

    # geopoly.c 被 include 到 rtree.c 中
    if [ "$base" = "geopoly" ]; then
        echo "[+] skipping $base.c (included into rtree.c)"
        continue
    fi

    # Tcl 接口，不属于 SQLite core，需要 tcl.h / tcl-dev
    if [ "$base" = "tclsqlite-ex" ] || [ "$base" = "tclsqlite" ] || [ "$base" = "tclsqlite3" ]; then
        echo "[+] skipping $base.c (Tcl extension, not part of SQLite core)"
        continue
    fi

    echo "[+] compiling $base.c -> bc/${base}.bc"
    clang $COMMON_FLAGS $INCLUDES -c "$f" -o "$BC_DIR/${base}.bc"
done

# 可选：把 shell.c 也编进去
if [ -f ../shell.c ]; then
    echo "[+] compiling shell.c -> bc/shell.bc"
    clang $COMMON_FLAGS $INCLUDES -c ../shell.c -o "$BC_DIR/shell.bc" || true
fi

link_inputs=()
for f in "$BC_DIR"/*.bc; do
    if llvm-dis "$f" -o - 2>/dev/null | grep -q '^@sqlite3_api ='; then
        echo "[+] excluding $(basename "$f") from full link (defines sqlite3_api)"
        continue
    fi
    link_inputs+=("$f")
done

link_inputs=()

echo "[+] checking duplicate sqlite3_api providers..."
for f in "$BC_DIR"/*.bc; do
    base="$(basename "$f")"

    # 已知不单独并入 core/full link 的文件
    case "$base" in
        shell.bc|tclsqlite-ex.bc|tclsqlite.bc|tclsqlite3.bc)
            echo "[+] excluding $base (non-core front-end / Tcl binding)"
            continue
            ;;
    esac

    # 凡是定义了 sqlite3_api 的，都不并进总模块
    if llvm-nm "$f" 2>/dev/null | grep -Eq '[[:space:]][A-ZBDGRSTVW][[:space:]]+sqlite3_api$|(^| )sqlite3_api$'; then
        echo "[+] excluding $base (defines sqlite3_api)"
        continue
    fi

    link_inputs+=("$f")
done

printf '[+] final link inputs: %d files\n' "${#link_inputs[@]}"

if [ "${#link_inputs[@]}" -eq 0 ]; then
    echo "[-] no linkable bc inputs left"
    exit 1
fi

llvm-link "${link_inputs[@]}" -o "$OUT_DIR/sqlite-full.bc"
llvm-dis "$OUT_DIR/sqlite-full.bc" -o "$OUT_DIR/sqlite-full.ll"

echo
echo "[+] done"
echo "[+] per-file bc dir : $BC_DIR"
echo "[+] full bc         : $OUT_DIR/sqlite-full.bc"
echo "[+] full ll         : $OUT_DIR/sqlite-full.ll"