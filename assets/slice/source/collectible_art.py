# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Rebuild the cartridge and record silhouettes using existing prize motifs."""
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]


def build():
    manifest_path = ROOT / "assets.json"
    manifest = json.loads(manifest_path.read_text())
    recipes = (
        (11011, "jelli-chain-cartridge", "movie-star", "Jelli Chain cartridge"),
        (11012, "morning-tea-record", "tea-sprite", "Morning Tea record"),
        (11013, "garden-waltz-record", "rainbow-seed", "Garden Waltz record"),
        (11014, "porch-picnic-record", "breakfast-sun", "Porch Picnic record"),
        (11015, "tiny-thunder-record", "bubble-gem", "Tiny Thunder record"),
        (11016, "neon-heart-record", "friendship-bow", "Neon Heart record"),
        (11017, "cloud-club-record", "butterfly", "Cloud Club record"),
        (11018, "pixel-sunset-record", "moon-charm", "Pixel Sunset record"),
        (11019, "bubble-lab-cartridge", "bubble-gem", "Bubble Lab cartridge"),
    )
    for asset_id, key, motif, title in recipes:
        image = Image.new("RGBA", (32, 32))
        draw = ImageDraw.Draw(image)
        if key.endswith("-cartridge"):
            draw.rectangle((4, 3, 27, 28), fill="#30312b")
            draw.rectangle((6, 4, 25, 25), fill="#87918b")
            draw.rectangle((8, 7, 23, 20), fill="#ded2b9")
            draw.rectangle((10, 23, 21, 28), fill="#5c5047")
            for x in range(11, 22, 3):
                draw.line((x, 25, x, 28), fill="#d0bda0")
        else:
            draw.ellipse((2, 2, 29, 29), fill="#30312b")
            draw.ellipse((4, 4, 27, 27), outline="#626955")
            draw.ellipse((7, 7, 24, 24), outline="#87918b")
            draw.ellipse((9, 9, 22, 22), fill="#ded2b9")
        with Image.open(ROOT / "prizes" / f"{motif}.png") as source:
            small = source.convert("RGBA").resize((12, 12), Image.Resampling.NEAREST)
            image.alpha_composite(small, (10, 8 if key.endswith("-cartridge") else 10))
        if not key.endswith("-cartridge"):
            draw.ellipse((15, 15, 16, 16), fill="#30312b")
        path = f"prizes/{key}.png"
        image.save(ROOT / path)
        entry = dict(id=asset_id, key=f"prizes.{key}", path=path, kind="prizes",
                     width=32, height=32, pivot=[16, 16],
                     bounds=list(image.getbbox()),
                     palette=sorted({f"#{r:02x}{g:02x}{b:02x}" for r, g, b, a in image.getdata() if a}),
                     purpose=f"{title}; permanent playable collectible")
        manifest["assets"] = [a for a in manifest["assets"] if a["id"] != asset_id]
        manifest["assets"].append(entry)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    build()
