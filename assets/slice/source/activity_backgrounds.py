# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0"]
# ///
"""Small original activity accents composed with the existing shared scenery kit.

Generate candidates outside assets/slice; copy reviewed PNGs and records explicitly.
No existing artwork or gameplay catalog is rewritten by this recipe.
"""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageDraw
from background_kit import KIT_PALETTE

C = KIT_PALETTE
# Stable asset order, independent of scene order and gameplay IDs.
ACCENTS = ('breakfast', 'tea', 'movie', 'reading', 'lunch', 'dinner', 'dessert',
           'yoga', 'jogging', 'fishing', 'day-dreaming', 'nap', 'chess', 'drawing',
           'thinking', 'gardening', 'soccer', 'volleyball', 'swimming', 'baseball',
           'bug-catching', 'science', 'snuggle', 'workout')


def accent(name):
    im = Image.new('RGBA', (24, 24))
    d = ImageDraw.Draw(im)
    def box(b, color): d.rectangle(b, fill=C[color])
    def line(points, color, width=1): d.line(points, fill=C[color], width=width)
    def oval(b, color): d.ellipse(b, fill=C[color])
    def pot(x, y):
        box((x,y,x+6,y+1),8);d.polygon([(x+1,y+2),(x+5,y+2),(x+4,y+6),(x+2,y+6)],fill=C[7])
        line((x+3,y-5,x+3,y),2);oval((x-1,y-6,x+3,y-3),3);oval((x+3,y-8,x+7,y-4),4)
    def table(y=17):
        box((1,y,22,y+2),8);line((3,y+3,3,23),6);line((20,y+3,20,23),6)
        line((2,y,21,y),10)
    def books(y=5):
        box((1,y-1,22,y+8),6)
        for x,h,c in ((3,6,3),(7,5,8),(11,7,12),(15,5,9),(19,6,4)):
            box((x,y+6-h,x+2,y+6),c);line((x,y+2,x+1,y+2),10)
        line((2,y+7,21,y+7),8)
    if name == 'breakfast':
        table();box((3,12,11,16),10);box((4,11,10,11),11);line((5,13,9,13),8)
        box((16,10,20,16),11);box((17,7,19,9),10);line((17,12,19,12),8)
        oval((2,1,12,8),10);line((7,2,7,5,10,5),7)
    elif name == 'tea':
        table();oval((3,12,12,17),7);oval((4,10,11,15),10);box((6,8,9,9),8)
        line((11,11,15,10,12,14),10,2);oval((17,12,21,17),11)
        line((18,8,17,5,18,3),13);pot(16,5)
    elif name == 'movie':
        box((0,0,23,13),6);box((2,1,21,11),10);box((3,2,20,10),12)
        d.polygon([(9,4),(9,8),(14,6)],fill=C[14]);box((0,0,3,14),7);box((20,0,23,14),7)
        for x in (1,13):
            box((x,17,x+8,22),7);box((x+1,15,x+7,19),8);line((x+2,20,x+6,20),9)
    elif name == 'reading':
        books(2);books(12);box((1,22,22,23),7)
    elif name == 'lunch':
        table(16);box((2,16,21,19),10)
        for x in range(3,22,4): line((x,16,x,19),7)
        line((2,18,21,18),7);box((7,10,15,15),8);oval((7,7,15,14),7);box((8,10,14,14),9)
        line((9,12,13,12),10)
    elif name == 'dinner':
        table();box((3,13,9,16),11);box((15,13,21,16),11)
        line((12,6,12,16),10);oval((11,3,13,6),11)
        for x in (3,19): line((x,0,x,3),7);oval((x-2,3,x+2,7),9);box((x-1,4,x+1,6),11)
    elif name == 'dessert':
        box((1,8,22,22),8);box((2,9,21,19),12);line((2,14,21,14),10)
        for x,y in ((4,11),(13,11),(5,16),(15,16)):
            box((x,y+1,x+4,y+2),10);oval((x,y-1,x+4,y+1),11);d.point((x+2,y-2),fill=C[7])
        box((0,5,23,7),3)
        for x in range(1,24,6): box((x,5,x+2,7),5)
    elif name == 'yoga':
        box((0,18,23,21),4);line((2,19,21,19),5)
        for b,c in (((4,14,19,17),12),((6,10,17,13),13),((8,7,15,9),14)): oval(b,c)
        pot(1,8)
    elif name == 'jogging':
        box((1,9,3,23),6);box((0,3,21,10),8);box((2,4,19,8),10)
        line((5,6,15,6,13,4),7);line((15,6,13,8),7)
        box((13,19,22,23),9);d.polygon([(15,19),(18,12),(21,19)],fill=C[8]);line((17,16,19,16),11)
    elif name == 'fishing':
        table(16);line((2,18,21,18),6);line((5,17,5,19),10);line((15,17,15,19),10)
        line((18,16,20,4,16,1),7);line((16,1,9,18),12);oval((7,18,11,20),10)
        for x in (2,5): line((x,13,x,4),2);box((x-1,3,x,7),7)
    elif name == 'day-dreaming':
        for x,y in ((2,4),(14,1),(18,10),(6,15)):
            line((x,y,x+4,y+4),10);line((x+4,y,x,y+4),10);d.point((x+2,y+2),fill=C[11])
        oval((5,3,17,10),15);box((3,7,19,11),15);oval((7,1,14,9),15)
    elif name == 'nap':
        for x in (1,22): box((x,2,x+1,23),7)
        d.polygon([(2,10),(7,14),(17,14),(22,9),(18,19),(8,20)],fill=C[9])
        line((2,10,8,17,17,16,22,9),11);oval((5,13,11,16),10)
    elif name == 'chess':
        table(18)
        for y in range(4):
            for x in range(6): box((3+x*3,13+y,5+x*3,13+y),10 if (x+y)%2 else 7)
        box((5,8,7,12),6);box((4,7,8,8),6);box((15,9,17,12),11);oval((15,7,17,9),11)
    elif name == 'drawing':
        line((6,3,2,23),7,2);line((16,3,22,23),7,2);box((3,3,20,17),8);box((5,4,18,15),11)
        d.polygon([(5,14),(9,9),(12,12),(15,7),(18,14)],fill=C[3]);oval((6,6,9,8),9)
        line((2,18,22,18),6,2);oval((11,20,18,23),8)
    elif name == 'thinking':
        box((4,19,20,22),7);box((7,16,17,18),8);oval((7,7,17,17),13);oval((8,4,16,11),14)
        line((11,4,11,1,16,1),12);line((17,11,21,9),12);line((8,12,5,9),12)
    elif name == 'gardening':
        table(18);pot(3,11);pot(13,9);line((20,4,20,17),7);box((18,15,22,18),12)
        box((1,21,10,23),8);line((3,22,8,22),9)
    elif name == 'soccer':
        line((2,21,2,4,21,4,21,21),11,2);line((2,4,8,0,23,0,21,4),13)
        for x in range(5,22,4): line((x,5,x,20),14)
        for y in range(8,21,4): line((3,y,20,y),14)
        oval((12,17,19,23),10);box((14,19,16,21),6)
    elif name == 'volleyball':
        line((1,1,1,23),7,2);line((22,1,22,23),7,2);box((2,4,21,5),11)
        for x in range(3,22,3): line((x,6,x,14),13)
        for y in range(8,15,3): line((2,y,21,y),13)
        oval((11,17,17,23),11);line((12,18,16,22),9)
    elif name == 'swimming':
        line((2,3,2,23),11,2);line((10,3,10,23),11,2);oval((2,0,10,6),11)
        for y in (8,13,18): line((2,y,10,y),12)
        oval((14,10,23,19),10);oval((17,13,20,16),12)
        line((15,21,23,21),14)
    elif name == 'baseball':
        box((0,2,23,12),6);box((2,3,21,10),3)
        for x in (4,12): box((x,5,x+4,8),10);line((x+2,5,x+2,8),3)
        line((3,13,3,23),7);line((20,13,20,23),7);line((7,22,17,15),8,3)
        oval((17,17,22,22),11);line((18,18,20,21),7)
    elif name == 'bug-catching':
        line((15,11,6,23),7,2);oval((10,1,23,14),10);oval((12,3,21,12),4)
        for x in (14,17,20): line((x,4,x-2,11),13)
        for y in (6,9): line((12,y,20,y),13)
        oval((1,4,6,8),9);oval((5,4,9,8),10);line((5,5,5,10),6)
    elif name == 'science':
        table(18);line((4,5,4,17,12,17),12,2);oval((6,6,13,12),13)
        box((9,2,12,7),6);line((8,12,15,12),7,2)
        d.polygon([(18,8),(21,8),(21,12),(23,16),(16,16),(18,12)],fill=C[14])
        box((18,13,21,15),3);d.point((20,6),fill=C[14])
    elif name == 'snuggle':
        box((1,14,22,23),7);oval((2,7,21,21),9);box((3,16,20,21),10)
        oval((8,10,17,19),8);oval((8,7,11,10),8);oval((14,7,17,10),8)
        oval((10,9,15,14),9);d.point((11,11),fill=C[6]);d.point((14,11),fill=C[6])
        box((3,18,9,21),4)
    elif name == 'workout':
        box((1,1,22,14),7);box((3,3,20,12),13);line((5,4,10,9),14);line((15,4,19,8),14)
        line((2,20,22,20),6,2)
        for x in (3,18): box((x,16,x+3,23),7);line((x+1,17,x+1,22),8)
    else: raise ValueError(name)
    return im


def shared(name):
    if name == 'clouds':
        im=Image.new('RGBA',(64,16));d=ImageDraw.Draw(im)
        for x,y in ((2,5),(37,10)):
            d.rectangle((x,y,x+16,y+2),fill=C[15]);d.rectangle((x+4,y-2,x+11,y),fill=C[15])
    elif name == 'room':
        im=Image.new('RGBA',(64,32),C[9]);d=ImageDraw.Draw(im)
        d.rectangle((0,24,63,31),fill=C[8]);d.line((0,24,63,24),fill=C[7])
        for x in range(0,64,8): d.line((x,25,x,31),fill=C[7])
        d.rectangle((24,5,40,21),fill=C[7]);d.rectangle((26,7,38,19),fill=(0,0,0,0))
        d.line((32,7,32,19),fill=C[10]);d.line((24,21,40,21),fill=C[10])
    elif name == 'floor':
        im=Image.new('RGBA',(64,32),C[10]);d=ImageDraw.Draw(im)
        for y in (0,9,21):
            d.line((0,y,63,y),fill=C[9])
            for x in range((y%2)*8,64,16):d.line((x,y,x,y+8),fill=C[9])
    elif name in ('turf','sand'):
        im=Image.new('RGBA',(64,32),C[4 if name=='turf' else 10]);d=ImageDraw.Draw(im)
        for x,y in ((2,6),(18,14),(38,5),(55,23),(7,25),(31,29),(47,16)):
            d.line((x,y,x+2,y),fill=C[3 if name=='turf' else 9])
            if name=='turf':d.point((x+1,y-1),fill=C[5])
    elif name == 'markings':
        im=Image.new('RGBA',(64,16));d=ImageDraw.Draw(im)
        d.line((0,3,63,3),fill=C[11]);d.line((0,12,63,12),fill=C[11])
        for x in (0,32):d.line((x,3,x,12),fill=C[11])
    elif name == 'water':
        im=Image.new('RGBA',(64,32),C[12]);d=ImageDraw.Draw(im)
        for y in (2,9,18,27):
            for x in range((y%3)*7,64,28):d.line((x,y,x+13,y),fill=C[13]);d.line((x+2,y+1,x+9,y+1),fill=C[14])
    else: raise ValueError(name)
    return im


def layer(asset,name,x=0,y=0,travel=0,repeat=False):
    return dict(asset='backgrounds.'+asset.replace('activity-', 'act-'),name=name,x=x,y=y,tilt_x=0,tilt_y=0,
                travel=travel,repeat_x=repeat,visible=True)


def scenes():
    # Quiet settings pin furniture/ground; outdoor movement has distinct depth ratios.
    indoor={'breakfast','movie','reading','dinner','dessert','thinking','science','snuggle','workout'}
    speeds={'going-out':8,'jogging':12,'soccer':4,'volleyball':3,'swimming':6,'baseball':3,'bug-catching':5,'workout':2}
    titles={'breakfast':'Morning kitchen','tea':'Arbor tea garden','going-out':'Neighbourhood outing',
            'movie':'Little cinema','reading':'Library nook','lunch':'Picnic pavilion','dinner':'Lantern supper',
            'dessert':'Patisserie window','yoga':'Stone meditation garden','jogging':'Park running trail',
            'fishing':'Reed-lined fishing dock','day-dreaming':'Cloud meadow','nap':'Hammock grove',
            'chess':'Chess terrace','drawing':'Garden art easel','thinking':'Quiet study',
            'gardening':'Potting garden','soccer':'Meadow football pitch','volleyball':'Beach volleyball',
            'swimming':'Open-water swimming','baseball':'Village ballpark','bug-catching':'Butterfly trail',
            'science':'Window-side laboratory','snuggle':'Cushion corner','workout':'Sunlit workout studio'}
    result=[]
    for name,title in titles.items():
        layers=[layer('kit-sky','Shared sky'),layer('activity-clouds','Drifting clouds',y=1,travel=60,repeat=True)]
        if name=='going-out':
            layers += [layer('kit-town','Distant town',y=-20,travel=15,repeat=True),layer('kit-street-fronts','Shopfronts',travel=45,repeat=True),layer('kit-sidewalk','Walking lane',travel=80,repeat=True),layer('kit-street-edge','Curbside details',travel=100,repeat=True)]
        else:
            if name in indoor:
                layers += [layer('activity-room','Panelled room',x=8 if name in ('movie','reading','science') else -8 if name in ('dinner','snuggle') else 0,repeat=True),layer('activity-floor','Wooden floor',y=32,repeat=True)]
            else:
                layers += [layer('kit-hills','Distant hills',y=-12,travel=15,repeat=True)]
                if name not in ('volleyball','swimming','day-dreaming'):
                    layers += [layer('kit-trees','Shared grove',y=-23,travel=40 if speeds.get(name,1)>2 else 0,repeat=True)]
                if name in ('fishing','swimming','volleyball'):
                    layers += [layer('activity-water','Moving water',y=32 if name=='swimming' else 23,travel=100,repeat=True)]
                if name!='swimming':
                    ground='floor' if name in ('tea','chess','yoga') else 'sand' if name=='volleyball' else 'turf'
                    layers += [layer('activity-'+ground,'Terrace' if ground=='floor' else 'Sand' if ground=='sand' else 'Grass',y=32,travel=90 if name in ('jogging','bug-catching') else 0,repeat=True)]
                    if name in ('jogging','soccer','baseball'):
                        layers += [layer('activity-markings','Sports ground markings',y=38,travel=100 if name=='jogging' else 0,repeat=True)]
            # Paired strips keep the unused motif outside the 64px viewport.
            pair=ACCENTS.index(name)//2
            left=ACCENTS.index(name)%2 == 1
            layers += [layer('activity-pair-'+str(pair),title+' details',x=-24 if left else 40,y=7)]
            if name in ('lunch','chess'):
                layers += [layer('activity-pair-7','Shared potted greenery',x=-24,y=8)]
        result.append(dict(key='activity-'+name,name=title,fill=C[15],seed=0,
                           activity=name.replace('-',' ').upper(),speed=speeds.get(name,1),layers=layers))
    return result


def build(output):
    output.mkdir(parents=True,exist_ok=True);(output/'backgrounds').mkdir(exist_ok=True)
    records=[]
    for i,name in enumerate(('clouds','room','floor','water')+tuple('pair-'+str(i) for i in range(12))+('turf','sand','markings')):
        if name.startswith('pair-'):
            pair=int(name[5:]);im=Image.new('RGBA',(48,24))
            im.paste(accent(ACCENTS[pair*2]),(0,0));im.paste(accent(ACCENTS[pair*2+1]),(24,0))
        else:
            im=shared(name)
            if name in ('floor','water','turf','sand'):im=im.crop((0,0,32,32))
        path=f'backgrounds/activity-{name}.png';im.save(output/path)
        records.append(dict(id=10016+i,key='backgrounds.act-'+name,path=path,kind='backgrounds',
                            width=im.width,height=im.height,pivot=[im.width//2,im.height//2],
                            bounds=list(im.getchannel('A').getbbox()),palette='earth-kit',
                            purpose=f'Reusable {name} activity layer; native geometry in activity_backgrounds.py'))
    (output/'records.json').write_text(json.dumps(records,indent=2)+'\n')
    (output/'scenes.json').write_text(json.dumps(scenes(),indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,default=Path('build/assets/activity-backgrounds'))
    output=parser.parse_args().output.resolve();source=Path(__file__).resolve().parents[1]
    if output==source or source in output.parents:raise SystemExit('Generate candidates outside source art')
    build(output)
