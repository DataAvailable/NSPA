#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Build an open-source C/C++ project with clang and collect LLVM
# bitcode files for NSPA/SVF/Saber.
#
# Backward-compatible default:
#   bash compile_script/build_curl_bc.sh
#   bash compile_script/build_curl_bc.sh /path/to/curl-master /path/to/curl-bc
#
# Multi-project usage:
#   bash compile_script/build_curl_bc.sh curl
#   bash compile_script/build_curl_bc.sh vim
#   bash compile_script/build_curl_bc.sh tmux
#   bash compile_script/build_curl_bc.sh sqlite
#   bash compile_script/build_curl_bc.sh ffmpeg
#   bash compile_script/build_curl_bc.sh all
#   bash compile_script/build_curl_bc.sh <project> <source-dir> <output-bc-dir>
#
# Defaults:
#   source-dir    = open-source-soft/<project>-master
#   output-bc-dir = workspace/<project>-bc
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CLANG_BIN="${CLANG_BIN:-clang}"
CLANGXX_BIN="${CLANGXX_BIN:-clang++}"
CONFIGURE_CC="${CONFIGURE_CC:-$CLANG_BIN}"
CONFIGURE_CXX="${CONFIGURE_CXX:-$CLANGXX_BIN}"
LLVM_AR_BIN="${LLVM_AR_BIN:-llvm-ar}"
LLVM_RANLIB_BIN="${LLVM_RANLIB_BIN:-llvm-ranlib}"
MAKE_BIN="${MAKE_BIN:-make}"
JOBS="${JOBS:-$(nproc)}"
BASE_CFLAGS="${NSPA_CFLAGS:--O0 -g}"
BASE_CXXFLAGS="${NSPA_CXXFLAGS:--O0 -g}"
BASE_CPPFLAGS="${NSPA_CPPFLAGS:-${CPPFLAGS:-}}"
BASE_LDFLAGS="${NSPA_LDFLAGS:-${LDFLAGS:-}}"
COLLECT_ALL_ARCHIVES="${COLLECT_ALL_ARCHIVES:-0}"
PROJECT_CPPFLAGS=""
PROJECT_LDFLAGS=""
PROJECT_PKG_CONFIG_PATH="${PKG_CONFIG_PATH:-}"

usage() {
  cat <<'EOF'
Usage:
  bash compile_script/build_curl_bc.sh [project] [source-dir] [output-bc-dir]
  bash compile_script/build_curl_bc.sh [source-dir] [output-bc-dir]

Supported project presets:
  curl, vim, tmux, sqlite, ffmpeg
  all  Build curl, vim, tmux, sqlite, and ffmpeg sequentially

Examples:
  bash compile_script/build_curl_bc.sh
  bash compile_script/build_curl_bc.sh curl
  bash compile_script/build_curl_bc.sh vim
  bash compile_script/build_curl_bc.sh all
  bash compile_script/build_curl_bc.sh tmux open-source-soft/tmux-master workspace/tmux-bc
  bash compile_script/build_curl_bc.sh /tmp/curl-master /tmp/curl-bc

Environment overrides:
  CLANG_BIN, CLANGXX_BIN, CONFIGURE_CC, CONFIGURE_CXX
  LLVM_AR_BIN, LLVM_RANLIB_BIN, MAKE_BIN, JOBS
  NSPA_CFLAGS, NSPA_CXXFLAGS, NSPA_CPPFLAGS, NSPA_LDFLAGS
  LIBEVENT_PREFIX=/path/to/libevent-install  Use a custom libevent for tmux.
  COLLECT_ALL_ARCHIVES=1  Extract every generated .a archive, not only libtool .libs archives.
EOF
}

normalize_project() {
  local name="$1"
  name="$(basename "$name")"
  name="${name%-master}"
  name="${name%-src}"
  case "$name" in
    ffmepg) name="ffmpeg" ;;
  esac
  printf '%s' "$name"
}

looks_like_path() {
  local value="$1"
  [[ "$value" == */* || "$value" == .* || -d "$value" ]]
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  usage
  exit 0
fi

if [ "${1:-}" = "all" ]; then
  for project in curl vim tmux sqlite ffmpeg; do
    echo "============================================================"
    echo "[+] Building preset project: $project"
    echo "============================================================"
    "$SCRIPT_DIR/build_curl_bc.sh" "$project"
  done
  exit 0
fi

if [ "$#" -eq 0 ]; then
  PROJECT_NAME="curl"
  SRC_DIR="$NSPA_ROOT/open-source-soft/curl-master"
  OUT_DIR="$NSPA_ROOT/workspace/curl-bc"
elif looks_like_path "$1"; then
  SRC_DIR="$1"
  PROJECT_NAME="$(normalize_project "$SRC_DIR")"
  OUT_DIR="${2:-$NSPA_ROOT/workspace/${PROJECT_NAME}-bc}"
else
  PROJECT_NAME="$(normalize_project "$1")"
  SRC_DIR="${2:-$NSPA_ROOT/open-source-soft/${PROJECT_NAME}-master}"
  OUT_DIR="${3:-$NSPA_ROOT/workspace/${PROJECT_NAME}-bc}"
fi

SRC_DIR="$(cd "$SRC_DIR" 2>/dev/null && pwd || printf '%s' "$SRC_DIR")"
OUT_DIR="$(mkdir -p "$OUT_DIR" && cd "$OUT_DIR" && pwd)"
LOG_DIR="$NSPA_ROOT/workspace/logs"
WRAPPER_CC="$SCRIPT_DIR/clang-bc-${PROJECT_NAME}.sh"
WRAPPER_CXX="$SCRIPT_DIR/clangxx-bc-${PROJECT_NAME}.sh"
BUILD_LOG="$LOG_DIR/${PROJECT_NAME}_build.log"

mkdir -p "$LOG_DIR"

echo "[+] NSPA root     : $NSPA_ROOT"
echo "[+] Project       : $PROJECT_NAME"
echo "[+] Source dir    : $SRC_DIR"
echo "[+] Output bc dir : $OUT_DIR"
echo "[+] Build log     : $BUILD_LOG"
echo "[+] CC wrapper    : $WRAPPER_CC"
echo "[+] CXX wrapper   : $WRAPPER_CXX"
echo "[+] Configure CC  : $CONFIGURE_CC"
echo "[+] Configure CXX : $CONFIGURE_CXX"
echo "[+] Jobs          : $JOBS"

if [ ! -d "$SRC_DIR" ]; then
  echo "[-] Source directory not found: $SRC_DIR"
  exit 1
fi

create_clang_wrapper() {
  local wrapper="$1"
  local compiler="$2"

  cat > "$wrapper" <<EOF
#!/usr/bin/env bash
for arg in "\$@"; do
  if [ "\$arg" = "-c" ]; then
    exec "$compiler" -emit-llvm -fno-discard-value-names "\$@"
  fi
done
exec "$compiler" "\$@"
EOF
  chmod +x "$wrapper"
}

run_make_clean() {
  echo "[+] Cleaning old build files..."
  if [ -f Makefile ] || [ -f makefile ] || [ -f GNUmakefile ]; then
    "$MAKE_BIN" distclean >/dev/null 2>&1 || "$MAKE_BIN" clean >/dev/null 2>&1 || true
  fi
}

ensure_configure() {
  if [ -f configure ]; then
    chmod +x configure
    return
  fi

  echo "[+] configure not found, trying to generate it..."
  if [ -x ./buildconf ]; then
    ./buildconf
  elif [ -x ./autogen.sh ]; then
    ./autogen.sh
  elif command -v autoreconf >/dev/null 2>&1; then
    autoreconf -fi
  else
    echo "[-] configure not found and no generator is available."
    exit 1
  fi

  if [ ! -f configure ]; then
    echo "[-] configure generation failed."
    exit 1
  fi
  chmod +x configure
}

configure_autotools() {
  local flags=("$@")
  ensure_configure
  echo "[+] Configuring $PROJECT_NAME..."
  env \
    PKG_CONFIG_PATH="$PROJECT_PKG_CONFIG_PATH" \
    CC="$CONFIGURE_CC" \
    CXX="$CONFIGURE_CXX" \
    AR="$LLVM_AR_BIN" \
    RANLIB="$LLVM_RANLIB_BIN" \
    CPPFLAGS="$BASE_CPPFLAGS $PROJECT_CPPFLAGS" \
    CFLAGS="$BASE_CFLAGS" \
    CXXFLAGS="$BASE_CXXFLAGS" \
    LDFLAGS="$BASE_LDFLAGS $PROJECT_LDFLAGS" \
    ./configure "${flags[@]}"
}

add_pkg_config_dir() {
  local dir="$1"
  [ -d "$dir" ] || return
  if [ -n "$PROJECT_PKG_CONFIG_PATH" ]; then
    PROJECT_PKG_CONFIG_PATH="$dir:$PROJECT_PKG_CONFIG_PATH"
  else
    PROJECT_PKG_CONFIG_PATH="$dir"
  fi
}

configure_libevent_prefix() {
  local prefix="$1"
  prefix="$(cd "$prefix" 2>/dev/null && pwd || printf '%s' "$prefix")"
  if [ ! -f "$prefix/include/event2/event.h" ] && [ ! -f "$prefix/include/event.h" ]; then
    echo "[-] LIBEVENT_PREFIX does not contain libevent headers: $prefix/include/event2/event.h"
    exit 1
  fi

  PROJECT_CPPFLAGS="$PROJECT_CPPFLAGS -I$prefix/include"
  if [ -d "$prefix/lib" ]; then
    PROJECT_LDFLAGS="$PROJECT_LDFLAGS -L$prefix/lib"
    add_pkg_config_dir "$prefix/lib/pkgconfig"
  fi
  if [ -d "$prefix/lib64" ]; then
    PROJECT_LDFLAGS="$PROJECT_LDFLAGS -L$prefix/lib64"
    add_pkg_config_dir "$prefix/lib64/pkgconfig"
  fi
}

print_tmux_dependency_help() {
  cat <<'EOF'
[-] tmux requires libevent >= 2, but it was not found.

Install the development package, then rerun:
  sudo apt-get update
  sudo apt-get install -y libevent-dev libncurses-dev pkg-config
  bash compile_script/build_curl_bc.sh tmux

Or point the script at a custom libevent installation:
  LIBEVENT_PREFIX=/path/to/libevent-install bash compile_script/build_curl_bc.sh tmux

If libevent is installed in a nonstandard pkg-config directory:
  PKG_CONFIG_PATH=/path/to/lib/pkgconfig bash compile_script/build_curl_bc.sh tmux
EOF
}

prepare_tmux_dependencies() {
  if [ -n "${LIBEVENT_PREFIX:-}" ]; then
    configure_libevent_prefix "$LIBEVENT_PREFIX"
  fi

  if command -v pkg-config >/dev/null 2>&1; then
    if PKG_CONFIG_PATH="$PROJECT_PKG_CONFIG_PATH" pkg-config --exists "libevent >= 2" \
      || PKG_CONFIG_PATH="$PROJECT_PKG_CONFIG_PATH" pkg-config --exists "libevent_core >= 2"; then
      echo "[+] Found libevent via pkg-config."
      return
    fi
  fi

  if [ -f /usr/include/event2/event.h ] || [ -f /usr/local/include/event2/event.h ]; then
    echo "[+] Found libevent headers in a standard include directory."
    return
  fi

  print_tmux_dependency_help
  exit 1
}

configure_ffmpeg() {
  ensure_configure
  echo "[+] Configuring ffmpeg..."
  ./configure \
    --cc="$CONFIGURE_CC" \
    --cxx="$CONFIGURE_CXX" \
    --ar="$LLVM_AR_BIN" \
    --ranlib="$LLVM_RANLIB_BIN" \
    --extra-cflags="$BASE_CFLAGS" \
    --extra-cxxflags="$BASE_CXXFLAGS" \
    --disable-shared \
    --enable-static \
    --disable-doc \
    --disable-programs \
    --disable-autodetect \
    --disable-x86asm \
    --disable-inline-asm \
    --disable-stripping \
    --disable-optimizations
}

configure_project() {
  case "$PROJECT_NAME" in
    curl)
      configure_autotools \
        --disable-shared \
        --enable-static \
        --without-ssl \
        --without-zlib \
        --without-brotli \
        --without-zstd \
        --without-libpsl \
        --without-libidn2 \
        --without-nghttp2 \
        --disable-ldap \
        --disable-ldaps
      ;;
    vim)
      if [ ! -f src/configure ]; then
        echo "[-] src/configure not found. Please check Vim source tree: $SRC_DIR"
        exit 1
      fi
      chmod +x configure src/configure
      configure_autotools --disable-gui --without-x
      ;;
    tmux)
      prepare_tmux_dependencies
      configure_autotools
      ;;
    sqlite)
      configure_autotools --disable-shared --disable-readline
      ;;
    ffmpeg)
      configure_ffmpeg
      ;;
    *)
      echo "[!] No preset for '$PROJECT_NAME'; using generic ./configure if available."
      if [ -f configure ] || [ -x ./autogen.sh ] || [ -x ./buildconf ]; then
        configure_autotools
      else
        echo "[!] No configure script found; will try make directly."
      fi
      ;;
  esac
}

validate_configure_object_extension() {
  if [ -f Makefile ] && grep -Eq '^[[:space:]]*OBJEXT[[:space:]]*=[[:space:]]*bc([[:space:]]|$)' Makefile; then
    cat <<EOF
[-] configure detected OBJEXT=bc.

This usually happens when configure is run with the bitcode wrapper as CC.
The project Makefile should keep the normal object suffix '.o'; the wrapper
will still write LLVM bitcode content into those .o files during make.

Fix:
  unset CONFIGURE_CC CONFIGURE_CXX
  bash compile_script/build_curl_bc.sh $PROJECT_NAME
EOF
    exit 1
  fi
}

build_project() {
  echo "[+] Building $PROJECT_NAME bitcode objects..."
  set +e
  "$MAKE_BIN" -k -j"$JOBS" \
    PKG_CONFIG_PATH="$PROJECT_PKG_CONFIG_PATH" \
    CC="$WRAPPER_CC" \
    CXX="$WRAPPER_CXX" \
    AR="$LLVM_AR_BIN" \
    RANLIB="$LLVM_RANLIB_BIN" \
    CPPFLAGS="$BASE_CPPFLAGS $PROJECT_CPPFLAGS" \
    CFLAGS="$BASE_CFLAGS" \
    CXXFLAGS="$BASE_CXXFLAGS" \
    LDFLAGS="$BASE_LDFLAGS $PROJECT_LDFLAGS" \
    2>&1 | tee "$BUILD_LOG"
  local make_ret=${PIPESTATUS[0]}
  set -e

  if [ "$make_ret" -ne 0 ]; then
    echo "[!] make returned non-zero, but continuing to collect generated LLVM bitcode."
  fi
}

rel_to_root() {
  local path="$1"
  case "$path" in
    "$NSPA_ROOT"/*) printf '%s' "${path#$NSPA_ROOT/}" ;;
    *) printf '%s' "$path" ;;
  esac
}

strip_libtool_prefix() {
  local stem="$1"
  stem="$(printf '%s' "$stem" | sed -E 's/^lib[^[:space:]]+_(la|a)-//')"
  printf '%s' "$stem"
}

declare -A SOURCE_BY_STEM

build_source_index() {
  local src rel stem
  SOURCE_BY_STEM=()
  while IFS= read -r -d '' src; do
    stem="$(basename "$src")"
    stem="${stem%.*}"
    rel="$(rel_to_root "$src")"
    if [ -z "${SOURCE_BY_STEM[$stem]+x}" ]; then
      SOURCE_BY_STEM["$stem"]="$rel"
    fi
  done < <(
    find "$SRC_DIR" \
      \( -path '*/.git/*' -o -path '*/.deps/*' \) -prune -o \
      -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.m' -o -name '*.mm' \) \
      -print0
  )
}

source_for_object() {
  local object_path="$1"
  local member_name="${2:-}"
  local stem raw_stem stripped dir candidate ext

  raw_stem="$(basename "${member_name:-$object_path}")"
  raw_stem="${raw_stem%.*}"
  stripped="$(strip_libtool_prefix "$raw_stem")"
  dir="$(dirname "$object_path")"
  dir="${dir#"$OUT_DIR/.tmp-archives/"}"

  for stem in "$raw_stem" "$stripped"; do
    for ext in c cc cpp cxx m mm; do
      candidate="$(dirname "$object_path")/$stem.$ext"
      if [ -f "$candidate" ]; then
        rel_to_root "$candidate"
        return
      fi
    done
    if [ -n "${SOURCE_BY_STEM[$stem]+x}" ]; then
      printf '%s' "${SOURCE_BY_STEM[$stem]}"
      return
    fi
  done

  printf 'SOURCE_NOT_FOUND:%s' "$raw_stem"
}

is_llvm_bitcode() {
  file "$1" | grep -qi 'LLVM.*bitcode'
}

unique_output_path() {
  local path="$1"
  local stem ext i candidate
  if [ ! -e "$path" ]; then
    printf '%s' "$path"
    return
  fi
  stem="${path%.*}"
  ext=".${path##*.}"
  i=2
  while true; do
    candidate="${stem}.${i}${ext}"
    if [ ! -e "$candidate" ]; then
      printf '%s' "$candidate"
      return
    fi
    i=$((i + 1))
  done
}

BC_COUNT=0
MANIFEST=""

copy_bitcode() {
  local input="$1"
  local output_rel="$2"
  local source_file="$3"
  local origin="$4"
  local archive_member="${5:-}"
  local output_path rel_bc

  output_path="$OUT_DIR/$output_rel"
  output_path="${output_path%.*}.bc"
  output_path="$(unique_output_path "$output_path")"
  mkdir -p "$(dirname "$output_path")"
  cp "$input" "$output_path"

  rel_bc="$(rel_to_root "$output_path")"
  printf '%s\t%s\t%s\t%s\n' "$rel_bc" "$source_file" "$origin" "$archive_member" >> "$MANIFEST"
  BC_COUNT=$((BC_COUNT + 1))
}

collect_direct_bitcode() {
  local file_path rel output_rel source_file
  echo "[+] Collecting direct bitcode object files..."
  while IFS= read -r -d '' file_path; do
    is_llvm_bitcode "$file_path" || continue
    rel="${file_path#$SRC_DIR/}"
    output_rel="objects/$rel"
    source_file="$(source_for_object "$file_path")"
    copy_bitcode "$file_path" "$output_rel" "$source_file" "object" ""
  done < <(
    find "$SRC_DIR" \
      \( -path '*/.git/*' -o -path '*/.deps/*' -o -path '*/.libs/*' -o -path '*/tmp_extract_*/*' \) -prune -o \
      -type f \( -name '*.o' -o -name '*.bc' \) -newer "$BUILD_MARKER" \
      -print0
  )
}

collect_archive_bitcode() {
  local archive archive_rel archive_group tmpdir member member_base source_file output_rel
  echo "[+] Collecting bitcode members from static archives..."
  while IFS= read -r -d '' archive; do
    if [ "$COLLECT_ALL_ARCHIVES" != "1" ] && [[ "$archive" != */.libs/*.a ]]; then
      continue
    fi

    archive_rel="${archive#$SRC_DIR/}"
    archive_group="${archive_rel%.a}"
    tmpdir="$OUT_DIR/.tmp-archives/${archive_group//\//__}"
    rm -rf "$tmpdir"
    mkdir -p "$tmpdir"

    (
      cd "$tmpdir"
      "$LLVM_AR_BIN" x "$archive"
    )

    while IFS= read -r -d '' member; do
      is_llvm_bitcode "$member" || continue
      member_base="$(basename "$member")"
      source_file="$(source_for_object "$archive" "$member_base")"
      output_rel="archives/$archive_group/$member_base"
      copy_bitcode "$member" "$output_rel" "$source_file" "archive:$(rel_to_root "$archive")" "$member_base"
    done < <(find "$tmpdir" -type f \( -name '*.o' -o -name '*.bc' \) -print0)
  done < <(
    find "$SRC_DIR" \
      \( -path '*/.git/*' -o -path '*/.deps/*' \) -prune -o \
      -type f -name '*.a' -newer "$BUILD_MARKER" \
      -print0
  )
}

collect_bitcode() {
  echo "[+] Collecting .bc files..."
  rm -rf "$OUT_DIR"
  mkdir -p "$OUT_DIR"

  MANIFEST="$OUT_DIR/manifest.tsv"
  printf 'bc_file\tsource_file\torigin\tarchive_member\n' > "$MANIFEST"

  build_source_index
  collect_direct_bitcode
  collect_archive_bitcode
  rm -rf "$OUT_DIR/.tmp-archives"
}

create_clang_wrapper "$WRAPPER_CC" "$CLANG_BIN"
create_clang_wrapper "$WRAPPER_CXX" "$CLANGXX_BIN"

cd "$SRC_DIR"
run_make_clean
configure_project
validate_configure_object_extension

BUILD_MARKER="$LOG_DIR/${PROJECT_NAME}.build-start"
rm -f "$BUILD_MARKER"
touch "$BUILD_MARKER"

build_project
collect_bitcode

echo "[+] Done."
echo "[+] Output directory : $OUT_DIR"
echo "[+] Number of .bc    : $BC_COUNT"
echo "[+] Manifest         : $MANIFEST"

if [ "$BC_COUNT" -eq 0 ]; then
  echo "[-] No .bc files found."
  echo "[-] If the project only keeps bitcode inside non-libtool archives, rerun with COLLECT_ALL_ARCHIVES=1."
  exit 1
fi

echo "[+] Sample mapping:"
head -n 20 "$MANIFEST"

echo "[+] File type check:"
find "$OUT_DIR" -name '*.bc' | head -n 5 | xargs -r file
