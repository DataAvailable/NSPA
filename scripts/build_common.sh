#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CLANG_BIN="${CLANG_BIN:-clang}"
CLANGXX_BIN="${CLANGXX_BIN:-clang++}"
LLVM_AR_BIN="${LLVM_AR_BIN:-llvm-ar}"
LLVM_LINK_BIN="${LLVM_LINK_BIN:-llvm-link}"
MAKE_BIN="${MAKE_BIN:-make}"
JOBS="${JOBS:-$(nproc)}"
BASE_CFLAGS="${NSPA_CFLAGS:--O0 -g}"
BASE_CXXFLAGS="${NSPA_CXXFLAGS:--O0 -g}"
BASE_CPPFLAGS="${NSPA_CPPFLAGS:-${CPPFLAGS:-}}"
BASE_LDFLAGS="${NSPA_LDFLAGS:-${LDFLAGS:-}}"

PROJECT_NAME="${PROJECT_NAME:?PROJECT_NAME must be set before sourcing build_common.sh}"
SRC_DIR="${SRC_DIR:?SRC_DIR must be set before sourcing build_common.sh}"
OUT_DIR="${OUT_DIR:-$NSPA_ROOT/workspace/${PROJECT_NAME}-bc}"
LOG_DIR="$OUT_DIR/logs"
WRAPPER_DIR="$OUT_DIR/wrappers"
WRAPPER_CC="$WRAPPER_DIR/clang-bc"
WRAPPER_CXX="$WRAPPER_DIR/clangxx-bc"
MANIFEST="$OUT_DIR/manifest.tsv"
FULL_BC="$OUT_DIR/project.bc"
BUILD_MARKER="$OUT_DIR/build-start.marker"

normalize_paths() {
  SRC_DIR="$(cd "$SRC_DIR" && pwd)"
  OUT_DIR="$(mkdir -p "$OUT_DIR" && cd "$OUT_DIR" && pwd)"
  LOG_DIR="$OUT_DIR/logs"
  WRAPPER_DIR="$OUT_DIR/wrappers"
  WRAPPER_CC="$WRAPPER_DIR/clang-bc"
  WRAPPER_CXX="$WRAPPER_DIR/clangxx-bc"
  MANIFEST="$OUT_DIR/manifest.tsv"
  FULL_BC="$OUT_DIR/project.bc"
  BUILD_MARKER="$OUT_DIR/build-start.marker"
}

require_tool() {
  local tool="$1"
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "[-] Required tool not found in PATH: $tool"
    exit 1
  fi
}

check_required_tools() {
  require_tool "$CLANG_BIN"
  require_tool "$CLANGXX_BIN"
  require_tool "$LLVM_AR_BIN"
  require_tool "$LLVM_LINK_BIN"
  require_tool "$MAKE_BIN"
  require_tool file
}

create_clang_wrapper() {
  mkdir -p "$WRAPPER_DIR"
  cat > "$WRAPPER_CC" <<EOF
#!/usr/bin/env bash
has_compile=0
has_output=0
source_file=""
for arg in "\$@"; do
  if [ "\$arg" = "-c" ]; then
    has_compile=1
  elif [ "\$arg" = "-o" ]; then
    has_output=1
  elif [[ "\$arg" == *.c || "\$arg" == *.m ]]; then
    source_file="\$arg"
  fi
done
if [ "\$has_compile" = "1" ]; then
  if [ "\$has_output" = "0" ] && [ -n "\$source_file" ]; then
    output="\${source_file##*/}"
    output="\${output%.*}.o"
    exec "$CLANG_BIN" -emit-llvm -fno-discard-value-names "\$@" -o "\$output"
  fi
  exec "$CLANG_BIN" -emit-llvm -fno-discard-value-names "\$@"
fi
exec "$CLANG_BIN" "\$@"
EOF
  cat > "$WRAPPER_CXX" <<EOF
#!/usr/bin/env bash
has_compile=0
has_output=0
source_file=""
for arg in "\$@"; do
  if [ "\$arg" = "-c" ]; then
    has_compile=1
  elif [ "\$arg" = "-o" ]; then
    has_output=1
  elif [[ "\$arg" == *.cc || "\$arg" == *.cpp || "\$arg" == *.cxx || "\$arg" == *.C || "\$arg" == *.mm ]]; then
    source_file="\$arg"
  fi
done
if [ "\$has_compile" = "1" ]; then
  if [ "\$has_output" = "0" ] && [ -n "\$source_file" ]; then
    output="\${source_file##*/}"
    output="\${output%.*}.o"
    exec "$CLANGXX_BIN" -emit-llvm -fno-discard-value-names "\$@" -o "\$output"
  fi
  exec "$CLANGXX_BIN" -emit-llvm -fno-discard-value-names "\$@"
fi
exec "$CLANGXX_BIN" "\$@"
EOF
  chmod +x "$WRAPPER_CC" "$WRAPPER_CXX"
}

prepare_output_dir() {
  rm -rf "$OUT_DIR/objects" "$OUT_DIR/archive-objects" "$OUT_DIR/link-work"
  mkdir -p "$OUT_DIR/objects" "$OUT_DIR/archive-objects" "$OUT_DIR/link-work" "$LOG_DIR"
  printf 'bc_file\tsource_or_member\torigin\n' > "$MANIFEST"
  rm -f "$FULL_BC"
  touch "$BUILD_MARKER"
}

print_context() {
  echo "[+] Project       : $PROJECT_NAME"
  echo "[+] Source dir    : $SRC_DIR"
  echo "[+] Output dir    : $OUT_DIR"
  echo "[+] Full project  : $FULL_BC"
  echo "[+] Manifest      : $MANIFEST"
  echo "[+] CC wrapper    : $WRAPPER_CC"
  echo "[+] CXX wrapper   : $WRAPPER_CXX"
  echo "[+] Jobs          : $JOBS"
}

run_make_clean() {
  if [ -f Makefile ] || [ -f makefile ] || [ -f GNUmakefile ]; then
    "$MAKE_BIN" distclean >/dev/null 2>&1 || "$MAKE_BIN" clean >/dev/null 2>&1 || true
  fi
}

ensure_configure() {
  if [ -f configure ]; then
    chmod +x configure
    return
  fi
  if [ -x ./buildconf ]; then
    ./buildconf
  elif [ -x ./autogen.sh ]; then
    ./autogen.sh
  elif command -v autoreconf >/dev/null 2>&1; then
    autoreconf -fi
  else
    echo "[-] configure not found and no generator is available in $SRC_DIR"
    exit 1
  fi
  chmod +x configure
}

configure_autotools() {
  local flags=("$@")
  ensure_configure
  env \
    CC="$CLANG_BIN" \
    CXX="$CLANGXX_BIN" \
    CPPFLAGS="$BASE_CPPFLAGS" \
    CFLAGS="$BASE_CFLAGS" \
    CXXFLAGS="$BASE_CXXFLAGS" \
    LDFLAGS="$BASE_LDFLAGS" \
    ./configure "${flags[@]}"
}

build_with_make() {
  local log_file="$LOG_DIR/build.log"
  set +e
  "$MAKE_BIN" -k -j"$JOBS" \
    CC="$WRAPPER_CC" \
    CXX="$WRAPPER_CXX" \
    CPPFLAGS="$BASE_CPPFLAGS" \
    CFLAGS="$BASE_CFLAGS" \
    CXXFLAGS="$BASE_CXXFLAGS" \
    LDFLAGS="$BASE_LDFLAGS" \
    "$@" \
    2>&1 | tee "$log_file"
  local make_ret=${PIPESTATUS[0]}
  set -e
  if [ "$make_ret" -ne 0 ]; then
    echo "[!] make returned non-zero. The script will still collect generated IR."
  fi
}

is_llvm_bitcode() {
  file "$1" | grep -qi 'LLVM.*bitcode'
}

manifest_append() {
  local bc_file="$1"
  local source_or_member="$2"
  local origin="$3"
  printf '%s\t%s\t%s\n' "$bc_file" "$source_or_member" "$origin" >> "$MANIFEST"
}

unique_path() {
  local path="$1"
  local stem ext candidate i
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

copy_direct_bitcode() {
  local input="$1"
  local rel output
  rel="${input#$SRC_DIR/}"
  output="$OUT_DIR/objects/${rel%.*}.bc"
  output="$(unique_path "$output")"
  mkdir -p "$(dirname "$output")"
  cp "$input" "$output"
  manifest_append "$output" "$rel" "object"
}

collect_direct_bitcode() {
  local input
  echo "[+] Collecting per-source LLVM bitcode objects..."
  while IFS= read -r -d '' input; do
    is_llvm_bitcode "$input" || continue
    copy_direct_bitcode "$input"
  done < <(
    find "$SRC_DIR" \
      \( -path '*/.git/*' -o -path '*/.deps/*' -o -path '*/autom4te.cache/*' \) -prune -o \
      -type f \( -name '*.o' -o -name '*.bc' \) \
      -newer "$BUILD_MARKER" \
      -print0
  )
}

collect_archive_bitcode() {
  local archive rel group tmp member output member_base
  echo "[+] Collecting LLVM bitcode members from static archives..."
  while IFS= read -r -d '' archive; do
    rel="${archive#$SRC_DIR/}"
    group="${rel%.a}"
    tmp="$OUT_DIR/link-work/archive-${group//\//__}"
    rm -rf "$tmp"
    mkdir -p "$tmp"
    (
      cd "$tmp"
      "$LLVM_AR_BIN" x "$archive" >/dev/null 2>&1 || true
    )
    while IFS= read -r -d '' member; do
      is_llvm_bitcode "$member" || continue
      member_base="$(basename "$member")"
      output="$OUT_DIR/archive-objects/${group}/${member_base%.*}.bc"
      output="$(unique_path "$output")"
      mkdir -p "$(dirname "$output")"
      cp "$member" "$output"
      manifest_append "$output" "$rel:$member_base" "archive"
    done < <(find "$tmp" -type f \( -name '*.o' -o -name '*.bc' \) -print0)
  done < <(
    find "$SRC_DIR" \
      \( -path '*/.git/*' -o -path '*/.deps/*' -o -path '*/autom4te.cache/*' \) -prune -o \
      -type f -name '*.a' \
      -newer "$BUILD_MARKER" \
      -print0
  )
}

count_collected_bc() {
  find "$OUT_DIR/objects" "$OUT_DIR/archive-objects" -type f -name '*.bc' | wc -l
}

link_file_list() {
  local origin_filter="$1"
  if [ -n "${LINK_OBJECT_REGEX:-}" ]; then
    awk -F '\t' -v origin="$origin_filter" -v pattern="$LINK_OBJECT_REGEX" -v exclude="${LINK_EXCLUDE_REGEX:-}" \
      'NR > 1 && $3 == origin && $2 ~ pattern && (exclude == "" || $2 !~ exclude) { print $1 }' "$MANIFEST"
  else
    awk -F '\t' -v origin="$origin_filter" -v exclude="${LINK_EXCLUDE_REGEX:-}" \
      'NR > 1 && $3 == origin && (exclude == "" || $2 !~ exclude) { print $1 }' "$MANIFEST"
  fi
}

link_batch() {
  local output="$1"
  shift
  "$LLVM_LINK_BIN" "$@" -o "$output"
}

link_project_bc_from_stdin() {
  local stage="$1"
  local current_list="$OUT_DIR/link-work/${stage}.list"
  local next_list batch_file batch_out count round
  cat > "$current_list"
  if [ ! -s "$current_list" ]; then
    return 1
  fi

  round=0
  while true; do
    count="$(wc -l < "$current_list")"
    if [ "$count" -eq 1 ]; then
      cp "$(sed -n '1p' "$current_list")" "$FULL_BC"
      return 0
    fi

    next_list="$OUT_DIR/link-work/${stage}.${round}.next"
    rm -f "$next_list"
    split -l 80 "$current_list" "$OUT_DIR/link-work/${stage}.${round}."
    for batch_file in "$OUT_DIR"/link-work/"${stage}.${round}".*; do
      [ -f "$batch_file" ] || continue
      case "$batch_file" in
        *.next) continue ;;
      esac
      batch_out="$OUT_DIR/link-work/linked-${stage}-${round}-$(basename "$batch_file").bc"
      mapfile -t inputs < "$batch_file"
      if ! link_batch "$batch_out" "${inputs[@]}"; then
        return 1
      fi
      printf '%s\n' "$batch_out" >> "$next_list"
    done
    if [ ! -s "$next_list" ]; then
      return 1
    fi
    current_list="$next_list"
    round=$((round + 1))
  done
}

link_project_bc() {
  local link_origin="${LINK_ORIGIN:-object}"
  echo "[+] Linking complete project LLVM bitcode..."
  rm -f "$FULL_BC"
  if link_file_list "$link_origin" | link_project_bc_from_stdin "$link_origin"; then
    echo "[+] Linked $link_origin bitcode into $FULL_BC"
    return
  fi
  if [ -n "${LINK_OBJECT_REGEX:-}" ] || [ "$link_origin" != "object" ]; then
    echo "[-] Failed to link filtered $link_origin bitcode for $PROJECT_NAME."
    exit 1
  fi
  if awk -F '\t' 'NR > 1 { print $1 }' "$MANIFEST" | link_project_bc_from_stdin "all"; then
    echo "[+] Linked all collected bitcode into $FULL_BC"
    return
  fi
  echo "[-] Failed to link project bitcode: no collected .bc files."
  exit 1
}

finish_build() {
  collect_direct_bitcode
  collect_archive_bitcode
  local bc_count
  bc_count="$(count_collected_bc)"
  if [ "$bc_count" -eq 0 ]; then
    echo "[-] No LLVM bitcode files were collected for $PROJECT_NAME"
    exit 1
  fi
  link_project_bc
  if [ ! -s "$FULL_BC" ]; then
    echo "[-] Full project bitcode was not created: $FULL_BC"
    exit 1
  fi
  echo "[+] Done."
  echo "[+] Per-source .bc count : $bc_count"
  echo "[+] Full project .bc     : $FULL_BC"
  echo "[+] Manifest             : $MANIFEST"
}

init_project_build() {
  if [ ! -d "$SRC_DIR" ]; then
    echo "[-] Source directory not found: $SRC_DIR"
    exit 1
  fi
  normalize_paths
  check_required_tools
  prepare_output_dir
  create_clang_wrapper
  print_context
}
