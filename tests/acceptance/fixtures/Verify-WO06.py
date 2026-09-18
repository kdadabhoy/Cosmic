"""Independent binary + numeric CSV accounting; never calls Cosmic loaders.
Usage: Verify-WO06.py <session-directory> [--zeros] [--output <json>]
"""
import argparse, csv, hashlib, json, math, struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('folder',type=Path);p.add_argument('--zeros',action='store_true');p.add_argument('--output',type=Path)
args=p.parse_args();b=(args.folder/'scene.bin').read_bytes();at=0
def read(fmt):
    global at
    s=struct.Struct('<'+fmt);v=s.unpack_from(b,at);at+=s.size;return v
def text(n):
    global at
    value=b[at:at+n];at+=n
    assert len(value)==n and b'\0' in value
    return value.split(b'\0')[0].decode('ascii')
magic,version,count,rate=read('4sIIf');assert (magic,version,count,rate)==(b'CSMC',1,3,60.)
entities=[]
base=['Temp_C','Voltage_V','Current_A','Consumption_mAh','eRPM','MotorRPM']
for i in range(count):
    name,tag=text(64),text(64);channels,samples=read('II');names=[text(32) for _ in range(channels)]
    expected=base+(['WeaponRPM','TipSpeed_mph','Power_W'] if name=='ESC_Weapon' else ['Speed_mph','Power_W'])
    assert names==expected and samples==432000
    assert (name,tag) in [('ESC_Right','Drive'),('ESC_Left','Drive'),('ESC_Weapon','Weapon')]
    entities.append((name,tag,names,samples))
result=[]
for name,tag,names,samples in entities:
    motor=35000/(3 if name=='ESC_Weapon' else 6)
    expected=[25.,16.8,4.2,120.,35000.,motor]
    if name=='ESC_Weapon': expected += [motor/4,motor/4*math.pi*7.874/1056,16.8*4.2]
    else: expected += [motor/19/.933*math.pi*3.5/1056,16.8*4.2]
    if args.zeros: expected=[0.]*len(names)
    previous=-1.;first_bits=None
    with (args.folder/(name+'.csv')).open(newline='',encoding='ascii') as f:
        reader=csv.reader(f);assert next(reader)==['Time']+names
        for i in range(samples):
            offset=at;row=read('f'*(len(names)+1));assert math.isfinite(row[0]) and row[0]>=previous;previous=row[0]
            bits=b[offset+4:at]
            if first_bits is None:
                first_bits=bits
                for got,want in zip(row[1:],expected):assert abs(got-want)<=1e-6+1e-5*abs(want),(name,got,want)
            assert bits==first_bits # each stored sample has the exact expected constant source bits
            csv_row=next(reader);assert len(csv_row)==len(row)
            assert all(float(v)==x for v,x in zip(csv_row,row)),(name,i)
        assert next(reader,None) is None
    result.append({'name':name,'samples':samples,'channels':names,'last_timestamp':previous,'source_float_bits_hex':first_bits.hex(),'csv_rows':samples})
assert at==len(b)
report={'oracle':'independent Python little-endian decoder and numeric CSV reader; analytic channel constants','zeros':args.zeros,'entities':result,'bytes':len(b),'sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(args.folder.iterdir()) if f.suffix in ('.bin','.csv')}}
out=json.dumps(report,indent=2)+'\n'
if args.output:args.output.write_text(out,encoding='utf-8')
print(out)
