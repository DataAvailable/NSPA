#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

# 当前文件位于 ~/Projects/NSPA/nspa/
NSPA_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(NSPA_ROOT))

from nspa.project_inventory import (  # noqa: E402
    DEFAULT_EXCLUDE_DIRS,
    code_line_count,
    function_count,
    iter_source_files,
    read_text,
)


SOURCE_ROOT = NSPA_ROOT / "open-source-soft" / "openssl-master"
BC_ROOT = NSPA_ROOT / "workspace" / "openssl-bc" / "bc"


def collect_tlc_and_func(source_root: Path) -> tuple[int, int]:
    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)

    # OpenSSL 中这些目录通常不计入核心源码统计，可按需要删减
    exclude_dirs.update(
        {
            "test",
            "tests",
            "fuzz",
            "demos",
            "doc",
            "docs",
            "engines",
            "ms",
            "VMS",
            "util",
        }
    )

    tlc = 0
    funcs = 0

    for source_file in iter_source_files(source_root, exclude_dirs):
        text = read_text(source_file)
        tlc += code_line_count(text)
        funcs += function_count(text)

    return tlc, funcs


def collect_bc_count(bc_root: Path) -> int:
    if not bc_root.is_dir():
        return 0

    return sum(
        1
        for path in bc_root.rglob("*.bc")
        if path.is_file()
    )


def format_count(value: int) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if value >= 1_000:
        return f"{value / 1_000:.1f}K"
    return str(value)


def main() -> int:
    if not SOURCE_ROOT.is_dir():
        print(f"[-] OpenSSL source root not found: {SOURCE_ROOT}")
        return 1

    tlc, funcs = collect_tlc_and_func(SOURCE_ROOT)
    bc_count = collect_bc_count(BC_ROOT)

    print("[+] OpenSSL statistics")
    print(f"[+] Source root : {SOURCE_ROOT}")
    print(f"[+] BC root     : {BC_ROOT}")
    print()
    print(f"TLC   : {tlc} ({format_count(tlc)})")
    print(f"#Func : {funcs} ({format_count(funcs)})")
    print(f"#BC   : {bc_count}")
    print()
    print("LaTeX fields:")
    print(f"& {format_count(tlc)} & {format_count(funcs)} & {bc_count} &")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())