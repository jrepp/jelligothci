"""Original 32px ring-menu artwork; same palette as the existing pixel icons."""
from PIL import Image, ImageDraw
import math


def heart(d, ink, pink, shine):
    """Puffy, stepped lobes with a soft tip and a little reflected light."""
    d.ellipse((2, 4, 17, 20), fill=ink)
    d.ellipse((14, 4, 29, 20), fill=ink)
    d.polygon([(3,14),(28,14),(26,21),(20,27),(17,29),(14,29),(8,24),(5,20)], fill=ink)
    d.ellipse((4, 6, 15, 19), fill=pink)
    d.ellipse((16, 6, 27, 19), fill=pink)
    d.polygon([(5,14),(26,14),(24,20),(18,26),(15,27),(9,22)], fill=pink)
    d.line([(7,12),(8,9),(11,8)], fill=shine, width=2)


def create_menu_assets(root, palette):
    colors = [tuple(bytes.fromhex(c[1:])) for c in palette]
    ink, shadow, teal, mint, light, lilac, cream, white, coral, gold = [colors[i] for i in (0, 1, 3, 5, 6, 8, 10, 11, 13, 14)]
    (root / "menus").mkdir(parents=True, exist_ok=True)
    records = []
    for index, name in enumerate(("food", "care", "rest", "collection", "more", "settings", "moments", "gifts", "breakfast", "tea", "outing", "movie", "close")):
        image = Image.new("RGBA", (32, 32))
        d = ImageDraw.Draw(image)
        if name == "food":
            d.polygon([(3,16),(28,16),(25,26),(22,29),(9,29),(6,26)], fill=ink)
            d.polygon([(5,18),(26,18),(23,25),(9,25)], fill=teal)
            d.line([(7,19),(24,19)], fill=mint, width=2)
            for x,y in ((7,9),(16,6),(23,12)):
                d.rectangle((x-3,y-2,x+3,y+3), fill=ink)
                d.rectangle((x-2,y-1,x+2,y+2), fill=coral)
                d.line([(x,y-3),(x+2,y-5)], fill=mint, width=2)
                d.point((x-1,y-1), fill=cream)
        elif name == "care":
            heart(d, ink, coral, white)
            d.rounded_rectangle((14,12,17,22), radius=1, fill=cream)
            d.rounded_rectangle((10,15,21,18), radius=1, fill=cream)
        elif name == "rest":
            # Two offset circles give a legible crescent with a clean transparent bite.
            d.ellipse((3, 3, 28, 28), fill=ink)
            d.ellipse((5, 5, 26, 26), fill=cream)
            d.ellipse((12, 0, 32, 22), fill=ink)
            d.ellipse((14, 0, 34, 20), fill=(0, 0, 0, 0))
            # Remove the outside of the original disk, leaving only its crescent.
            for y in range(32):
                for x in range(32):
                    if (x - 15.5) ** 2 + (y - 15.5) ** 2 > 13 ** 2:
                        image.putpixel((x, y), (0, 0, 0, 0))
        elif name == "collection":
            for x,y,c in ((17,6,lilac),(5,13,mint)):
                d.polygon([(x+3,y),(x+9,y),(x+12,y+5),(x+13,y+12),(x+10,y+15),(x+7,y+13),(x+4,y+15),(x,y+12),(x,y+5)], fill=ink)
                d.polygon([(x+4,y+2),(x+8,y+2),(x+10,y+6),(x+11,y+11),(x+8,y+12),(x+5,y+11),(x+2,y+12),(x+2,y+6)], fill=c)
                d.rectangle((x+4,y+6,x+5,y+8), fill=ink)
                d.rectangle((x+8,y+6,x+9,y+8), fill=ink)
                d.point((x+4,y+3), fill=cream)
        elif name == "more":
            d.rounded_rectangle((2,7,29,24), radius=5, fill=ink)
            d.rounded_rectangle((4,9,27,22), radius=4, fill=shadow)
            for x,c in ((8,mint),(16,gold),(24,lilac)):
                d.ellipse((x-2,13,x+2,18), fill=c)
                d.point((x-1,13), fill=cream)
        elif name == "settings":
            points=[]
            for tooth in range(8):
                for fraction,radius in ((-.42,11),(-.24,14),(.24,14),(.42,11)):
                    angle=(tooth+fraction)*math.pi/4
                    points.append((round(15.5+radius*math.cos(angle)),round(15.5+radius*math.sin(angle))))
            d.polygon(points,fill=ink)
            d.ellipse((6,6,25,25),fill=lilac)
            d.ellipse((10,10,21,21),fill=ink)
            d.ellipse((13,13,18,18),fill=cream)
        elif name in ("tea", "moments"):
            d.rounded_rectangle((6,13,22,26), radius=5, fill=ink)
            d.rounded_rectangle((8,15,20,24), radius=4, fill=mint)
            d.point((11,19), fill=ink)
            d.point((17,19), fill=ink)
            d.arc((12,19,16,22), 0, 180, fill=ink)
            d.rectangle((22,15,28,21), fill=ink)
            d.rectangle((23,17,25,19), fill=cream)
            d.line([(4,28),(25,28)], fill=cream, width=2)
            for x in (11,17):
                d.line([(x,10),(x-2,7),(x,4)], fill=cream, width=2)
        elif name == "gifts":
            d.rounded_rectangle((4,12,27,28), radius=3, fill=ink)
            d.rounded_rectangle((6,14,25,26), radius=2, fill=mint)
            d.rectangle((2,9,29,14), fill=ink)
            d.rectangle((4,10,27,12), fill=teal)
            d.rectangle((14,9,17,26), fill=coral)
            d.rounded_rectangle((5,2,14,10), radius=3, fill=ink)
            d.rounded_rectangle((17,2,26,10), radius=3, fill=ink)
            d.rounded_rectangle((7,4,12,8), radius=2, fill=coral)
            d.rounded_rectangle((19,4,24,8), radius=2, fill=coral)
            d.rectangle((9,5,11,6), fill=ink)
            d.rectangle((20,5,22,6), fill=ink)
            d.rounded_rectangle((13,6,18,11), radius=1, fill=coral)
        elif name == "breakfast":
            d.ellipse((3,9,29,29), fill=ink)
            d.ellipse((5,11,27,27), fill=cream)
            d.rounded_rectangle((5,3,16,17), radius=3, fill=ink)
            d.rounded_rectangle((7,5,14,15), radius=2, fill=gold)
            d.point((9,10), fill=ink)
            d.point((12,10), fill=ink)
            d.ellipse((13,15,25,25), fill=white)
            d.ellipse((17,17,23,23), fill=gold)
        elif name == "outing":
            d.rounded_rectangle((3,23,28,29), radius=3, fill=ink)
            d.rectangle((5,24,26,27), fill=teal)
            d.line((16,9,16,25), fill=ink, width=4)
            d.line((16,12,16,25), fill=mint, width=2)
            d.ellipse((4,13,14,21), fill=ink)
            d.ellipse((6,15,13,19), fill=mint)
            d.ellipse((18,16,28,23), fill=ink)
            d.ellipse((19,18,26,21), fill=mint)
            for x,y in ((12,3),(18,3),(9,8),(21,8),(15,12)):
                d.ellipse((x-3,y-2,x+3,y+4), fill=coral)
            d.ellipse((13,6,19,12), fill=gold)
        elif name == "movie":
            # A quiet, square screen and closed clapper; no diagonal fragments.
            d.rectangle((3,5,28,27), fill=ink)
            d.rectangle((5,7,26,11), fill=cream)
            for x in (9,19):
                d.rectangle((x,7,x+3,11), fill=ink)
            d.rectangle((5,14,26,25), fill=teal)
            d.polygon([(13,16),(20,20),(13,23)], fill=cream)
        elif name == "close":
            d.line([(7,7),(24,24)], fill=cream, width=6)
            d.line([(7,24),(24,7)], fill=cream, width=6)
        path = f"menus/{name}.png"
        image.save(root / path)
        records.append(dict(id=6001+index, key=f"menus.{name}", path=path, kind="menus",
                            width=32, height=32, pivot=[16,16], bounds=list(image.getchannel("A").getbbox()),
                            purpose="Ring menu category; drawn at 3x (96 physical pixels)"))
    return records


def create_meter_assets(root, palette):
    c = [tuple(bytes.fromhex(value[1:])) for value in palette]
    ink, cream, mint, gold, pink, lilac = [c[i] for i in (0,10,5,14,13,8)]
    (root / "meters").mkdir(parents=True, exist_ok=True)
    records = []
    for index, name in enumerate(("satiety", "energy", "hygiene", "amusement", "social")):
        image = Image.new("RGBA", (32,32)); d = ImageDraw.Draw(image)
        if name == "satiety":
            d.polygon([(5,12),(9,8),(23,8),(27,12),(25,22),(16,29),(7,22)], fill=ink)
            d.polygon([(7,13),(11,10),(21,10),(25,13),(23,21),(16,26),(9,21)], fill=pink)
            d.polygon([(9,5),(16,8),(21,3),(21,9),(16,12)], fill=mint)
            for x,y in ((11,15),(20,16),(15,21)):
                d.rectangle((x,y,x+1,y+1), fill=cream)
        elif name == "energy":
            d.polygon([(15,2),(26,2),(20,12),(28,12),(10,30),(13,19),(5,19)], fill=ink)
            d.polygon([(16,4),(23,4),(17,14),(23,14),(14,24),(16,17),(9,17)], fill=gold)
        elif name == "hygiene":
            d.rounded_rectangle((3,14,28,27), radius=4, fill=ink)
            d.rounded_rectangle((5,16,26,25), radius=3, fill=mint)
            d.line([(9,18),(21,18)], fill=cream, width=2)
            d.ellipse((6,6,13,13), fill=cream)
            d.ellipse((18,3,23,8), fill=cream)
            d.line([(26,9),(26,15)], fill=gold, width=2)
            d.line([(23,12),(29,12)], fill=gold, width=2)
        elif name == "amusement":
            d.ellipse((3,3,28,28), fill=ink)
            d.ellipse((5,5,26,26), fill=gold)
            d.polygon([(15,5),(22,11),(19,20),(10,20),(7,11)], fill=lilac)
            d.line([(6,23),(11,20),(16,27)], fill=cream, width=2)
        else:
            heart(d, ink, pink, cream)
            d.point((12,17), fill=ink)
            d.point((20,17), fill=ink)
            d.arc((14,17,18,21), 0, 180, fill=ink)
        path = f"meters/{name}.png"; image.save(root / path)
        records.append(dict(id=7001+index,key=f"meters.{name}",path=path,kind="meters",width=32,height=32,
                            pivot=[16,16],bounds=list(image.getchannel("A").getbbox()),
                            purpose="Large stat tile pictogram; vertical fill and 1–100 value rendered in game"))
    return records
