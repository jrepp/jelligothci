#!/usr/bin/env python3
"""Reject mutable external action refs and moving hosted OS aliases."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
errors = []
for path in sorted((root / ".github/workflows").glob("*.y*ml")):
    for number, line in enumerate(path.read_text().splitlines(), 1):
        match = re.search(r"\buses:\s*([^\s#]+)", line)
        if match:
            action = match.group(1).strip("\"'")
            if not action.startswith("./") and not re.fullmatch(r"[^@]+@[0-9a-f]{40}", action):
                errors.append(f"{path.name}:{number}: external action must use a full commit SHA")
        if re.search(r"\b(?:ubuntu|macos|windows)-latest\b", line):
            errors.append(f"{path.name}:{number}: select an explicit hosted OS version")
if errors:
    sys.exit("\n".join(errors))
print("Workflow action and OS pins valid")
