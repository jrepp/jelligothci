"""Original health icons, tiny magical celebration sprites, and neutral scenery."""
from PIL import Image, ImageDraw
import math


def create_healthy_assets(root, palette):
    c = [tuple(bytes.fromhex(v[1:])) for v in palette]
    ink, teal, mint, lilac, cream, white, pink, gold = [c[i] for i in (0,3,5,8,10,11,13,14)]
    records = []
    for index, name in enumerate(('brush','medicine','shot','wash','stretch','floss','mouthwash','spit','cleanup')):
        im=Image.new('RGBA',(32,32));d=ImageDraw.Draw(im)
        if name=='brush':
            d.rounded_rectangle((3,5,22,27),radius=8,fill=ink)
            d.rounded_rectangle((5,7,20,25),radius=7,fill=white)
            d.polygon([(11,21),(14,21),(17,27),(8,27)],fill=ink)
            d.point((9,14),fill=ink);d.point((16,14),fill=ink);d.arc((10,14,15,18),0,180,fill=ink)
            d.rounded_rectangle((22,12,26,29),radius=2,fill=teal)
            d.rounded_rectangle((20,6,29,16),radius=2,fill=mint)
            for y in (8,11,14):d.line((22,y,29,y),fill=white)
        elif name=='medicine':
            d.rounded_rectangle((8,3,23,8),radius=2,fill=ink);d.rectangle((10,4,21,7),fill=lilac)
            d.rounded_rectangle((5,8,26,29),radius=5,fill=ink)
            d.rounded_rectangle((7,10,24,27),radius=4,fill=lilac)
            d.rounded_rectangle((9,14,22,24),radius=2,fill=cream)
            d.rectangle((14,16,17,22),fill=pink);d.rectangle((12,18,19,20),fill=pink)
            d.line((10,10,13,10),fill=white,width=2)
        elif name=='shot':
            d.rounded_rectangle((10,8,23,23),radius=3,fill=ink)
            d.rectangle((12,10,21,20),fill=mint)
            d.rectangle((15,3,18,9),fill=ink);d.rounded_rectangle((11,2,22,5),radius=1,fill=lilac)
            d.rectangle((7,8,26,10),fill=cream)
            d.line((16,23,16,29),fill=cream,width=2)
            for y in (12,16):d.line((18,y,21,y),fill=teal)
            d.line((4,17,4,23),fill=gold,width=2);d.line((1,20,7,20),fill=gold,width=2)
        elif name=='wash':
            d.rounded_rectangle((4,15,28,28),radius=6,fill=ink)
            d.rounded_rectangle((6,17,26,26),radius=5,fill=mint)
            for x,y,r in ((7,8,4),(19,4,3),(24,11,4)):
                d.ellipse((x-r,y-r,x+r,y+r),fill=cream);d.point((x-1,y-1),fill=white)
            d.arc((10,18,22,23),0,180,fill=teal,width=2)
        elif name=='stretch':
            d.ellipse((10,3,22,15),fill=ink);d.ellipse((12,5,20,13),fill=mint)
            d.rounded_rectangle((10,14,22,25),radius=4,fill=mint)
            d.line([(11,18),(5,13),(3,9)],fill=mint,width=4)
            d.line([(21,18),(27,13),(29,9)],fill=mint,width=4)
            d.line([(13,23),(9,29)],fill=mint,width=4);d.line([(19,23),(23,29)],fill=mint,width=4)
            d.point((14,8),fill=ink);d.point((18,8),fill=ink)
        elif name=='floss':
            d.rounded_rectangle((4,11,27,29),radius=5,fill=ink)
            d.rounded_rectangle((6,13,25,27),radius=4,fill=mint)
            d.ellipse((11,17,20,23),fill=cream)
            d.line([(10,11),(10,5),(17,3),(23,6),(23,10)],fill=cream,width=2)
            d.line((20,10,27,10),fill=gold,width=2)
        elif name=='mouthwash':
            d.rounded_rectangle((7,2,23,7),radius=2,fill=ink)
            d.rectangle((9,3,21,6),fill=lilac)
            d.rounded_rectangle((5,7,25,29),radius=5,fill=ink)
            d.rounded_rectangle((7,9,23,27),radius=4,fill=teal)
            d.rounded_rectangle((9,15,21,24),radius=3,fill=cream)
            d.arc((11,16,19,22),0,180,fill=teal,width=2)
            d.line((10,10,10,13),fill=white,width=2)
        elif name=='spit':
            d.ellipse((2,4,17,20),fill=ink)
            d.ellipse((4,6,15,18),fill=mint)
            d.ellipse((12,12,17,15),fill=ink)
            d.point((9,10),fill=ink)
            for x,y in ((20,16),(25,19)):
                d.ellipse((x,y,x+3,y+3),fill=lilac)
            d.polygon([(9,25),(29,25),(26,30),(12,30)],fill=cream)
        else:  # A folded face cloth for the final clean-up.
            d.rounded_rectangle((3,7,28,27),radius=4,fill=ink)
            d.rounded_rectangle((5,9,26,25),radius=3,fill=cream)
            d.line((6,21,25,21),fill=lilac,width=2)
            d.polygon([(19,9),(26,9),(26,16)],fill=mint)
            d.line((9,1,9,7),fill=gold,width=2)
            d.line((6,4,12,4),fill=gold,width=2)
        folder='health';(root/folder).mkdir(exist_ok=True);path=f'{folder}/{name}.png';im.save(root/path)
        records.append(dict(id=8001+index,key=f'{folder}.{name}',path=path,kind=folder,width=32,height=32,pivot=[16,16],bounds=list(im.getchannel('A').getbbox()),purpose='Healthy clicker activity; ring and actor overlay'))
    for index,name in enumerate(('star','heart','orb','comet','wings','music','idea','rainbow')):
        im=Image.new('RGBA',(16,16));d=ImageDraw.Draw(im)
        if name=='star':
            d.polygon([(8,0),(10,5),(15,6),(11,10),(12,15),(8,12),(3,15),(4,10),(0,6),(6,5)],fill=gold)
            d.line((7,5,6,7),fill=white,width=2)
        elif name=='heart':
            d.ellipse((1,3,8,10),fill=pink);d.ellipse((7,3,14,10),fill=pink)
            d.polygon([(2,8),(13,8),(8,14),(6,13)],fill=pink);d.line((3,5,5,4),fill=white,width=2)
        elif name=='orb':
            d.ellipse((2,2,13,13),outline=mint,width=2);d.arc((4,4,11,11),180,280,fill=white,width=2)
            d.point((11,11),fill=lilac)
        elif name=='comet':
            d.line([(1,14),(6,9),(12,4)],fill=lilac,width=3)
            d.line([(4,14),(9,9)],fill=pink,width=2)
            d.polygon([(11,0),(13,3),(15,4),(13,6),(11,9),(9,6),(7,4),(9,3)],fill=gold)
            d.rectangle((10,3,12,5),fill=white)
        elif name=='wings':
            d.polygon([(7,8),(3,2),(0,3),(1,9),(5,12),(7,11)],fill=lilac)
            d.polygon([(8,8),(12,2),(15,3),(14,9),(10,12),(8,11)],fill=mint)
            d.line([(2,5),(5,9),(7,9),(9,9),(13,5)],fill=white,width=2)
            d.ellipse((6,7,9,12),fill=gold)
        elif name=='music':
            d.line([(5,11),(5,3),(13,1),(13,10)],fill=lilac,width=3)
            d.line((5,4,12,2),fill=white,width=1)
            d.ellipse((1,9,6,14),fill=pink);d.ellipse((9,8,14,13),fill=pink)
        elif name=='idea':
            d.ellipse((3,1,12,10),fill=gold)
            d.line((5,4,6,3),fill=white,width=2)
            d.rectangle((6,10,9,12),fill=cream)
            d.rectangle((6,14,9,14),fill=lilac)
            d.point((0,3),fill=gold);d.point((15,3),fill=gold)
        else:  # A bold arch and two small clouds, readable at native size.
            d.arc((0,1,15,16),180,360,fill=pink,width=3)
            d.arc((3,4,12,15),180,360,fill=gold,width=2)
            d.arc((5,6,10,15),180,360,fill=mint,width=2)
            d.ellipse((0,9,5,14),fill=white);d.ellipse((10,9,15,14),fill=white)
        folder='effects';(root/folder).mkdir(exist_ok=True);path=f'{folder}/{name}.png';im.save(root/path)
        records.append(dict(id=9001+index,key=f'{folder}.{name}',path=path,kind=folder,width=16,height=16,pivot=[8,8],bounds=list(im.getchannel('A').getbbox()),purpose='Magical celebration sprite; native RGB565 fade'))
    grays=[f'#{v:02x}{v:02x}{v:02x}' for v in range(0,211,14)]
    for index,name in enumerate(('home','garden')):
        im=Image.new('RGBA',(64,64));pixels=im.load()
        for y in range(64):
            for x in range(64):
                radius=math.hypot(x-31.5,y-31.5)/32
                shade=182*max(0,1-max(0,radius-.60)/.40)**.8
                # Quiet scene marks sit inside the vignette, below the creature.
                if name=='home':
                    if y in (42,43) or (y>43 and (y%7==0 or (x+((y//7)%2)*12)%24==0)):shade-=28
                    if 9<=x<=22 and 16<=y<=32:
                        shade+=14
                        if x in (9,10,15,16,21,22) or y in (16,17,24,31,32):shade-=56
                    if 44<=x<=52 and 33<=y<=40:shade-=35
                    if (x-48)**2+(y-29)**2<26:shade-=28
                else:
                    if y>43+int(3*math.sin(x/9)):shade-=28
                    if ((x-11)**2+(y-36)**2<48 or (x-51)**2+(y-35)**2<65):shade-=42
                    if 41<y<53 and (x+2*y)%17<2:shade-=28
                    if (x-47)**2+(y-17)**2<16:shade+=28
                value=max(0,min(210,int((shade+7)/14)*14));pixels[x,y]=(value,value,value,255)
        folder='backgrounds';(root/folder).mkdir(exist_ok=True);path=f'{folder}/{name}.png';im.save(root/path)
        records.append(dict(id=10001+index,key=f'{folder}.{name}',path=path,kind=folder,width=64,height=64,pivot=[32,32],bounds=[0,0,64,64],palette=grays,purpose='Subtle neutral scene with black vignette; nearest sampling at panel size'))
    from location_art import create_location_assets
    records.extend(create_location_assets(root))
    return records
