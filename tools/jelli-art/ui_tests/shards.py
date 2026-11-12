"""Deterministically distribute every Studio browser test file across runners."""
from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TESTS = Path(__file__).resolve().parent
COUNT = 3


def partition(directory: Path, count: int = COUNT) -> list[list[Path]]:
    if count < 1:
        raise ValueError("shard count must be positive")
    ignored = {"__pycache__", "build", "dist", "node_modules", "venv", "CVS"}
    files = sorted(
        (path for path in directory.rglob("*.py")
         if (path.name.startswith("test_") or path.name.endswith("_test.py"))
         and not any(part.startswith(".") or part in ignored for part in path.relative_to(directory).parts[:-1])),
        key=lambda path: (-path.stat().st_size, path.relative_to(directory).as_posix()),
    )
    if len(files) < count:
        raise ValueError("each shard needs at least one test file")
    shards: list[list[Path]] = [[] for _ in range(count)]
    weights = [0] * count
    for path in files:
        index = min(range(count), key=lambda i: (weights[i], i))
        shards[index].append(path)
        weights[index] += path.stat().st_size
    flattened = [path for shard in shards for path in shard]
    if len(flattened) != len(files) or len(set(flattened)) != len(files) or set(flattened) != set(files):
        raise ValueError("test files must occur in exactly one shard")
    return [sorted(shard) for shard in shards]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", type=int, help="zero-based shard index")
    args = parser.parse_args()
    if not 0 <= args.index < COUNT:
        parser.error(f"index must be between 0 and {COUNT - 1}")
    for path in partition(TESTS)[args.index]:
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
