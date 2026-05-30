#!/usr/bin/env bash
for arg in "$@"; do
  if [ "$arg" = "-c" ]; then
    exec "clang++" -emit-llvm -fno-discard-value-names "$@"
  fi
done
exec "clang++" "$@"
