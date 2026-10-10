#!/usr/bin/env python3
"""Reject mutable external action refs, unpinned container images and moving hosted OS aliases."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
errors = []
images = set()
for path in sorted((root / ".github/workflows").glob("*.y*ml")):
    for number, line in enumerate(path.read_text().splitlines(), 1):
        match = re.search(r"\buses:\s*([^\s#]+)", line)
        if match:
            action = match.group(1).strip("\"'")
            if not action.startswith("./") and not re.fullmatch(r"[^@]+@[0-9a-f]{40}", action):
                errors.append(f"{path.name}:{number}: external action must use a full commit SHA")
        match = re.search(r"^\s*image:\s*([^\s#]+)", line)
        if match:
            image = match.group(1).strip("\"'")
            images.add(image)
            if not re.fullmatch(r"[^@]+@sha256:[0-9a-f]{64}", image):
                errors.append(f"{path.name}:{number}: container image must be pinned by sha256 digest")
        if re.search(r"\b(?:ubuntu|macos|windows)-latest\b", line):
            errors.append(f"{path.name}:{number}: select an explicit hosted OS version")
# The Jelli Art UI tests run locally in the same image CI uses for its screenshot baselines.
run_sh = root / "tools/jelli-art/ui_tests/run.sh"
if run_sh.exists():
    match = re.search(r'^image="([^"]+)"', run_sh.read_text(), re.M)
    if not match or match.group(1) not in images:
        errors.append(f"{run_sh.relative_to(root)}: image must match a workflow container image")
if errors:
    sys.exit("\n".join(errors))
print("Workflow action, image and OS pins valid")
