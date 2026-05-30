#!/usr/bin/env python3
"""Compatibility wrapper for injecting NSPA functions into SaberCheckerAPI."""

from __future__ import annotations

import sys
from pathlib import Path

from nspa.fine_grained_reachability import (
    load_validated_memory_functions,
    patch_saber_checker_api,
)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <validated_memory_functions.json> <SaberCheckerAPI.cpp>")
        return 1

    json_path = Path(sys.argv[1])
    cpp_path = Path(sys.argv[2])
    project = "curl" if "curl" in json_path.name.lower() else json_path.stem

    functions = load_validated_memory_functions(json_path, min_confidence=0.5)
    alloc_count, free_count = patch_saber_checker_api(
        cpp_path,
        functions,
        project_tag=project,
    )

    print(f"[+] Patched: {cpp_path}")
    print(f"[+] Loaded memory functions: {len(functions)}")
    print(f"[+] Injected allocators: {alloc_count}")
    print(f"[+] Injected releasers/destroyers: {free_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

