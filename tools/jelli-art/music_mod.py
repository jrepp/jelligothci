"""Bounded four-channel ProTracker M.K. MOD codec for Jelli Art.

PCM is signed mono 8-bit. Loop offsets/lengths are bytes, always even.
Only no-effect, set volume (Cxx), and speed/tempo (Fxx) are supported.
"""
import struct

PERIODS = (856,808,762,720,678,640,604,570,538,508,480,453,
           428,404,381,360,339,320,302,285,269,254,240,226,
           214,202,190,180,170,160,151,143,135,127,120,113)
MAX_BYTES = 131072
MAX_SAMPLE_BYTES = 32768


def integer(value, low, high, label):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{label} must be an integer {low}..{high}')
    return value


def text(value, size, label):
    if not isinstance(value, str) or len(value) > size or any(not 32 <= ord(c) <= 126 for c in value):
        raise ValueError(f'{label} needs at most {size} printable ASCII characters')
    return value.encode('ascii').ljust(size, b'\0')


def fields(value, keys, label):
    if not isinstance(value, dict) or set(value) != set(keys.split()):
        raise ValueError(f'{label} requires {keys}')


def sequence(value, low, high, label):
    if not isinstance(value, list) or not low <= len(value) <= high:
        raise ValueError(f'{label} needs {low}..{high} entries')
    return value


def encode(doc):
    fields(doc, 'title restart orders samples patterns', 'Module')
    header = bytearray(text(doc['title'], 20, 'Title'))
    integer(doc['restart'], 0, 127, 'Restart')
    orders = sequence(doc['orders'], 1, 16, 'Orders')
    patterns = sequence(doc['patterns'], 1, 16, 'Patterns')
    for order in orders:
        integer(order, 0, len(patterns)-1, 'Order pattern')
    if max(orders) != len(patterns)-1:
        raise ValueError('Highest pattern must be referenced by the order list')
    samples = sequence(doc['samples'], 31, 31, 'Samples')
    pcm = bytearray()
    for sample in samples:
        fields(sample, 'name volume loop_start loop_length pcm', 'Sample')
        values = sequence(sample['pcm'], 0, 8192, 'Sample PCM')
        length = len(values)
        if length % 2:
            raise ValueError('Sample length must be even')
        start = integer(sample['loop_start'], 0, length, 'Loop start')
        loop = integer(sample['loop_length'], 0, length, 'Loop length')
        if start % 2 or loop % 2 or start+loop > length or (loop == 0 and start != 0) or loop == 2:
            raise ValueError('Loop must be disabled (0,0) or an even range of at least four bytes')
        volume = integer(sample['volume'], 0, 64, 'Sample volume')
        header += text(sample['name'], 22, 'Sample name')
        header += struct.pack('>HBBHH', length//2, 0, volume, start//2, loop//2 if loop else 1)
        if len(pcm)+length > MAX_SAMPLE_BYTES:
            raise ValueError('Combined PCM exceeds 32768 bytes')
        for value in values:
            pcm.append(integer(value, -128, 127, 'PCM sample') & 255)
    header += bytes([len(orders), doc['restart']]) + bytes(orders).ljust(128, b'\0') + b'M.K.'
    events = bytearray()
    for pattern in patterns:
        for row in sequence(pattern, 64, 64, 'Pattern rows'):
            for cell in sequence(row, 4, 4, 'Row channels'):
                fields(cell, 'sample period effect param', 'Cell')
                sample = integer(cell['sample'], 0, 31, 'Instrument')
                period = integer(cell['period'], 0, 856, 'Period')
                effect = integer(cell['effect'], 0, 15, 'Effect')
                param = integer(cell['param'], 0, 255, 'Effect parameter')
                if period and period not in PERIODS:
                    raise ValueError('Note must use a C1..B3 ProTracker period')
                if effect not in (0, 12, 15) or (effect == 0 and param != 0):
                    raise ValueError('Only empty effect, Cxx volume, and Fxx speed/tempo are supported')
                if (effect == 12 and param > 64) or (effect == 15 and param == 0):
                    raise ValueError('Cxx volume must be 0..64; Fxx must be 1..255')
                events.extend(((sample & 240) | (period >> 8), period & 255,
                               ((sample & 15) << 4) | effect, param))
    return bytes(header+events+pcm)


def decode(data):
    if not isinstance(data, bytes) or not 1084 <= len(data) <= MAX_BYTES:
        raise ValueError('MOD must contain 1084..131072 bytes')
    if data[1080:1084] != b'M.K.':
        raise ValueError('Only four-channel M.K. MOD files are supported')
    count = data[950]
    if not 1 <= count <= 16:
        raise ValueError('MOD needs 1..16 orders')
    orders = list(data[952:952+count])
    pattern_count = max(orders)+1
    if pattern_count > 16:
        raise ValueError('MOD supports at most 16 patterns')
    end = 1084+1024*pattern_count
    if end > len(data):
        raise ValueError('Truncated MOD patterns')

    def label(raw):
        try:
            value = raw.split(b'\0', 1)[0].decode('ascii')
        except UnicodeDecodeError as exc:
            raise ValueError('MOD labels must be ASCII') from exc
        text(value, len(raw), 'MOD label')
        return value

    samples = []
    for i in range(31):
        offset = 20+i*30
        length, fine, volume, start, loop = struct.unpack_from('>HBBHH', data, offset+22)
        if fine:
            raise ValueError('Sample finetune is unsupported; use zero')
        length *= 2
        if length > 8192 or end+length > len(data):
            raise ValueError('Oversized or truncated MOD sample')
        values = [v if v < 128 else v-256 for v in data[end:end+length]]
        end += length
        samples.append({'name': label(data[offset:offset+22]), 'volume': volume,
                        'loop_start': start*2 if loop > 1 else 0,
                        'loop_length': loop*2 if loop > 1 else 0, 'pcm': values})
    if end != len(data):
        raise ValueError('Trailing bytes after MOD samples')
    patterns = []
    for p in range(pattern_count):
        rows = []
        for row in range(64):
            cells = []
            for channel in range(4):
                offset = 1084+p*1024+row*16+channel*4
                a,b,c,d = data[offset:offset+4]
                cells.append({'sample': (a & 240) | (c >> 4), 'period': ((a & 15) << 8) | b,
                              'effect': c & 15, 'param': d})
            rows.append(cells)
        patterns.append(rows)
    doc = {'title': label(data[:20]), 'restart': data[951], 'orders': orders,
           'samples': samples, 'patterns': patterns}
    encode(doc)  # One schema/bounds contract for binary imports and editor saves.
    return doc
