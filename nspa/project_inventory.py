"""Inventory open-source projects and their generated LLVM IR footprint."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable

from nspa.memory_function_detector import (
    iter_regex_function_definitions,
    mask_comments_and_strings,
)


SOURCE_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".m",
    ".mm",
}

DEFAULT_EXCLUDE_DIRS = {
    ".deps",
    ".git",
    ".libs",
    ".svn",
    "__pycache__",
    "autom4te.cache",
    "build",
    "CMakeFiles",
    "tmp_extract_libcurl",
}


@dataclass(frozen=True)
class ProjectInventory:
    project: str
    path: str
    version: str
    tlc: int
    functions: int
    ir_size_bytes: int
    ir_files: int


def project_name(project_dir: Path) -> str:
    name = project_dir.name
    for suffix in ("-master", "-src"):
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return name


def iter_project_dirs(open_source_root: Path) -> Iterable[Path]:
    for path in sorted(open_source_root.iterdir()):
        if path.is_dir() and not path.name.startswith("."):
            yield path


def should_skip(path: Path, exclude_dirs: set[str]) -> bool:
    return any(part in exclude_dirs or part.startswith("tmp_extract_") for part in path.parts)


def iter_source_files(project_dir: Path, exclude_dirs: set[str]) -> Iterable[Path]:
    for path in sorted(project_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(project_dir)
        if should_skip(rel.parent, exclude_dirs):
            continue
        if path.suffix.lower() in SOURCE_EXTENSIONS:
            yield path


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def code_line_count(source_text: str) -> int:
    masked = mask_comments_and_strings(source_text)
    return sum(1 for line in masked.splitlines() if line.strip())


def function_count(source_text: str) -> int:
    masked = mask_comments_and_strings(source_text)
    return sum(1 for _ in iter_regex_function_definitions(source_text, masked))


def macro_value(path: Path, name: str) -> str | None:
    if not path.is_file():
        return None
    pattern = re.compile(rf"^\s*#\s*define\s+{re.escape(name)}\s+(.+?)\s*$", re.MULTILINE)
    match = pattern.search(read_text(path))
    return match.group(1).strip() if match else None


def ac_init_version(path: Path) -> str | None:
    if not path.is_file():
        return None
    text = read_text(path)
    match = re.search(r"AC_INIT\(\s*\[[^\]]+\]\s*,\s*\[?([^\],)]+)\]?", text)
    if match:
        return match.group(1).strip()
    return None


def extract_version(project_dir: Path, name: str) -> str:
    extractors = {
        "bash": extract_bash_version,
        "curl": extract_curl_version,
        "ffmpeg": extract_ffmpeg_version,
        "git": extract_git_version,
        "openssl": extract_openssl_version,
        "sqlite": extract_sqlite_version,
        "tmux": extract_tmux_version,
        "vim": extract_vim_version,
    }
    extractor = extractors.get(name)
    if extractor is not None:
        version = extractor(project_dir)
        if version:
            return version

    for relative in ("VERSION", "version", "RELEASE", "configure.ac"):
        path = project_dir / relative
        if path.is_file():
            if relative == "configure.ac":
                version = ac_init_version(path)
                if version:
                    return version
            else:
                first = read_text(path).strip().splitlines()
                if first:
                    return first[0].strip()
    return "unknown"


def extract_bash_version(project_dir: Path) -> str | None:
    configure_ac = project_dir / "configure.ac"
    patchlevel = project_dir / "patchlevel.h"
    text = read_text(configure_ac) if configure_ac.is_file() else ""
    base = re.search(r"define\(\s*bashvers\s*,\s*([^)]+)\)", text)
    status = re.search(r"define\(\s*relstatus\s*,\s*([^)]+)\)", text)
    patch = macro_value(patchlevel, "PATCHLEVEL")
    if not base:
        return None
    version = base.group(1).strip()
    if patch:
        version = f"{version}.{patch}"
    if status and status.group(1).strip() != "release":
        version = f"{version}-{status.group(1).strip()}"
    return version


def extract_curl_version(project_dir: Path) -> str | None:
    path = project_dir / "include" / "curl" / "curlver.h"
    if not path.is_file():
        return None
    match = re.search(r'#\s*define\s+LIBCURL_VERSION\s+"([^"]+)"', read_text(path))
    return match.group(1) if match else None


def extract_ffmpeg_version(project_dir: Path) -> str | None:
    for relative in ("VERSION", "RELEASE"):
        path = project_dir / relative
        if path.is_file():
            value = read_text(path).strip()
            if value:
                return value.splitlines()[0].strip()
    return None


def extract_git_version(project_dir: Path) -> str | None:
    version_file = project_dir / "version"
    if version_file.is_file():
        return read_text(version_file).strip()
    gen = project_dir / "GIT-VERSION-GEN"
    if not gen.is_file():
        return None
    match = re.search(r"^DEF_VER=v?(.+)$", read_text(gen), re.MULTILINE)
    return match.group(1).strip() if match else None


def extract_openssl_version(project_dir: Path) -> str | None:
    path = project_dir / "VERSION.dat"
    if not path.is_file():
        return None
    values: dict[str, str] = {}
    for line in read_text(path).splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"')
    parts = [values.get("MAJOR"), values.get("MINOR"), values.get("PATCH")]
    if not all(parts):
        return None
    version = ".".join(part or "0" for part in parts)
    pre = values.get("PRE_RELEASE_TAG", "")
    build = values.get("BUILD_METADATA", "")
    if pre:
        version = f"{version}-{pre}"
    if build:
        version = f"{version}+{build}"
    return version


def extract_sqlite_version(project_dir: Path) -> str | None:
    path = project_dir / "VERSION"
    if not path.is_file():
        return None
    return read_text(path).strip()


def extract_tmux_version(project_dir: Path) -> str | None:
    return ac_init_version(project_dir / "configure.ac")


def extract_vim_version(project_dir: Path) -> str | None:
    path = project_dir / "src" / "version.h"
    if not path.is_file():
        return None
    text = read_text(path)
    values: dict[str, str] = {}
    for key in ("VIM_VERSION_MAJOR", "VIM_VERSION_MINOR", "VIM_VERSION_BUILD"):
        match = re.search(rf"^\s*#\s*define\s+{key}\s+([0-9]+)", text, re.MULTILINE)
        if match:
            values[key] = match.group(1)
    if "VIM_VERSION_MAJOR" not in values or "VIM_VERSION_MINOR" not in values:
        return None
    version = f"{values['VIM_VERSION_MAJOR']}.{values['VIM_VERSION_MINOR']}"
    if values.get("VIM_VERSION_BUILD"):
        version = f"{version}.{values['VIM_VERSION_BUILD']}"
    return version


def ir_directory(ir_root: Path, name: str) -> Path:
    return ir_root / f"{name}-bc"


def collect_ir_size(ir_root: Path, name: str) -> tuple[int, int]:
    root = ir_directory(ir_root, name)
    if not root.is_dir():
        return 0, 0
    size = 0
    count = 0
    for path in root.rglob("*.bc"):
        if path.is_file():
            size += path.stat().st_size
            count += 1
    return size, count


def collect_project(project_dir: Path, ir_root: Path, exclude_dirs: set[str]) -> ProjectInventory:
    name = project_name(project_dir)
    tlc = 0
    functions = 0
    for source_file in iter_source_files(project_dir, exclude_dirs):
        text = read_text(source_file)
        tlc += code_line_count(text)
        functions += function_count(text)
    ir_size, ir_files = collect_ir_size(ir_root, name)
    return ProjectInventory(
        project=name,
        path=str(project_dir),
        version=extract_version(project_dir, name),
        tlc=tlc,
        functions=functions,
        ir_size_bytes=ir_size,
        ir_files=ir_files,
    )


def write_csv(rows: list[ProjectInventory], output: Path) -> None:
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["project", "version", "tlc", "functions", "ir_size_bytes", "ir_files", "path"],
        )
        writer.writeheader()
        for row in rows:
            writer.writerow(asdict(row))


def print_table(rows: list[ProjectInventory]) -> None:
    headers = ["Project", "Version", "TLC", "#Func", "IRSize", "IRFiles"]
    data = [
        [
            row.project,
            row.version,
            str(row.tlc),
            str(row.functions),
            str(row.ir_size_bytes),
            str(row.ir_files),
        ]
        for row in rows
    ]
    widths = [len(header) for header in headers]
    for row in data:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))
    print("  ".join(header.ljust(widths[index]) for index, header in enumerate(headers)))
    print("  ".join("-" * width for width in widths))
    for row in data:
        print("  ".join(value.ljust(widths[index]) for index, value in enumerate(row)))


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Collect Version, TLC, #Func, and generated LLVM IR size for open-source projects."
    )
    parser.add_argument("--open-source-root", type=Path, default=Path("open-source-soft"))
    parser.add_argument("--ir-root", type=Path, default=Path("workspace"))
    parser.add_argument("--project", action="append", help="Project name to include; repeatable.")
    parser.add_argument("--output", type=Path, help="Write CSV report to this path.")
    parser.add_argument("--json", type=Path, help="Write JSON report to this path.")
    parser.add_argument(
        "--exclude-dir",
        action="append",
        default=[],
        help="Additional directory name to exclude while counting source code.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    root = args.open_source_root.resolve()
    ir_root = args.ir_root.resolve()
    if not root.is_dir():
        print(f"open-source root not found: {root}", file=sys.stderr)
        return 2

    requested = {project_name(Path(item)) for item in args.project or []}
    exclude_dirs = set(DEFAULT_EXCLUDE_DIRS)
    exclude_dirs.update(args.exclude_dir)
    rows = [
        collect_project(project_dir, ir_root, exclude_dirs)
        for project_dir in iter_project_dirs(root)
        if not requested or project_name(project_dir) in requested
    ]

    print_table(rows)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        write_csv(rows, args.output)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps([asdict(row) for row in rows], indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
