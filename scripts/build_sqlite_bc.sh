#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="sqlite"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/sqlite-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/sqlite-bc}"
BUILD_DIR="$SRC_DIR/build"

source "$SCRIPT_DIR/build_common.sh"

init_project_build
cd "$SRC_DIR"

chmod +x configure 2>/dev/null || true
if [ -d autosetup ]; then
  find autosetup -type f -exec chmod +x {} \;
fi

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

env TMPDIR="$TMPDIR" CC="$CLANG_BIN" ../configure --disable-shared --disable-readline --disable-amalgamation

# Generate tsrc/, which contains SQLite core sources after configure-time code
# generation. Compiling tsrc mirrors SQLite's own non-amalgamation build.
"$MAKE_BIN" sqlite3.c

COMMON_FLAGS=(-O0 -g -emit-llvm -fno-discard-value-names -D_HAVE_SQLITE_CONFIG_H -DBUILD_sqlite)
INCLUDES=(-I. -Itsrc -I../src -I../ext/rtree -I../ext/icu -I../ext/fts3 -I../ext/session -I../ext/misc)

compile_sqlite_source() {
  local src="$1"
  local rel="$2"
  local out="$OUT_DIR/objects/${rel%.*}.bc"
  mkdir -p "$(dirname "$out")"
  "$CLANG_BIN" "${COMMON_FLAGS[@]}" "${INCLUDES[@]}" -c "$src" -o "$out"
  manifest_append "$out" "$rel" "object"
}

for src in tsrc/*.c; do
  base="$(basename "$src" .c)"
  case "$base" in
    geopoly)
      echo "[+] skipping $base.c (included into rtree.c)"
      continue
      ;;
    tclsqlite-ex|tclsqlite|tclsqlite3)
      echo "[+] skipping $base.c (Tcl extension, not part of SQLite core)"
      continue
      ;;
  esac

  echo "[+] compiling $base.c -> objects/build/tsrc/${base}.bc"
  compile_sqlite_source "$src" "build/tsrc/${base}.c"
done

if [ -f ../shell.c ]; then
  echo "[+] compiling shell.c -> objects/shell.bc"
  compile_sqlite_source ../shell.c "shell.c" || true
fi

link_inputs=()
echo "[+] checking duplicate sqlite3_api providers..."
while IFS= read -r -d '' bc; do
  base="$(basename "$bc")"
  case "$base" in
    shell.bc|tclsqlite-ex.bc|tclsqlite.bc|tclsqlite3.bc)
      echo "[+] excluding $base (non-core front-end / Tcl binding)"
      continue
      ;;
  esac

  if llvm-nm "$bc" 2>/dev/null | grep -Eq '[[:space:]][A-ZBDGRSTVW][[:space:]]+sqlite3_api$|(^| )sqlite3_api$'; then
    echo "[+] excluding $base (defines sqlite3_api)"
    continue
  fi

  link_inputs+=("$bc")
done < <(find "$OUT_DIR/objects" -type f -name '*.bc' -print0)

printf '[+] final link inputs: %d files\n' "${#link_inputs[@]}"

if [ "${#link_inputs[@]}" -eq 0 ]; then
  echo "[-] no linkable bc inputs left"
  exit 1
fi

"$LLVM_LINK_BIN" "${link_inputs[@]}" -o "$FULL_BC"
cp "$FULL_BC" "$OUT_DIR/sqlite-full.bc"
llvm-dis "$FULL_BC" -o "$OUT_DIR/sqlite-full.ll"

echo
echo "[+] Done."
echo "[+] Per-source .bc count : $(find "$OUT_DIR/objects" -name '*.bc' | wc -l)"
echo "[+] Full project .bc     : $FULL_BC"
echo "[+] Compatibility .bc    : $OUT_DIR/sqlite-full.bc"
echo "[+] Manifest             : $MANIFEST"
