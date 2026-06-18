#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
NSPA_ROOT = SCRIPT_DIR.parent

sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(NSPA_ROOT))

from project_inventory import (
    DEFAULT_EXCLUDE_DIRS,
    code_line_count,
    function_count,
    iter_source_files,
    read_text,
    extract_version,
)


OPEN_SOURCE_ROOT = NSPA_ROOT / "open-source-soft"
WORKSPACE_ROOT = NSPA_ROOT / "workspace"


@dataclass
class ProjectSpec:
    name: str
    type_name: str
    source_candidates: list[str]
    ir_name: str
    linked_bc_candidates: list[str]


PROJECTS = [
    ProjectSpec(
        name="htop",
        type_name="System monitor",
        source_candidates=["htop-master", "htop"],
        ir_name="htop",
        linked_bc_candidates=["project.bc"],
    ),
    ProjectSpec(
        name="lighttpd",
        type_name="Web server",
        source_candidates=["lighttpd-1.4.83", "lighttpd-master", "lighttpd"],
        ir_name="lighttpd",
        linked_bc_candidates=["project.bc"],
    ),
    ProjectSpec(
        name="nasm",
        type_name="Assembler",
        source_candidates=["nasm-master", "nasm"],
        ir_name="nasm",
        linked_bc_candidates=["project.bc"],
    ),
    ProjectSpec(
        name="redis",
        type_name="In-memory database",
        source_candidates=["redis-master", "redis"],
        ir_name="redis",
        linked_bc_candidates=["redis-server.bc", "project.bc"],
    ),
    ProjectSpec(
        name="screen",
        type_name="Terminal tool",
        source_candidates=["screen-master/src", "screen-5.0.1", "screen-master"],
        ir_name="screen",
        linked_bc_candidates=["project.bc"],
    ),
]


def first_existing_dir(candidates: list[str]) -> Path | None:
    for item in candidates:
        path = OPEN_SOURCE_ROOT / item
        if path.is_dir():
            return path
    return None


def format_count(value: int) -> str:
    if value >= 1_000_000:
        return f"{value / 1_000_000:.1f}M"
    if value >= 1_000:
        return f"{value / 1_000:.1f}K"
    return str(value)


def format_mb(size_bytes: int) -> str:
    mb = size_bytes / 1024 / 1024
    if mb >= 10:
        return f"{mb:.1f}M"
    return f"{mb:.2f}M"


def extract_redis_version(project_dir: Path) -> str | None:
    candidates = [
        project_dir / "src" / "version.h",
        project_dir / "src" / "release.h",
    ]
    for path in candidates:
        if not path.is_file():
            continue
        text = read_text(path)
        patterns = [
            r'#\s*define\s+REDIS_VERSION\s+"([^"]+)"',
            r'#\s*define\s+REDIS_VERSION_NUM\s+([0-9]+)',
        ]
        for pat in patterns:
            m = re.search(pat, text)
            if m:
                return m.group(1)
    return None


def extract_lighttpd_version(project_dir: Path) -> str | None:
    # 优先从 configure.ac / CMakeLists.txt / meson.build 中提取。
    candidates = [
        project_dir / "configure.ac",
        project_dir / "CMakeLists.txt",
        project_dir / "meson.build",
    ]
    for path in candidates:
        if not path.is_file():
            continue
        text = read_text(path)

        m = re.search(r"AC_INIT\(\s*\[[^\]]+\]\s*,\s*\[?([0-9][^\],)]+)\]?", text)
        if m:
            return m.group(1).strip()

        m = re.search(r"project\s*\([^,]+,\s*['\"]c['\"].*?version\s*:\s*['\"]([^'\"]+)['\"]", text, re.S)
        if m:
            return m.group(1).strip()

        m = re.search(r"VERSION\s+([0-9]+\.[0-9]+(?:\.[0-9]+)?)", text)
        if m:
            return m.group(1).strip()

    # 如果目录名是 lighttpd-1.4.83，则直接取目录名版本。
    m = re.search(r"lighttpd-([0-9]+\.[0-9]+(?:\.[0-9]+)?)", project_dir.name)
    if m:
        return m.group(1)

    return None


def extract_screen_version(project_dir: Path) -> str | None:
    # screen-master/src 下通常有 configure.ac
    version = extract_version(project_dir, "screen")
    if version != "unknown":
        return version

    # 如果传入的是 screen-master，尝试 screen-master/src/configure.ac
    src = project_dir / "src"
    if src.is_dir():
        version = extract_version(src, "screen")
        if version != "unknown":
            return version

    m = re.search(r"screen-([0-9]+\.[0-9]+(?:\.[0-9]+)?)", project_dir.name)
    if m:
        return m.group(1)

    return None


def get_version(project: str, project_dir: Path) -> str:
    if project == "redis":
        return extract_redis_version(project_dir) or extract_version(project_dir, project)
    if project == "lighttpd":
        return extract_lighttpd_version(project_dir) or extract_version(project_dir, project)
    if project == "screen":
        return extract_screen_version(project_dir) or "unknown"

    version = extract_version(project_dir, project)
    return version


def collect_source_stats(project_dir: Path) -> tuple[int, int]:
    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)
    tlc = 0
    funcs = 0

    for source_file in iter_source_files(project_dir, exclude_dirs):
        text = read_text(source_file)
        tlc += code_line_count(text)
        funcs += function_count(text)

    return tlc, funcs


def count_per_source_bc(ir_name: str) -> int:
    bc_root = WORKSPACE_ROOT / f"{ir_name}-bc" / "bc"
    if not bc_root.is_dir():
        return 0
    return sum(1 for p in bc_root.rglob("*.bc") if p.is_file())


def linked_ir_size(ir_name: str, candidates: list[str]) -> int:
    ir_root = WORKSPACE_ROOT / f"{ir_name}-bc"
    for item in candidates:
        path = ir_root / item
        if path.is_file():
            return path.stat().st_size
    return 0


def latex_escape(text: str) -> str:
    return text.replace("_", r"\_")


def main() -> int:
    print("Project,Ver,Type,TLC,#Func,#BC,IRSize")
    latex_rows = []

    for spec in PROJECTS:
        src_dir = first_existing_dir(spec.source_candidates)
        if src_dir is None:
            print(f"{spec.name},MISSING_SOURCE,{spec.type_name},0,0,0,0")
            continue

        version = get_version(spec.name, src_dir)
        tlc, funcs = collect_source_stats(src_dir)
        bc_count = count_per_source_bc(spec.ir_name)
        ir_size = linked_ir_size(spec.ir_name, spec.linked_bc_candidates)

        tlc_s = format_count(tlc)
        funcs_s = format_count(funcs)
        ir_s = format_mb(ir_size) if ir_size > 0 else "0M"

        print(f"{spec.name},{version},{spec.type_name},{tlc_s},{funcs_s},{bc_count},{ir_s}")

        latex_rows.append(
            f"\\t\\t\\t      {spec.name:<10} &    {version:<8} & {latex_escape(spec.type_name):<18} "
            f"&    {tlc_s:<8} &      {funcs_s:<8} &      {bc_count:<5} &      {ir_s:<8} &  \\\\"
        )

    print()
    print("LaTeX rows:")
    for row in latex_rows:
        print(row)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())