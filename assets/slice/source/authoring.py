# /// script
# requires-python = ">=3.12,<3.13"
# dependencies = ["Pillow==12.0.0"]
# ///
"""Reproduce candidate PNGs into build/assets/reimport, never over source artwork."""
from pathlib import Path
from PIL import Image, ImageDraw
import argparse
import json
from menu_icons import create_menu_assets, create_meter_assets
from healthy_art import create_healthy_assets
from prize_art import create_prize_assets
source_root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=source_root.parents[1] / "build/assets/reimport")
root = parser.parse_args().output.resolve()
if root == source_root or source_root in root.parents:
    raise SystemExit("Choose an output outside the source artwork tree")
for folder in ("source", "creatures", "icons", "props", "font"):
    (root / folder).mkdir(parents=True, exist_ok=True)
palette=['#291b35','#49334f','#72516b','#187b79','#43bca2','#85e4b6','#cff5cf','#7655a3','#a47bdb','#d2b8f3','#fff4cf','#ffffff','#d84f70','#fa8c99','#f5c764','#ae7855']
rgb=[tuple(bytes.fromhex(c[1:])) for c in palette]
atlas=Image.open(source_root/'source/creatures-props-atlas.png').convert('RGBA')
assert atlas.width==atlas.height
cell=atlas.width/4
assets=[]
def save(im, key, group, ident, **extra):
 path=f'{group}/{key}.png'; im.save(root/path)
 a=im.getchannel('A').getbbox()
 assets.append(dict(id=ident,key=f'{group}.{key}',path=path,kind=group,width=im.width,height=im.height,pivot=[im.width//2,im.height-2],bounds=list(a) if a else [0,0,0,0],**extra))
# Technical export: fixed grid extraction, nearest-neighbor sizing, binary alpha,
# nearest palette color. No pose changes or AI-image repainting.
for i in range(16):
 x,y=(i%4)*cell,(i//4)*cell
 im=atlas.crop((round(x),round(y),round(x+cell),round(y+cell)))
 size=32 if i<12 else 24
 im=im.resize((size,size),Image.Resampling.NEAREST)
 px=[]
 for r,g,b,a in im.getdata():
  if a<128: px.append((0,0,0,0)); continue
  col=min(rgb,key=lambda c:sum((c[j]-[r,g,b][j])**2 for j in range(3)))
  px.append((*col,255))
 im.putdata(px)
 if i<12:
  form='baby' if i<6 else 'grown'; pose=['idle-a','idle-b','eating','happy','asleep','unwell'][i%6]
  save(im,f'{form}-{pose}','creatures',1001+i,form=form,pose=pose)
 else:
  save(im,['food-bowl','gift-closed','gift-open','bed'][i-12],'props',3001+i-12)
# Two paintover-ready posture keys per form. Existing state artwork stays intact.
for form_index, form in enumerate(('baby','grown')):
 for pose_index, pose in enumerate(('curious','content')):
  source=Image.open(root/'creatures'/f"{form}-{'idle-a' if pose=='curious' else 'happy'}.png")
  im=Image.new('RGBA',(32,32))
  if pose=='curious':
   for y in range(32):
    shift=2 if y<16 else 1 if y<23 else 0
    im.paste(source.crop((0,y,32-shift,y+1)),(shift,y))
  else:
   im.paste(source.resize((32,28),Image.Resampling.NEAREST),(0,4))
  save(im,f'{form}-{pose}','creatures',1021+form_index*2+pose_index,form=form,pose=pose)
# Original code-authored 16px UI geometry; shared palette, no external icon set.
for n,key in enumerate(['basic-care','food','play','clean','rest','wake','medicine','gift','reward','inventory','back','confirm']):
 im=Image.new('RGBA',(16,16)); d=ImageDraw.Draw(im)
 ink,mint,cream,coral,gold,purple=[rgb[i] for i in [0,4,10,13,14,8]]
 def rect(box,c): d.rectangle(box,fill=c)
 def line(points,c=ink,w=1): d.line(points,fill=c,width=w)
 if key=='basic-care':
  d.ellipse((1,2,8,10),fill=ink);d.ellipse((7,2,14,10),fill=ink)
  d.polygon([(2,7),(13,7),(12,11),(9,14),(7,14),(3,10)],fill=ink)
  d.ellipse((2,3,7,9),fill=coral);d.ellipse((8,3,13,9),fill=coral)
  d.polygon([(3,7),(12,7),(11,10),(8,13),(4,9)],fill=coral)
  rect((7,6,8,10),cream);rect((5,7,10,8),cream)
 elif key=='food':
  d.polygon([(1,7),(14,7),(12,13),(3,13)],fill=ink)
  d.polygon([(3,8),(12,8),(11,11),(4,11)],fill=mint)
  for x,y in [(4,4),(8,3),(11,5)]:rect((x,y,x+2,y+2),coral);rect((x+1,y-1,x+1,y-1),mint)
 elif key=='play':
  d.ellipse((2,2,13,13),fill=ink);d.ellipse((3,3,12,12),fill=gold)
  d.polygon([(7,3),(10,5),(9,9),(5,9),(4,5)],fill=purple)
  line([(3,10),(6,9),(8,12)],cream);line([(10,5),(12,6)],cream)
 elif key=='clean':
  rect((3,7,12,12),ink);rect((4,8,11,11),mint)
  for x,y in [(4,4),(9,3),(12,5)]:rect((x,y,x+1,y+1),cream)
  line([(7,1),(7,5)],gold);line([(5,3),(9,3)],gold)
 elif key=='rest':
  d.ellipse((1,1,14,14),fill=ink)
  d.ellipse((2,2,13,13),fill=cream)
  d.ellipse((6,0,16,10),fill=ink)
  d.ellipse((7,-1,17,9),fill=(0,0,0,0))
  for y in range(16):
   for x in range(16):
    if (x-7.5)**2+(y-7.5)**2>7**2: im.putpixel((x,y),(0,0,0,0))
 elif key=='wake':
  d.ellipse((4,4,11,11),fill=ink);d.ellipse((5,5,10,10),fill=gold)
  for box in ((7,1,8,2),(7,13,8,14),(1,7,2,8),(13,7,14,8)):rect(box,gold)
 elif key=='medicine':
  rect((5,1,10,3),ink);rect((4,4,11,13),ink);rect((5,5,10,12),cream)
  rect((7,6,8,10),coral);rect((6,7,9,9),coral)
 elif key=='gift':
  rect((2,6,13,13),ink);rect((3,7,12,12),mint);rect((1,4,14,6),ink);rect((2,5,13,5),mint)
  rect((7,4,8,12),coral);d.rounded_rectangle((3,1,6,4),radius=1,outline=coral);d.rounded_rectangle((9,1,12,4),radius=1,outline=coral);rect((7,3,8,5),coral)
 elif key=='reward':
  d.polygon([(8,0),(10,5),(15,5),(11,9),(13,15),(8,12),(2,15),(4,9),(0,5),(6,5)],fill=ink)
  d.polygon([(8,3),(9,6),(12,6),(10,9),(11,12),(8,10),(5,12),(6,9),(3,6),(7,6)],fill=gold)
 elif key=='inventory':
  rect((5,2,10,5),ink);rect((6,3,9,5),mint);rect((3,5,12,13),ink);rect((4,6,11,12),purple)
  rect((5,9,10,11),cream);rect((7,7,8,8),gold)
 elif key=='back':
  d.polygon([(7,2),(1,8),(7,14),(7,10),(14,10),(14,6),(7,6)],fill=ink)
  d.polygon([(6,4),(2,8),(6,12),(6,10),(13,10),(13,6),(6,6)],fill=cream)
 elif key=='confirm':
  line([(2,8),(6,12),(13,4)],ink,4);line([(2,7),(6,11),(13,3)],mint,2)
 save(im,key,'icons',2001+n)
# Original five-column, seven-row small-caps bitmap glyphs. Lowercase maps to
# small-cap letterforms deliberately; no system font or third-party font input.
rows={
'A':['01110','10001','10001','11111','10001','10001','10001'],
'B':['11110','10001','10001','11110','10001','10001','11110'],
'C':['01111','10000','10000','10000','10000','10000','01111'],
'D':['11110','10001','10001','10001','10001','10001','11110'],
'E':['11111','10000','10000','11110','10000','10000','11111'],
'F':['11111','10000','10000','11110','10000','10000','10000'],
'G':['01111','10000','10000','10111','10001','10001','01111'],
'H':['10001','10001','10001','11111','10001','10001','10001'],
'I':['01110','00100','00100','00100','00100','00100','01110'],
'J':['00111','00010','00010','00010','10010','10010','01100'],
'K':['10001','10010','10100','11000','10100','10010','10001'],
'L':['10000','10000','10000','10000','10000','10000','11111'],
'M':['10001','11011','10101','10101','10001','10001','10001'],
'N':['10001','11001','10101','10011','10001','10001','10001'],
'O':['01110','10001','10001','10001','10001','10001','01110'],
'P':['11110','10001','10001','11110','10000','10000','10000'],
'Q':['01110','10001','10001','10001','10101','10010','01101'],
'R':['11110','10001','10001','11110','10100','10010','10001'],
'S':['01111','10000','10000','01110','00001','00001','11110'],
'T':['11111','00100','00100','00100','00100','00100','00100'],
'U':['10001','10001','10001','10001','10001','10001','01110'],
'V':['10001','10001','10001','10001','10001','01010','00100'],
'W':['10001','10001','10001','10101','10101','11011','10001'],
'X':['10001','10001','01010','00100','01010','10001','10001'],
'Y':['10001','10001','01010','00100','00100','00100','00100'],
'Z':['11111','00001','00010','00100','01000','10000','11111'],
'0':['01110','10001','10011','10101','11001','10001','01110'],
'1':['00100','01100','00100','00100','00100','00100','01110'],
'2':['01110','10001','00001','00010','00100','01000','11111'],
'3':['11110','00001','00001','01110','00001','00001','11110'],
'4':['00010','00110','01010','10010','11111','00010','00010'],
'5':['11111','10000','10000','11110','00001','00001','11110'],
'6':['01110','10000','10000','11110','10001','10001','01110'],
'7':['11111','00001','00010','00100','01000','01000','01000'],
'8':['01110','10001','10001','01110','10001','10001','01110'],
'9':['01110','10001','10001','01111','00001','00001','01110']}
patterns={' ':'00000/00000/00000/00000/00000/00000/00000','!':'00100/00100/00100/00100/00100/00000/00100','"':'01010/01010/01010/00000/00000/00000/00000','#':'01010/11111/01010/01010/11111/01010/00000','$':'00100/01111/10100/01110/00101/11110/00100','%':'11001/11010/00010/00100/01000/01011/10011','&':'01100/10010/10100/01000/10101/10010/01101',"'":'00100/00100/01000/00000/00000/00000/00000','(':'00010/00100/01000/01000/01000/00100/00010',')':'01000/00100/00010/00010/00010/00100/01000','*':'00000/10101/01110/11111/01110/10101/00000','+':'00000/00100/00100/11111/00100/00100/00000',',':'00000/00000/00000/00000/00100/00100/01000','-':'00000/00000/00000/11111/00000/00000/00000','.':'00000/00000/00000/00000/00000/00110/00110','/':'00001/00010/00010/00100/01000/01000/10000',':':'00000/00110/00110/00000/00110/00110/00000',';':'00000/00110/00110/00000/00100/00100/01000','<':'00010/00100/01000/10000/01000/00100/00010','=':'00000/00000/11111/00000/11111/00000/00000','>':'01000/00100/00010/00001/00010/00100/01000','?':'01110/10001/00001/00010/00100/00000/00100','@':'01110/10001/10111/10101/10111/10000/01110','[':'01110/01000/01000/01000/01000/01000/01110','\\':'10000/01000/01000/00100/00010/00010/00001',']':'01110/00010/00010/00010/00010/00010/01110','^':'00100/01010/10001/00000/00000/00000/00000','_':'00000/00000/00000/00000/00000/00000/11111','`':'01000/00100/00010/00000/00000/00000/00000','{':'00010/00100/00100/01000/00100/00100/00010','|':'00100/00100/00100/00100/00100/00100/00100','}':'01000/00100/00100/00010/00100/00100/01000','~':'00000/00000/01001/10110/00000/00000/00000',chr(127):'11111/10001/10101/10101/10101/10001/11111'}
rows.update({k:v.split('/') for k,v in patterns.items()})
font=Image.new('RGBA',(128,72));fd=ImageDraw.Draw(font)
for code in range(32,128):
 ch=chr(code); pattern=rows[ch.upper() if ch.islower() else ch]
 gx=((code-32)%16)*8;gy=((code-32)//16)*12
 for y,row in enumerate(pattern):
  for x,bit in enumerate(row):
   if bit=='1':fd.point((gx+x+1,gy+y+2),fill=(*rgb[10],255))
save(font,'jelli-smallcaps','font',4001,glyph_width=8,glyph_height=12,columns=16,first_codepoint=32,glyph_count=96,advance=8)
(root/'source/font-patterns.json').write_text(json.dumps(rows,indent=2)+'\n')
clips=[]
for form in ['baby','grown']:
 for pose in ['idle','eating','happy','asleep','unwell']:
  keys=[f'creatures.{form}-idle-a',f'creatures.{form}-idle-b'] if pose=='idle' else [f'creatures.{form}-{pose}']
  clips.append(dict(id=len(clips)+5001,key=f'{form}.{pose}',frames=keys,durations_ms=[450]*len(keys),loop=pose=='idle'))
for form in ('baby','grown'):
 for pose in ('curious','content'):
  clips.append(dict(id=len(clips)+5001,key=f'{form}.{pose}',frames=[f'creatures.{form}-{pose}'],durations_ms=[900],loop=False))
assets.extend(create_healthy_assets(root, palette))
assets.extend(create_menu_assets(root, palette))
assets.extend(create_meter_assets(root, palette))
assets.extend(create_prize_assets(root, palette))
manifest=dict(schema_version=1,name='Jelligotchi vertical slice',status='Ring menu artwork integrated; physical readability review pending',pixel_format='RGBA PNG; binary alpha; export RGB565 little-endian plus MSB-first row masks',palette=palette,assets=assets,clips=clips)
(root/'assets.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(f'Created {len(assets)} PNG assets from atlas {atlas.size}; {len(clips)} clips')
