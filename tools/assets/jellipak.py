#!/usr/bin/env -S uv run
# /// script
# requires-python = ">=3.15,<3.16"
# dependencies = ["Pillow==13.0.0", "lz4==4.4.5"]
# ///
"""Provisional source-art compiler; no renderer or firmware migration.

JART v1 is the first independently encoded BLOK: 32-byte header, 80-byte image entries, 36-byte
palettes, UTF-8 keys. All words are little endian. See parse_metadata for the
field order. Clips and build provenance are report data, not runtime contracts.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import defaultdict
from contextlib import nullcontext
from importlib.metadata import version

import lz4.block
from build_slice import SOURCE, load_assets, pack_mask, palette_colours
from embed_slice import rgb565
from sprite_geometry import ground_anchor_q8, opaque_centroid_q8

U32 = 0xffffffff
MAX_PACK_BYTES = 256 * 1024 * 1024
MAX_DECODED_BYTES = 64 * 1024 * 1024
FIELDS = ('id width height format palette block offset length index_stride mask_stride '
          'anchor_x_q8 anchor_y_q8 x0 y0 x1 y1 key_offset key_length centroid_x_q8 centroid_y_q8').split()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def crc(data):
    return zlib.crc32(data) & U32


def canonical(data):
    return json.dumps(data, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode()


def groups_for(manifest):
    assets = {a['key']: a for a in manifest['assets']}
    owners, traversal = defaultdict(set), defaultdict(list)
    for clip in sorted(manifest.get('clips', []), key=lambda c: c['key']):
        form = clip['key'].split('.')[0]
        for key in clip['frames']:
            require(key in assets, 'dangling clip frame: ' + key)
            owners[key].add(form)
            if key not in traversal[form]:
                traversal[form].append(key)
    natural, assigned = {}, set()
    for form, keys in sorted(traversal.items()):
        selected = [k for k in keys if assets[k]['kind'] == 'creatures' and len(owners[k]) == 1]
        natural['form:' + form] = selected
        assigned.update(selected)
    for a in sorted(assets.values(), key=lambda a: a['id']):
        if a['key'] not in assigned:
            name = 'asset:' + a['key'] if a['kind'] == 'creatures' else 'kind:' + a['kind']
            natural.setdefault(name, []).append(a['key'])
    groups = defaultdict(list)
    for name, keys in sorted(natural.items()):
        for key in keys:
            a = assets[key]
            if a['kind'] == 'font':
                group = 'font'
            else:
                authored = sorted({rgb565(*bytes.fromhex(c[1:])) for c in palette_colours(manifest, a)})
                scope = name if name.startswith(('form:', 'asset:')) else ('backgrounds' if a['kind'] == 'backgrounds' else 'ui')
                group = scope + ':' + struct.pack('<' + 'H' * len(authored), *authored).hex()
            groups[group].append(key)
    return groups


def compile_art(manifest, images, decoded_limit, expected_blocks=None):
    require(type(decoded_limit) is int and 1 <= decoded_limit <= 1024 * 1024, 'decoded limit must be 1..1048576')
    assets = manifest['assets']
    require(assets and len({a['key'] for a in assets}) == len(assets), 'empty or duplicate asset keys')
    require(len({a['id'] for a in assets}) == len(assets), 'duplicate asset IDs')
    require(all(type(a['id']) is int and 0 < a['id'] <= U32 for a in assets), 'invalid asset ID')
    lookup = {a['key']: a for a in assets}
    palettes, records, payloads, block_groups = [], {}, [], []
    for group, keys in sorted(groups_for(manifest).items()):
        font = group == 'font'
        colours = sorted({rgb565(r,g,b) for key in keys for r,g,b,a in images[key].getdata() if a}) if not font else []
        require(len(colours) <= 16, 'incompatible palette union: ' + group)
        palette = U32
        if not font:
            # Empty palettes explicitly represent wholly transparent groups.
            if colours not in palettes:
                palettes.append(colours)
            palette = palettes.index(colours)
        chunk = bytearray()
        pending = []
        def flush():
            if chunk:
                payloads.append(bytes(chunk))
                block_groups.append(group)
                for key in pending:
                    records[key]['block'] = len(payloads) + 1
                chunk.clear()
                pending.clear()
        for key in keys:
            asset, image = lookup[key], images[key].convert('RGBA')
            w,h = image.size
            require(w > 0 and h > 0, 'empty image: ' + key)
            if font:
                require((w,h) == (128,72), 'font requires 128x72 glyph sheet')
                raw = b''.join(pack_mask(image.crop(((i%16)*8,(i//16)*12,(i%16+1)*8,(i//16+1)*12))) for i in range(96))
                stride, maskstride = 0, 1
            else:
                rgba = list(image.getdata())
                indices = [colours.index(rgb565(r,g,b)) if a else 0 for r,g,b,a in rgba]
                raw = bytes((indices[y*w+x] << 4) | (indices[y*w+x+1] if x+1<w else 0) for y in range(h) for x in range(0,w,2)) + pack_mask(image)
                stride, maskstride = (w+1)//2, (w+7)//8
            require(len(raw) <= decoded_limit, 'image exceeds decoded limit: ' + key)
            if len(chunk) + len(raw) > decoded_limit:
                flush()
            bounds = asset.get('bounds', list(image.getchannel('A').getbbox() or (0,0,0,0)))
            centroid = asset.get('centroid_q8')
            if centroid is None:
                centroid = opaque_centroid_q8(image) if image.getchannel('A').getbbox() else [0,0]
            anchor = asset.get('ground_anchor_q8')
            if anchor is None:
                anchor = ground_anchor_q8(image) if asset['kind'] == 'creatures' and image.getchannel('A').getbbox() else [0,0]
            records[key] = dict(zip(FIELDS, [asset['id'],w,h,2 if font else 1,palette,0,len(chunk),len(raw),stride,maskstride,*anchor,*bounds,0,0,*centroid]))
            chunk.extend(raw)
            pending.append(key)
        flush()
    keys_bytes = bytearray()
    entries = bytearray()
    for key in sorted(records, key=lambda k: records[k]['id']):
        record = records[key]
        encoded = key.encode('utf-8')
        record['key_offset'],record['key_length'] = len(keys_bytes),len(encoded)
        keys_bytes.extend(encoded)
        entries.extend(struct.pack('<20I', *(record[f] & U32 for f in FIELDS)))
    palette_bytes = b''.join(struct.pack('<I16H',len(p),*(p+[0]*(16-len(p)))) for p in palettes)
    metadata = struct.pack('<4s7I',b'JART',1,len(records),len(palettes),32,32+len(entries),32+len(entries)+len(palette_bytes),len(keys_bytes)) + entries + palette_bytes + keys_bytes
    require(len(metadata) <= decoded_limit, 'metadata exceeds decoded limit')
    raw_blocks = [metadata] + payloads
    # Capture immutable originals before any encoder runs; never reconstruct this oracle.
    if expected_blocks is not None:
        expected_blocks.extend((i + 1, bytes(raw)) for i, raw in enumerate(raw_blocks))
    stored, directory, offset = [],bytearray(),32+32*len(raw_blocks)
    block_report = []
    for i,raw in enumerate(raw_blocks):
        compressed = lz4.block.compress(raw,mode='high_compression',compression=12,store_size=False)
        codec = int(len(compressed) < len(raw))
        data = compressed if codec else raw
        directory.extend(struct.pack('<8I',i+1,0x4b4f4c42,codec,offset,len(data),len(raw),crc(data),crc(raw)))
        stored.append(data)
        block_report.append(dict(id=i+1,codec=codec,stored_bytes=len(data),decoded_bytes=len(raw),group='metadata' if i==0 else block_groups[i-1]))
        offset += len(data)
    header = struct.pack('<4sHH5I',b'JPAK',1,32,offset,len(raw_blocks),32,32,crc(directory))
    pack = header + struct.pack('<I',crc(header)) + directory + b''.join(stored)
    report = dict(schema_version=1,pack_bytes=len(pack),pack_sha256=hashlib.sha256(pack).hexdigest(),header_bytes=32,directory_bytes=len(directory),metadata_bytes=len(metadata),palette_table_bytes=len(palette_bytes),image_table_bytes=len(entries),key_bytes=len(keys_bytes),padding_bytes=0,stored_art_bytes=sum(map(len,stored[1:])),decoded_art_bytes=sum(map(len,payloads)),decoded_limit=decoded_limit,blocks=block_report,assets=[dict(key=k,**records[k]) for k in sorted(records)],clips=manifest.get('clips',[]),versions={'Pillow':version('Pillow'),'lz4':version('lz4')},scope='complete serialized pack; no linked firmware savings or runtime migration')
    report['source_revision'] = subprocess.check_output(['git','rev-parse','HEAD'],cwd=SOURCE.parents[1],text=True).strip()
    source_paths = ['assets/slice/assets.json'] + ['assets/slice/'+a['path'] for a in assets if 'path' in a]
    report['source_git_status'] = subprocess.check_output(['git','status','--porcelain','--',*source_paths],cwd=SOURCE.parents[1],text=True).splitlines()
    report['normalized_source_sha256'] = hashlib.sha256(canonical(manifest) + b''.join(k.encode('utf-8') + b'\0' + images[k].convert('RGBA').tobytes() for k in sorted(images))).hexdigest()
    report['compiler_inputs_sha256'] = {name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest() for name in ('jellipak.py','build_slice.py','embed_slice.py','sprite_geometry.py')}
    report['by_kind'] = {}
    for asset in assets:
        kind = asset['kind']
        totals = report['by_kind'].setdefault(kind,dict(assets=0,indexed_mask_bytes=0,baseline_rgb565_mask_bytes=0))
        image = images[asset['key']]
        totals['assets'] += 1
        totals['indexed_mask_bytes'] += records[asset['key']]['length']
        totals['baseline_rgb565_mask_bytes'] += 1152 if kind=='font' else image.width*image.height*2+len(pack_mask(image))
    report['baseline_rgb565_mask_bytes'] = sum(1152 if a['kind']=='font' else images[a['key']].width * images[a['key']].height * 2 + len(pack_mask(images[a['key']])) for a in assets)
    return pack,report


def parse_metadata(raw):
    require(len(raw)>=32,'short JART header')
    magic,v,n,p,ao,po,so,sl=struct.unpack_from('<4s7I',raw)
    require(magic==b'JART' and v==1,'unsupported art schema')
    require(ao==32 and po==ao+80*n and so==po+36*p and so+sl==len(raw),'invalid metadata tables')
    palettes=[]
    for i in range(p):
        row=struct.unpack_from('<I16H',raw,po+36*i)
        require(row[0]<=16,'invalid palette count')
        values=list(row[1:1+row[0]])
        require(values==sorted(set(values)) and not any(row[1+row[0]:]),'noncanonical palette')
        palettes.append(values)
    assets=[]
    for i in range(n):
        record=dict(zip(FIELDS,struct.unpack_from('<20I',raw,ao+80*i)))
        k,l=record['key_offset'],record['key_length']
        require(l>0 and k+l<=sl,'invalid asset key span')
        record['key']=raw[so+k:so+k+l].decode('utf-8')
        assets.append(record)
    require(len({a['key'] for a in assets})==n and len({a['id'] for a in assets})==n,'duplicate metadata IDs/keys')
    require([a['id'] for a in assets]==sorted(a['id'] for a in assets) and all(a['id'] for a in assets),'invalid asset ordering')
    return dict(schema_version=v,assets=assets,palettes=palettes)


def read_pack(data):
    require(32<=len(data)<=MAX_PACK_BYTES,'invalid pack size')
    magic,v,hs,total,n,do,es,dc,hc=struct.unpack_from('<4sHH6I',data)
    require((magic,v,hs,total,do,es)==(b'JPAK',1,32,len(data),32,32),'invalid container header')
    require(0<n<=65536 and 32+32*n<=len(data),'invalid directory count')
    directory=data[32:32+32*n]
    require(crc(data[:28])==hc and crc(directory)==dc,'header/directory CRC mismatch')
    blocks={}
    offset=32+32*n
    # Validate aggregate policy before any potentially large decompression.
    total_decoded = sum(struct.unpack_from('<I', directory, 32*i+20)[0] for i in range(n))
    require(total_decoded<=MAX_DECODED_BYTES,'aggregate decoded size exceeds host policy')
    for i in range(n):
        ident,kind,codec,pos,stored,decoded,sc,rc=struct.unpack_from('<8I',directory,32*i)
        require(ident==i+1 and kind==0x4b4f4c42 and codec in (0,1) and pos==offset and 0<stored<=1024*1024 and 0<decoded<=1024*1024 and pos+stored<=len(data),'invalid block entry')
        encoded=data[pos:pos+stored]
        require(crc(encoded)==sc,'stored CRC mismatch')
        raw=lz4.block.decompress(encoded,uncompressed_size=decoded) if codec else encoded
        require(len(raw)==decoded and crc(raw)==rc,'decoded CRC mismatch')
        blocks[ident]=raw
        offset+=stored
    require(offset==len(data),'trailing bytes')
    metadata=parse_metadata(blocks[1])
    spans=defaultdict(list)
    for a in metadata['assets']:
        w,h=a['width'],a['height']
        require(w>0 and h>0 and a['block'] in blocks and a['block']!=1,'invalid image reference')
        if a['format']==1:
            require(a['palette']<len(metadata['palettes']) and a['index_stride']==(w+1)//2 and a['mask_stride']==(w+7)//8,'invalid indexed geometry')
            expected=(a['index_stride']+a['mask_stride'])*h
        else:
            require(a['format']==2 and (w,h)==(128,72) and a['palette']==U32 and a['index_stride']==0 and a['mask_stride']==1,'invalid font geometry')
            expected=1152
        require(a['length']==expected and a['offset']+expected<=len(blocks[a['block']]),'invalid image span')
        spans[a['block']].append((a['offset'],expected))
        if a['format']==1:
            raw=blocks[a['block']][a['offset']:a['offset']+expected]
            colours=metadata['palettes'][a['palette']]
            for y in range(h):
                for x in range(w):
                    idx=(raw[y*a['index_stride']+x//2]>>(0 if x%2 else 4))&15
                    visible=raw[h*a['index_stride']+y*a['mask_stride']+x//8]&(128>>(x%8))
                    require(idx<len(colours) if visible else idx==0,'invalid palette index')
                require(w%2==0 or raw[y*a['index_stride']+a['index_stride']-1]&15==0,'nibble padding')
                require(w%8==0 or raw[h*a['index_stride']+y*a['mask_stride']+a['mask_stride']-1]&((1<<(8-w%8))-1)==0,'mask padding')
    for ident,raw in blocks.items():
        if ident==1:
            continue
        end=0
        for start,length in sorted(spans[ident]):
            require(start==end,'overlapping or unused art bytes')
            end+=length
        require(end==len(raw),'unreferenced art bytes')
    return metadata,blocks


def verify_fidelity(metadata,blocks,manifest,images):
    source={a['key']:a for a in manifest['assets']}
    require(set(source)=={a['key'] for a in metadata['assets']},'source catalog mismatch')
    for a in metadata['assets']:
        key=a['key']; image=images[key].convert('RGBA'); w,h=image.size
        require((a['id'],a['width'],a['height'])==(source[key]['id'],w,h),'source geometry/ID mismatch: '+key)
        bounds = source[key].get('bounds', list(image.getchannel('A').getbbox() or (0,0,0,0)))
        require([a[f] for f in ('x0','y0','x1','y1')]==bounds,'source bounds mismatch: '+key)
        centroid = source[key].get('centroid_q8')
        if centroid is None:
            centroid = opaque_centroid_q8(image) if image.getchannel('A').getbbox() else [0,0]
        anchor = source[key].get('ground_anchor_q8')
        if anchor is None:
            anchor = ground_anchor_q8(image) if source[key]['kind']=='creatures' and image.getchannel('A').getbbox() else [0,0]
        require([a['anchor_x_q8'],a['anchor_y_q8']]==[n & U32 for n in anchor] and [a['centroid_x_q8'],a['centroid_y_q8']]==[n & U32 for n in centroid],'source anchors mismatch: '+key)
        raw=blocks[a['block']][a['offset']:a['offset']+a['length']]
        if a['format']==2:
            for y in range(h):
                for x in range(w):
                    i=(y//12)*16+x//8
                    visible=bool(raw[i*12+y%12]&(128>>(x%8)))
                    require(visible==bool(image.getpixel((x,y))[3]),'font fidelity: '+key)
        else:
            colours=metadata['palettes'][a['palette']]
            for y in range(h):
                for x in range(w):
                    r,g,b,alpha=image.getpixel((x,y))
                    visible=bool(raw[h*a['index_stride']+y*a['mask_stride']+x//8]&(128>>(x%8)))
                    require(visible==bool(alpha),'coverage fidelity: '+key)
                    if visible:
                        idx=(raw[y*a['index_stride']+x//2]>>(0 if x%2 else 4))&15
                        require(colours[idx]==rgb565(r,g,b),'RGB565 fidelity: '+key)


def native_verify(verifier,path,expected_blocks=None):
    command = [str(Path(verifier).resolve()),str(path)]
    # The stream belongs to this invocation and is removed even on rejection/timeout.
    comparison_area = (tempfile.TemporaryDirectory(dir=Path(path).parent,prefix='.jraw-')
                       if expected_blocks is not None else nullcontext())
    with comparison_area as temporary:
        if expected_blocks is not None:
            expected = Path(temporary)/'expected.jraw'
            with expected.open('wb') as stream:
                stream.write(struct.pack('<4sII',b'JRAW',1,len(expected_blocks)))
                for ident, raw in expected_blocks:
                    stream.write(struct.pack('<II',ident,len(raw)))
                    stream.write(raw)
            command.extend(['--compare',str(expected)])
        result=subprocess.run(command,capture_output=True,text=True,timeout=60)
        require(result.returncode==0,'native verifier rejected candidate: '+result.stderr.strip())
        if expected_blocks is not None:
            compared = sum(len(raw) for _,raw in expected_blocks)
            require('compared_bytes='+str(compared) in result.stdout.splitlines(),
                    'native verifier did not confirm exact byte comparison')
        return result.stdout.strip()


def publish(path,data):
    path=Path(path)
    if path.exists() and path.read_bytes()==data:
        return
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent,prefix='.'+path.name+'.',delete=False) as stream:
            temporary=Path(stream.name); stream.write(data); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def guarded_output(path):
    resolved = path.resolve()
    protected = (SOURCE.resolve(), Path(__file__).resolve().parent)
    require(not any(resolved == root or root in resolved.parents for root in protected),'output would overwrite source/compiler inputs: '+str(path))
    return resolved


def read_bounded(path):
    with path.open('rb') as stream:
        data = stream.read(MAX_PACK_BYTES+1)
    require(len(data)<=MAX_PACK_BYTES,'pack exceeds host size policy')
    return data


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    build=sub.add_parser('build'); build.add_argument('--output',required=True,type=Path); build.add_argument('--verifier',required=True); build.add_argument('--decoded-limit',required=True,type=int); build.add_argument('--report',type=Path)
    inspect=sub.add_parser('inspect'); inspect.add_argument('pack',type=Path)
    verify=sub.add_parser('verify'); verify.add_argument('pack',type=Path); verify.add_argument('--verifier',required=True); verify.add_argument('--source-fidelity',action='store_true')
    args=parser.parse_args(argv)
    try:
        if args.command=='build':
            args.output = guarded_output(args.output)
            if args.report is not None:
                args.report = guarded_output(args.report)
            require(args.report is None or args.report.resolve()!=args.output.resolve(),'report must differ from pack output')
            # Reject duplicate authoring keys before the existing validator collapses them.
            def unique_pairs(pairs):
                result = {}
                for key,value in pairs:
                    require(key not in result,'duplicate source JSON key: '+key)
                    result[key] = value
                return result
            json.loads((SOURCE/'assets.json').read_text(),object_pairs_hook=unique_pairs)
            manifest,images=load_assets()
            expected_blocks=[]
            data,report=compile_art(manifest,images,args.decoded_limit,expected_blocks)
            metadata,blocks=read_pack(data); verify_fidelity(metadata,blocks,manifest,images)
            # The candidate is private until the full C sweep succeeds.
            args.output.parent.mkdir(parents=True,exist_ok=True)
            if args.report:
                args.report.parent.mkdir(parents=True,exist_ok=True)
            with tempfile.TemporaryDirectory(dir=args.output.parent,prefix='.jellipak-') as temporary:
                candidate=Path(temporary)/'candidate.jpak'; candidate.write_bytes(data)
                native_verify(args.verifier,candidate,expected_blocks)
                report['native_byte_comparison']=dict(blocks=len(expected_blocks),bytes=sum(len(raw) for _,raw in expected_blocks),source='precompression')
                publish(args.output,data)
            if args.report:
                publish(args.report,json.dumps(report,indent=2,sort_keys=True).encode()+b'\n')
            print(json.dumps(report,sort_keys=True))
        else:
            data=read_bounded(args.pack); metadata,blocks=read_pack(data)
            result=dict(pack_bytes=len(data),pack_sha256=hashlib.sha256(data).hexdigest(),metadata=metadata,block_count=len(blocks),decoded_bytes=sum(map(len,blocks.values())))
            if args.command=='verify':
                result['native_verifier']=native_verify(args.verifier,args.pack)
                if args.source_fidelity:
                    manifest,images=load_assets(); verify_fidelity(metadata,blocks,manifest,images); result['source_fidelity']=True
            print(json.dumps(result,sort_keys=True))
        return 0
    except (ValueError,OSError,struct.error,RuntimeError,KeyError,subprocess.SubprocessError) as error:
        print('jellipak: '+str(error),file=sys.stderr); return 1


if __name__=='__main__':
    sys.exit(main())
