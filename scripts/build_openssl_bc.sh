#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="openssl"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/openssl-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/openssl-bc}"

source "$SCRIPT_DIR/build_common.sh"

init_project_build
cd "$SRC_DIR"
run_make_clean
if [ ! -x ./Configure ]; then
  echo "[-] OpenSSL Configure script not found: $SRC_DIR/Configure"
  exit 1
fi
env CC="$CLANG_BIN" CXX="$CLANGXX_BIN" ./Configure \
  no-shared \
  no-tests \
  no-docs \
  no-asm \
  --debug \
  -O0 \
  -g
build_with_make
LINK_ORIGIN=archive
LINK_OBJECT_REGEX='^(libcrypto[.]a:|libssl[.]a:)'
finish_build
