#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="curl"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/curl-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/curl-bc}"

source "$SCRIPT_DIR/build_common.sh"

init_project_build
cd "$SRC_DIR"
run_make_clean
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
build_with_make
LINK_OBJECT_REGEX='^(src/curl-|src/toolx/curl-|lib/libcurl_la-|lib/(curlx|vauth|vtls|vquic|vssh)/libcurl_la-)'
finish_build
