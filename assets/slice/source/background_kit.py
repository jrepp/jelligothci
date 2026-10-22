# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Native pixel geometry for the reusable earth-tone kit. Writes candidates only."""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageDraw

# Shared value ramp for the original quiet, vignetted location recipes.
EARTH_RAMP = [
    "#000000", "#10110e", "#20221b", "#303229",
    "#404338", "#505447", "#606455", "#707464",
    "#808473", "#909383", "#a0a293", "#b0b1a3",
    "#bfbfb2", "#cecdc1", "#dddbd0", "#ece9df",
]
KIT_PALETTE = [
    "#30312b", "#474a3f", "#626955", "#7f866c",
    "#a1a68a", "#c0c1a5", "#5c5047", "#796452",
    "#987f66", "#b39c7e", "#d0bda0", "#ded2b9",
    "#666f70", "#87918b", "#a9b2a5", "#c9cbb8",
]
NAMES = ("sky", "hills", "town", "shop", "trees", "paving",
         "street-fronts", "sidewalk", "street-edge")


def image(name):
    im = Image.new("RGBA", (64, 64))
    d = ImageDraw.Draw(im)
    c = KIT_PALETTE
    if name == "sky":
        d.rectangle((0, 0, 63, 63), fill=c[14])
        d.rectangle((0, 24, 63, 63), fill=c[15])
        for x, y in ((3, 14), (39, 8)):
            d.rectangle((x, y, x+16, y+2), fill=c[15])
            d.rectangle((x+4, y-2, x+11, y), fill=c[15])
    elif name == "hills":
        d.polygon([(0,29),(8,25),(15,26),(25,19),(35,20),(45,27),(55,25),(63,29),(63,63),(0,63)],fill=c[4])
        d.polygon([(0,38),(12,31),(22,33),(33,28),(42,31),(55,32),(63,38),(63,63),(0,63)],fill=c[3])
    elif name == "town":
        for x, w, top in ((0,12,25),(16,17,19),(38,12,24),(54,10,28)):
            d.rectangle((x,top,x+w-1,55),fill=c[13])
            d.rectangle((x-1,top-1,x+w,top),fill=c[12])
            for wx in range(x+3,x+w-2,5):
                for y in range(top+5,47,8):
                    d.rectangle((wx,y,wx+1,y+2),fill=c[14])
    elif name == "shop":
        d.rectangle((5,23,40,54),fill=c[9])
        d.rectangle((4,21,41,23),fill=c[7])
        d.rectangle((8,26,37,29),fill=c[10])
        for x in (9,20,31):
            d.rectangle((x,32,x+5,37),fill=c[12])
            d.line((x,32,x+5,32),fill=c[11])
        d.rectangle((9,44,25,52),fill=c[12])
        d.rectangle((29,43,36,54),fill=c[7])
        d.rectangle((30,44,35,49),fill=c[13])
        d.rectangle((7,40,38,42),fill=c[3])
        for x in range(8,38,6):
            d.rectangle((x,40,x+2,42),fill=c[5])
        d.rectangle((4,55,41,56),fill=c[8])
    elif name == "trees":
        for x, y in ((12,34),(51,38)):
            d.rectangle((x-1,y,x+1,56),fill=c[7])
            d.polygon([(x-7,y+5),(x-9,y-2),(x-5,y-8),(x+3,y-10),(x+8,y-5),(x+8,y+3),(x+3,y+7)],fill=c[2])
            d.rectangle((x-5,y-6,x+3,y-3),fill=c[3])
            d.rectangle((x-6,y-1,x-2,y+2),fill=c[3])
        d.rectangle((0,57,63,63),fill=c[2])
        for x in range(0,64,8):
            d.rectangle((x,55,x+3,57),fill=c[3])
    elif name == "paving":
        d.rectangle((0,56,63,63),fill=c[8])
        d.line((0,56,63,56),fill=c[10])
        d.line((0,58,63,58),fill=c[7])
        for x in range(0,64,16):
            d.line((x,59,x,63),fill=c[7])
        d.line((0,63,63,63),fill=c[7])
    elif name == "street-fronts":
        # A continuous shop row ends behind the home actor's source-space feet.
        for left, right, top, wall in ((0,20,12,c[9]), (22,43,16,c[8]), (45,63,10,c[13])):
            d.rectangle((left,top,right,30),fill=wall)
            d.rectangle((left,top,right,top+1),fill=c[7])
            d.line((left,30,right,30),fill=c[6])
            for x in range(left+3,right-2,7):
                d.rectangle((x,top+4,x+3,top+7),fill=c[12])
                d.line((x,top+8,x+3,top+8),fill=c[10])
        # Shop windows, recessed doors and alternating canvas awnings.
        for x in (2,24,47):
            d.rectangle((x,23,x+10,28),fill=c[12])
            d.line((x+5,23,x+5,28),fill=c[9])
            d.rectangle((x+13,23,x+16,30),fill=c[6])
            d.point((x+15,27),fill=c[10])
            d.rectangle((x-1,21,x+11,22),fill=c[2])
            for stripe in range(x, x+11, 4):
                d.line((stripe,21,stripe,22),fill=c[4])
    elif name == "sidewalk":
        # Back wall at y=30; pet contact is about y=35. A broad walking lane
        # stays clear before the curb, with road beyond it in the foreground.
        d.rectangle((0,31,63,40),fill=c[10])
        d.line((0,31,63,31),fill=c[8])
        d.line((0,36,63,36),fill=c[9])
        for x in range(0,64,16):
            d.line((x,32,x,35),fill=c[9])
            d.line((x+8,37,x+8,40),fill=c[9])
        d.rectangle((0,41,63,43),fill=c[8])
        d.line((0,41,63,41),fill=c[11])
        d.line((0,43,63,43),fill=c[6])
        d.rectangle((0,44,63,63),fill=c[12])
        d.line((0,44,63,44),fill=c[6])
        for x in range(0,64,16):
            d.point((x,42),fill=c[6])
        d.line((0,57,10,57),fill=c[9])
        d.line((32,57,42,57),fill=c[9])
    elif name == "street-edge":
        # Close details below the actor's feet: no actor occlusion is needed.
        d.rectangle((3,40,12,42),fill=c[6])
        d.rectangle((4,39,11,41),fill=c[7])
        d.rectangle((3,38,12,39),fill=c[8])
        d.rectangle((4,36,11,37),fill=c[2])
        d.rectangle((6,35,9,36),fill=c[3])
        d.point((5,36),fill=c[4])
        # A bench, a short bollard and a drain mark the near curb.
        d.rectangle((45,38,58,39),fill=c[7])
        d.line((45,36,58,36),fill=c[8])
        for x in (47,56):
            d.line((x,37,x,42),fill=c[6])
        d.rectangle((23,37,24,41),fill=c[6])
        d.line((23,37,24,37),fill=c[9])
        d.rectangle((31,44,38,45),fill=c[6])
        for x in (32,34,36):
            d.point((x,44),fill=c[12])
    return im


def build(root):
    root = Path(root)
    (root / "backgrounds").mkdir(parents=True, exist_ok=True)
    records = []
    for i, name in enumerate(NAMES):
        im = image(name)
        path = f"backgrounds/kit-{name}.png"
        im.save(root / path)
        records.append({"id":10007+i, "key":f"backgrounds.kit-{name}", "path":path,
                        "kind":"backgrounds", "width":64, "height":64, "pivot":[32,32],
                        "bounds":list(im.getchannel("A").getbbox()), "palette":"earth-kit",
                        "purpose":f"Reusable muted {name} layer; source geometry in background_kit.py"})
    return records


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/assets/background-kit"))
    out = parser.parse_args().output.resolve()
    source = Path(__file__).resolve().parents[1]
    if out == source or source in out.parents:
        raise SystemExit("Generate candidates outside the source artwork tree")
    records = build(out)
    (out / "kit.json").write_text(json.dumps(records, indent=2)+"\n")
