#!/usr/bin/env python3
"""Check tracked, first-party C with Lizard's parser and explicit file limits."""
import json
from pathlib import Path
import subprocess
import sys

import lizard

ROOT = Path(__file__).resolve().parent.parent


def check_file(path: Path, limits: dict[str, int]) -> list[str]:
    text = path.read_text(encoding="utf-8")
    issues = []
    lines = len(text.splitlines())
    if lines > limits["max_file_lines"]:
        issues.append(f"{path}: {lines} lines exceeds file limit {limits['max_file_lines']}")
    for function in lizard.analyze_file.analyze_source_code(str(path), text).function_list:
        for field, limit in [("nloc", "max_function_nloc"),
                             ("cyclomatic_complexity", "max_cyclomatic_complexity")]:
            value = getattr(function, field)
            if value > limits[limit]:
                issues.append(f"{path}:{function.start_line}: {function.name}: "
                              f"{field}={value} exceeds {limits[limit]}")
    return issues


def main() -> int:
    limits = json.loads((ROOT / ".c-size-limits.json").read_text())
    if any(type(value) is not int or value <= 0 for value in limits.values()):
        raise ValueError("C size limits must be positive integers")
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "--", "core", "include", "ports", "tests"], cwd=ROOT
    ).decode().split("\0")
    paths = [ROOT / name for name in tracked if name.endswith((".c", ".h"))
             and not name.startswith("ports/esp32/managed_components/")]
    if not paths:
        print("No tracked C sources found", file=sys.stderr)
        return 1
    issues = [issue for path in paths for issue in check_file(path, limits)]
    for issue in issues:
        print(issue, file=sys.stderr)
    if issues:
        return 1
    print(f"C size checks passed ({len(paths)} files): "
          f"function NLOC <= {limits['max_function_nloc']}, "
          f"file lines <= {limits['max_file_lines']}, "
          f"complexity <= {limits['max_cyclomatic_complexity']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
