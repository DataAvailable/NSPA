#!/usr/bin/env python3
"""Measure enhanced SVF/Saber runtime for each NSPA project.

For every selected project this script:

1. loads first-stage/LLM-validated custom memory allocation/free functions;
2. injects them into SVF's SaberCheckerAPI.cpp;
3. rebuilds the enhanced Saber binary;
4. runs Saber on the project's bitcode and records only step-4 runtime.
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

REPO_ROOT = Path(__file__).resolve().parents[1]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from nspa.fine_grained_reachability import (
    DEFAULT_CHECKERS,
    collect_bc_files,
    find_extapi_bc,
    find_saber_binary,
    format_elapsed_seconds,
    load_validated_memory_functions,
    parse_checkers,
    patch_saber_checker_api,
    run_saber_on_bitcode,
    write_saber_manifest,
)


DEFAULT_PROJECTS = ("bash", "curl", "ffmpeg", "git", "openssl", "sqlite", "tmux", "vim")


@dataclass(slots=True)
class ProjectTiming:
    project: str
    status: str
    validated_json: str
    bc_input: str
    output_dir: str
    loaded_memory_functions: int = 0
    injected_allocators: int = 0
    injected_releasers: int = 0
    rebuild_elapsed_seconds: float = 0.0
    saber_elapsed_seconds: float = 0.0
    saber_run_count: int = 0
    saber_failed_count: int = 0
    error: str = ""

    def to_dict(self) -> dict[str, object]:
        return {
            "project": self.project,
            "status": self.status,
            "validated_json": self.validated_json,
            "bc_input": self.bc_input,
            "output_dir": self.output_dir,
            "loaded_memory_functions": self.loaded_memory_functions,
            "injected_allocators": self.injected_allocators,
            "injected_releasers": self.injected_releasers,
            "rebuild_elapsed_seconds": round(self.rebuild_elapsed_seconds, 3),
            "rebuild_elapsed": format_elapsed_seconds(self.rebuild_elapsed_seconds),
            "saber_elapsed_seconds": round(self.saber_elapsed_seconds, 3),
            "saber_elapsed": format_elapsed_seconds(self.saber_elapsed_seconds),
            "saber_run_count": self.saber_run_count,
            "saber_failed_count": self.saber_failed_count,
            "error": self.error,
        }


def parse_projects(value: str) -> list[str]:
    projects = [item.strip() for item in value.split(",") if item.strip()]
    unknown = [project for project in projects if project not in DEFAULT_PROJECTS]
    if unknown:
        raise argparse.ArgumentTypeError(f"Unknown project(s): {', '.join(unknown)}")
    return projects


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Inject project-specific Saber APIs, rebuild Saber, and measure step-4 Saber runtime."
    )
    parser.add_argument("--projects", type=parse_projects, default=list(DEFAULT_PROJECTS))
    parser.add_argument("--nspa-root", type=Path, default=Path.cwd())
    parser.add_argument("--svf-build-dir", type=Path, default=Path("SVF/Release-build"))
    parser.add_argument("--saber-api-cpp", type=Path, default=Path("SVF/svf/lib/SABER/SaberCheckerAPI.cpp"))
    parser.add_argument("--outputs-root", type=Path, default=Path("outputs"))
    parser.add_argument("--workspace-root", type=Path, default=Path("workspace"))
    parser.add_argument("--saber-output-root", type=Path, default=Path("outputs/saber"))
    parser.add_argument("--summary-json", type=Path, default=Path("outputs/saber_timing_summary.json"))
    parser.add_argument("--summary-csv", type=Path, default=Path("outputs/saber_timing_summary.csv"))
    parser.add_argument("--min-confidence", type=float, default=0.5)
    parser.add_argument("--keep-macros", action="store_true", help="Also inject function-like macro names")
    parser.add_argument("--checkers", type=parse_checkers, default=list(DEFAULT_CHECKERS))
    parser.add_argument("--timeout", type=float, help="Per-Saber-run timeout in seconds")
    parser.add_argument("--bc-limit", type=int, help="Run Saber on only the first N selected .bc files per project")
    parser.add_argument(
        "--bc-scope",
        choices=("objects", "project", "all"),
        default="objects",
        help="objects: per-source bitcode only; project: project.bc only; all: every .bc under workspace/<project>-bc",
    )
    parser.add_argument("--jobs", default=None, help="make -j value for rebuilding Saber; defaults to nproc")
    parser.add_argument("--rebuild-target", default="saber", help="SVF make target to rebuild before each project")
    parser.add_argument("--save-stdout", action="store_true")
    parser.add_argument("--quiet", action="store_true", help="Do not print per-Saber-run progress")
    parser.add_argument("--stop-on-saber-error", action="store_true")
    parser.add_argument("--stop-on-project-error", action="store_true")
    args = parser.parse_args(argv)

    if not 0.0 <= args.min_confidence <= 1.0:
        parser.error("--min-confidence must be between 0 and 1")
    if args.bc_limit is not None and args.bc_limit < 1:
        parser.error("--bc-limit must be >= 1")
    return args


def resolve_under(root: Path, path: Path) -> Path:
    return path if path.is_absolute() else root / path


def selected_bc_files(project: str, workspace_root: Path, scope: str, limit: int | None) -> tuple[Path, list[Path]]:
    project_bc_dir = workspace_root / f"{project}-bc"
    if scope == "project":
        bc_input = project_bc_dir / "project.bc"
        files = [bc_input] if bc_input.is_file() else []
    elif scope == "objects":
        bc_input = project_bc_dir / "objects"
        files = collect_bc_files(bc_input, limit)
    else:
        bc_input = project_bc_dir
        files = collect_bc_files(bc_input, limit)

    if scope == "project" and limit is not None:
        files = files[:limit]
    return bc_input, files


def rebuild_saber(svf_build_dir: Path, jobs: str | None, target: str) -> float:
    command = ["make", "--no-print-directory", "-C", str(svf_build_dir)]
    if jobs:
        command.append(f"-j{jobs}")
    else:
        command.append("-j")
    if target:
        command.append(target)

    start = time.perf_counter()
    subprocess.run(command, check=True)
    return time.perf_counter() - start


def write_summary(results: Sequence[ProjectTiming], json_path: Path, csv_path: Path) -> None:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    csv_path.parent.mkdir(parents=True, exist_ok=True)

    payload = {
        "metadata": {
            "project_count": len(results),
            "ok_count": sum(1 for result in results if result.status == "ok"),
            "failed_count": sum(1 for result in results if result.status != "ok"),
            "total_saber_elapsed_seconds": round(sum(result.saber_elapsed_seconds for result in results), 3),
            "total_saber_elapsed": format_elapsed_seconds(sum(result.saber_elapsed_seconds for result in results)),
        },
        "projects": [result.to_dict() for result in results],
    }
    json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    fieldnames = list(results[0].to_dict().keys()) if results else list(ProjectTiming("", "", "", "", "").to_dict())
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for result in results:
            writer.writerow(result.to_dict())


def run_project(project: str, args: argparse.Namespace) -> ProjectTiming:
    root = args.nspa_root.resolve()
    outputs_root = resolve_under(root, args.outputs_root)
    workspace_root = resolve_under(root, args.workspace_root)
    saber_output_root = resolve_under(root, args.saber_output_root)
    svf_build_dir = resolve_under(root, args.svf_build_dir)
    saber_api_cpp = resolve_under(root, args.saber_api_cpp)

    validated_json = outputs_root / project / f"nspa_{project}_validated_memory_functions.json"
    output_dir = saber_output_root / project
    bc_input, bc_files = selected_bc_files(project, workspace_root, args.bc_scope, args.bc_limit)

    timing = ProjectTiming(
        project=project,
        status="failed",
        validated_json=str(validated_json),
        bc_input=str(bc_input),
        output_dir=str(output_dir),
    )

    if not validated_json.is_file():
        raise FileNotFoundError(f"validated memory function JSON not found: {validated_json}")
    if not bc_files:
        raise FileNotFoundError(f"no selected bitcode files for {project}: {bc_input}")

    functions = load_validated_memory_functions(
        validated_json,
        min_confidence=args.min_confidence,
        skip_macros=not args.keep_macros,
    )
    alloc_count, free_count = patch_saber_checker_api(
        saber_api_cpp,
        functions,
        project_tag=project,
        create_backup=True,
    )
    timing.loaded_memory_functions = len(functions)
    timing.injected_allocators = alloc_count
    timing.injected_releasers = free_count

    timing.rebuild_elapsed_seconds = rebuild_saber(svf_build_dir, args.jobs, args.rebuild_target)

    saber = find_saber_binary(svf_build_dir)
    extapi = find_extapi_bc(svf_build_dir)
    saber_start = time.perf_counter()
    results = run_saber_on_bitcode(
        saber=saber,
        extapi=extapi,
        bc_files=bc_files,
        checkers=args.checkers,
        output_dir=output_dir,
        timeout=args.timeout,
        continue_on_error=not args.stop_on_saber_error,
        save_stdout=args.save_stdout,
        progress=not args.quiet,
    )
    timing.saber_elapsed_seconds = time.perf_counter() - saber_start
    timing.saber_run_count = len(results)
    timing.saber_failed_count = sum(1 for result in results if result.returncode != 0)
    timing.status = "ok"

    write_saber_manifest(
        results,
        output_dir,
        saber_elapsed_seconds=timing.saber_elapsed_seconds,
    )
    return timing


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    all_results: list[ProjectTiming] = []

    for index, project in enumerate(args.projects, start=1):
        print(f"[+] ({index}/{len(args.projects)}) Project: {project}", file=sys.stderr, flush=True)
        try:
            result = run_project(project, args)
            print(
                f"[+] {project}: Saber step-4 time "
                f"{format_elapsed_seconds(result.saber_elapsed_seconds)} "
                f"({result.saber_run_count} runs, {result.saber_failed_count} failed)",
                file=sys.stderr,
                flush=True,
            )
        except Exception as exc:  # noqa: BLE001 - keep batch timing resilient.
            result = ProjectTiming(
                project=project,
                status="failed",
                validated_json=str(resolve_under(args.nspa_root.resolve(), args.outputs_root) / project / f"nspa_{project}_validated_memory_functions.json"),
                bc_input=str(resolve_under(args.nspa_root.resolve(), args.workspace_root) / f"{project}-bc"),
                output_dir=str(resolve_under(args.nspa_root.resolve(), args.saber_output_root) / project),
                error=str(exc),
            )
            print(f"[-] {project}: {exc}", file=sys.stderr, flush=True)
            if args.stop_on_project_error:
                all_results.append(result)
                break
        all_results.append(result)
        write_summary(
            all_results,
            resolve_under(args.nspa_root.resolve(), args.summary_json),
            resolve_under(args.nspa_root.resolve(), args.summary_csv),
        )

    summary_json = resolve_under(args.nspa_root.resolve(), args.summary_json)
    summary_csv = resolve_under(args.nspa_root.resolve(), args.summary_csv)
    print(f"[+] Timing summary JSON: {summary_json}", file=sys.stderr, flush=True)
    print(f"[+] Timing summary CSV : {summary_csv}", file=sys.stderr, flush=True)
    return 0 if all(result.status == "ok" for result in all_results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
