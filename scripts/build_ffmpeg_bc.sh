#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="ffmpeg"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/ffmpeg-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/ffmpeg-bc}"

source "$SCRIPT_DIR/build_common.sh"

init_project_build
cd "$SRC_DIR"
run_make_clean
chmod +x configure
./configure \
  --cc="$CLANG_BIN" \
  --cxx="$CLANGXX_BIN" \
  --ar="$LLVM_AR_BIN" \
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
log_file="$LOG_DIR/build.log"
set +e
"$MAKE_BIN" -k -j"$JOBS" CC="$WRAPPER_CC" CXX="$WRAPPER_CXX" 2>&1 | tee "$log_file"
make_ret=${PIPESTATUS[0]}
set -e
if [ "$make_ret" -ne 0 ]; then
  echo "[!] make returned non-zero. The script will still collect generated IR."
fi
LINK_ORIGIN=archive
LINK_OBJECT_REGEX='^(libavdevice/libavdevice[.]a:|libavfilter/libavfilter[.]a:|libavformat/libavformat[.]a:|libavcodec/libavcodec[.]a:|libswresample/libswresample[.]a:|libswscale/libswscale[.]a:|libavutil/libavutil[.]a:)'
LINK_EXCLUDE_REGEX='^libswscale/libswscale[.]a:framepool[.]o$'
finish_build
