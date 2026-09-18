"""Independent little-endian v1 specimens. These are NOT SF-Stable captures.
Do not regenerate pinned files during acceptance; this script documents origin.
"""
import struct, hashlib, json
from pathlib import Path
root = Path(__file__).parent / 'wo06'
root.mkdir(exist_ok=True)
entities = [
    ('Empty','zero',['value'],[]),
    ('One','one',['value'],[(1.25,-0.0)]),
    ('A','many',['value'],[(0.,8.),(2.,16.),(4.,32.)]),
    ('B','delayed-unequal',['value'],[(1.,100.),(3.,300.)]),
]
def text(s,n):
    return s.encode('ascii').ljust(n,b'\0')
def encode(es):
    b=bytearray(struct.pack('<4sIIf',b'CSMC',1,len(es),60.))
    for name,tag,channels,rows in es:
        b.extend(text(name,64)+text(tag,64)+struct.pack('<II',len(channels),len(rows)))
        for ch in channels: b.extend(text(ch,32))
    for _,_,_,rows in es:
        for row in rows: b.extend(struct.pack('<'+'f'*len(row),*row))
    return b
good=encode(entities)
(root/'independent-v1.bin').write_bytes(good)
(root/'fallback-A.bin').write_bytes(encode([entities[2]]))
bad=bytearray(good);bad[4:8]=struct.pack('<I',99)
(root/'bad-version.bin').write_bytes(bad)
(root/'truncated.bin').write_bytes(good[:-1])
(root/'manifest.json').write_text(json.dumps({
 'provenance':'Independently encoded from documented v1 layout; no SF-Stable runtime available.',
 'entities':entities,
 'sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.glob('*.bin'))}
},indent=2)+'\n',encoding='utf-8')
