#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NSPA_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROJECT_NAME="git"
SRC_DIR="${1:-$NSPA_ROOT/open-source-soft/git-master}"
OUT_DIR="${2:-$NSPA_ROOT/workspace/git-bc}"

source "$SCRIPT_DIR/build_common.sh"

init_project_build
cd "$SRC_DIR"
run_make_clean
build_with_make \
  NO_CURL=YesPlease \
  NO_EXPAT=YesPlease \
  NO_GETTEXT=YesPlease \
  NO_ICONV=YesPlease \
  NO_OPENSSL=YesPlease \
  NO_PERL=YesPlease \
  NO_PYTHON=YesPlease \
  NO_REGEX=NeedsStartEnd \
  NO_RUST=YesPlease \
  NO_TCLTK=YesPlease \
  git
finish_build
