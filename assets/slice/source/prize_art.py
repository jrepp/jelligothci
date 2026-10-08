"""Nine original, bold 32px collectible silhouettes; binary alpha, shared palette."""
from PIL import Image, ImageDraw

NAMES = ('butterfly', 'pearl-tooth', 'breakfast-sun', 'tea-sprite', 'movie-star',
         'bubble-gem', 'moon-charm', 'rainbow-seed', 'friendship-bow')
LABELS = ('Butterfly', 'Pearl Tooth', 'Breakfast Sun', 'Tea Sprite', 'Movie Star',
          'Bubble Gem', 'Moon Charm', 'Rainbow Seed', 'Friendship Bow')


def create_prize_assets(root, palette):
    c = [tuple(bytes.fromhex(v[1:])) for v in palette]
    ink, shadow, teal, mint, lilac, cream, white, pink, gold = [c[i] for i in (0, 1, 3, 5, 8, 10, 11, 13, 14)]
    (root / 'prizes').mkdir(parents=True, exist_ok=True)
    records = []
    for index, name in enumerate(NAMES):
        im = Image.new('RGBA', (32, 32)); d = ImageDraw.Draw(im)
        if name == 'butterfly':
            for box, inner in (((1,4,15,20),(3,6,13,18)), ((16,4,30,20),(18,6,28,18))):
                d.ellipse(box, fill=ink); d.ellipse(inner, fill=lilac)
            for box, inner in (((4,17,15,28),(6,19,13,26)), ((16,17,27,28),(18,19,25,26))):
                d.ellipse(box, fill=ink); d.ellipse(inner, fill=mint)
            d.line([(15,10),(12,2),(10,2)], fill=ink, width=2)
            d.line([(16,10),(19,2),(21,2)], fill=ink, width=2)
            d.rounded_rectangle((13,9,18,26), radius=2, fill=ink)
            d.rounded_rectangle((15,11,16,24), radius=1, fill=gold)
            d.ellipse((5,8,8,11), fill=cream); d.ellipse((23,8,26,11), fill=cream)
        elif name == 'pearl-tooth':
            d.rounded_rectangle((4,3,27,24), radius=8, fill=ink)
            d.polygon([(5,17),(12,17),(16,22),(20,17),(26,17),(24,29),(19,29),(16,24),(13,29),(8,29)], fill=ink)
            d.rounded_rectangle((6,5,25,22), radius=7, fill=cream)
            d.polygon([(7,17),(13,18),(16,22),(19,18),(24,17),(22,27),(20,27),(16,22),(12,27),(10,27)], fill=cream)
            d.line([(9,10),(10,8),(13,7)], fill=white, width=2)
            d.rectangle((10,14,11,16), fill=ink); d.rectangle((20,14,21,16), fill=ink)
            d.arc((13,15,18,19),0,180,fill=ink,width=1)
        elif name == 'breakfast-sun':
            for box in ((13,1,18,6),(13,25,18,30),(1,13,6,18),(25,13,30,18)):
                d.rounded_rectangle(box, radius=2, fill=ink)
            for box in ((14,2,17,5),(14,26,17,29),(2,14,5,17),(26,14,29,17)):
                d.rectangle(box,fill=gold)
            d.ellipse((5,5,26,26), fill=ink); d.ellipse((7,7,24,24),fill=gold)
            d.arc((9,9,21,21),190,270,fill=cream,width=2)
            d.rectangle((11,14,12,16),fill=ink); d.rectangle((19,14,20,16),fill=ink)
            d.arc((13,16,18,20),0,180,fill=ink,width=2)
        elif name == 'tea-sprite':
            d.rounded_rectangle((20,15,30,25),radius=4,fill=ink)
            d.rounded_rectangle((23,18,27,22),radius=2,fill=cream)
            d.rounded_rectangle((3,13,24,29),radius=5,fill=ink)
            d.rounded_rectangle((5,15,22,27),radius=4,fill=lilac)
            d.ellipse((4,11,23,17),fill=ink);d.ellipse((6,13,21,15),fill=gold)
            d.rounded_rectangle((9,2,20,13),radius=5,fill=ink)
            d.rounded_rectangle((11,4,18,12),radius=4,fill=mint)
            d.polygon([(11,9),(11,15),(14,12),(17,15),(18,9)],fill=mint)
            d.point((13,7),fill=ink); d.point((16,7),fill=ink)
            d.line((7,18,7,22),fill=cream,width=2)
        elif name == 'movie-star':
            outer=[(15,1),(20,10),(30,12),(23,20),(25,30),(15,25),(5,30),(7,20),(1,12),(11,10)]
            inner=[(15,5),(19,12),(26,14),(21,19),(22,26),(15,22),(8,26),(9,19),(5,14),(12,12)]
            d.polygon(outer,fill=ink);d.polygon(inner,fill=gold)
            d.line([(14,8),(12,13),(8,14)],fill=cream,width=2)
            d.rectangle((11,16,12,18),fill=ink);d.rectangle((18,16,19,18),fill=ink)
            d.arc((13,18,17,21),0,180,fill=ink)
        elif name == 'bubble-gem':
            d.ellipse((2,2,29,29),fill=ink);d.ellipse((4,4,27,27),fill=mint)
            d.ellipse((7,7,24,24),fill=teal)
            d.polygon([(15,8),(23,15),(15,25),(8,15)],fill=ink)
            d.polygon([(15,11),(20,15),(15,21),(11,15)],fill=lilac)
            d.polygon([(15,11),(15,20),(11,15)],fill=cream)
            d.arc((6,6,25,25),185,260,fill=white,width=2)
        elif name == 'moon-charm':
            d.ellipse((12,1,20,9),fill=ink);d.ellipse((14,3,18,7),fill=gold)
            d.ellipse((3,6,28,30),fill=ink);d.ellipse((5,8,26,28),fill=lilac)
            d.ellipse((13,4,30,21),fill=ink);d.ellipse((15,3,31,19),fill=(0,0,0,0))
            d.arc((7,10,24,26),90,185,fill=cream,width=2)
        elif name == 'rainbow-seed':
            for box, color, width in (((2,2,29,27),ink,7),((4,4,27,25),pink,2),((6,6,25,25),gold,2),((8,8,23,25),mint,2)):
                d.arc(box,180,360,fill=color,width=width)
            d.polygon([(16,12),(24,19),(23,27),(18,30),(10,29),(7,22),(10,16)],fill=ink)
            d.polygon([(16,15),(21,20),(20,26),(17,28),(12,27),(10,22),(12,18)],fill=gold)
            d.line([(15,19),(13,23),(14,26)],fill=cream,width=2)
            d.ellipse((17,10,27,16),fill=ink);d.ellipse((19,11,25,14),fill=mint)
        else:
            d.polygon([(2,6),(7,4),(15,10),(24,4),(29,6),(29,20),(23,22),(16,17),(8,22),(2,20)],fill=ink)
            d.polygon([(4,8),(7,7),(13,12),(13,15),(7,19),(4,18)],fill=pink)
            d.polygon([(19,12),(24,7),(27,8),(27,18),(23,19),(19,15)],fill=pink)
            d.polygon([(11,17),(16,18),(12,29),(8,26)],fill=ink)
            d.polygon([(17,18),(21,17),(25,26),(21,29)],fill=ink)
            d.polygon([(12,19),(14,19),(11,26),(10,25)],fill=pink)
            d.polygon([(19,19),(20,19),(23,25),(22,26)],fill=pink)
            d.rounded_rectangle((12,9,20,19),radius=3,fill=ink)
            d.rounded_rectangle((14,11,18,17),radius=2,fill=gold)
            d.line((5,9,5,12),fill=cream,width=2);d.line((25,9,25,12),fill=cream,width=2)
        path = f'prizes/{name}.png';im.save(root / path)
        records.append(dict(id=11001+index,key=f'prizes.{name}',path=path,kind='prizes',width=32,height=32,
                            pivot=[16,16],bounds=list(im.getchannel('A').getbbox()),purpose=LABELS[index]+' collectible; shared catch target, collection slot, and gift latch'))
    return records
