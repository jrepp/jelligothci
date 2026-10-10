"""Named asset IDs from assets/slice/assets.json, as a C header (memo-032).

Reads only the manifest (no Pillow), so the build, the embedder and the C linters can
all generate it. Usage: python3 tools/assets/asset_ids.py OUTPUT.h
"""
import json
import sys
from pathlib import Path

MANIFEST = Path(__file__).resolve().parents[2] / "assets/slice/assets.json"
# Families the renderer indexes by position (prize index, effect style, need meter).
INDEXED_FAMILIES = {"prizes": "PRIZE", "effects": "EFFECT", "meters": "METER"}


def asset_macro(key):
    return "JELLI_ASSET_" + key.upper().replace(".", "_").replace("-", "_")


def header(manifest):
    """Named asset IDs, so renderer code never writes a bare ID (memo-032)."""
    lines = ["/* Generated from assets/slice/assets.json by tools/assets/embed_slice.py. */",
             "#ifndef JELLI_ASSET_IDS_H", "#define JELLI_ASSET_IDS_H", ""]
    seen = set()
    for asset in sorted(manifest["assets"], key=lambda a: a["id"]):
        macro = asset_macro(asset["key"])
        if macro in seen:
            raise ValueError(f"Two asset keys map to {macro}")
        seen.add(macro)
        lines.append(f"#define {macro} {asset['id']}u")
    lines.append("")
    for kind, name in INDEXED_FAMILIES.items():
        ids = sorted(a["id"] for a in manifest["assets"] if a["kind"] == kind)
        if ids != list(range(ids[0], ids[0] + len(ids))):
            raise ValueError(f"{kind} IDs must be contiguous: the renderer indexes them")
        lines.append(f"/* {kind}: index i is JELLI_ASSET_{name}_BASE + 1 + i, for i < {len(ids)}. */")
        lines.append(f"#define JELLI_ASSET_{name}_BASE {ids[0] - 1}u")
        lines.append(f"#define JELLI_ASSET_{name}_COUNT {len(ids)}u")
    lines += ["", "#endif", ""]
    return "\n".join(lines)




def main():
    out = Path(sys.argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(header(json.loads(MANIFEST.read_text())))


if __name__ == "__main__":
    main()
