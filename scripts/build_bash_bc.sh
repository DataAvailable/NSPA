#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="bash"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/bash-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/bash-bc}"

source "$SCRIPT_DIR/build_common.sh"

compile_bash_source() {
  local src="$1"
  local rel out
  rel="${src#$SRC_DIR/}"
  out="$OUT_DIR/objects/${rel%.*}.bc"
  mkdir -p "$(dirname "$out")"
  "$CLANG_BIN"     -emit-llvm -fno-discard-value-names -O0 -g     -DPROGRAM='"bash"' -DPACKAGE='"bash"' -DSHELL -DHAVE_CONFIG_H     -DCONF_HOSTTYPE='"x86_64"' -DCONF_OSTYPE='"linux-gnu"'     -DCONF_MACHTYPE='"x86_64-pc-linux-gnu"' -DCONF_VENDOR='"pc"'     -DLOCALEDIR='"/usr/local/share/locale"'     -I"$SRC_DIR" -I"$SRC_DIR/include" -I"$SRC_DIR/lib"     -I"$SRC_DIR/builtins" -I"$SRC_DIR/lib/readline" -I"$SRC_DIR/lib/glob"     -I"$SRC_DIR/lib/intl" -I"$SRC_DIR/lib/sh" -I"$SRC_DIR/lib/tilde"     -c "$src" -o "$out"
  manifest_append "$out" "$rel" "object"
}

init_project_build
cd "$SRC_DIR"
run_make_clean
configure_autotools --without-bash-malloc --disable-nls

# Bash needs native generator programs during its build. Build once normally so
# generated files such as syntax.c, builtext.h, and signames.h exist.
"$MAKE_BIN" -j"$JOBS" CC="$CLANG_BIN" CFLAGS="$BASE_CFLAGS" >/dev/null

printf 'bc_file	source_or_member	origin
' > "$MANIFEST"
rm -rf "$OUT_DIR/objects" "$OUT_DIR/archive-objects" "$OUT_DIR/link-work"
mkdir -p "$OUT_DIR/objects" "$OUT_DIR/archive-objects" "$OUT_DIR/link-work"

while IFS= read -r -d '' src; do
  case "${src#$SRC_DIR/}" in
    mksyntax.c|nojobs.c|builtins/mkbuiltins.c|builtins/gen-helpfiles.c|builtins/psize.c|lib/glob/glob_loop.c|lib/glob/gm_loop.c|lib/glob/sm_loop.c|lib/readline/tilde.c|lib/readline/xfree.c|lib/readline/xmalloc.c|lib/readline/shell.c|lib/readline/emacs_keymap.c|lib/readline/vi_keymap.c)
      continue
      ;;
  esac
  compile_bash_source "$src"
done < <(
  find "$SRC_DIR" -maxdepth 1 -type f -name '*.c' -print0
  find "$SRC_DIR/builtins" -maxdepth 1 -type f -name '*.c' -print0
  find "$SRC_DIR/lib/readline" -maxdepth 1 -type f -name '*.c' -print0
  find "$SRC_DIR/lib/glob" -maxdepth 1 -type f -name '*.c' -print0
  find "$SRC_DIR/lib/sh" -maxdepth 1 -type f -name '*.c' -print0
  find "$SRC_DIR/lib/termcap" -maxdepth 1 -type f -name '*.c' -print0
  find "$SRC_DIR/lib/tilde" -maxdepth 1 -type f -name '*.c' -print0
)

link_project_bc
if [ ! -s "$FULL_BC" ]; then
  echo "[-] Full project bitcode was not created: $FULL_BC"
  exit 1
fi

echo "[+] Done."
echo "[+] Per-source .bc count : $(find "$OUT_DIR/objects" -name '*.bc' | wc -l)"
echo "[+] Full project .bc     : $FULL_BC"
echo "[+] Manifest             : $MANIFEST"
