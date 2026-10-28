#!/usr/bin/env python3
"""Guarded X20 LK/bundle deployment over SSH; never writes Preloader or GPT."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
from bundle import verify

CID = '90014a483847346192a15278594aa351'
REMOTE = r'''
import hashlib,json,os,subprocess,sys
from pathlib import Path
kind,expected,cid=sys.argv[1:]
data=sys.stdin.buffer.read(0x800001)
if hashlib.sha256(data).hexdigest()!=expected or len(data)>0x800000:
    raise SystemExit('transfer SHA/size guard failed')
matches=[p for p in Path('/sys/block').glob('mmcblk*')
         if Path('/dev',p.name+'boot0').exists()]
if len(matches)!=1: raise SystemExit('expected one eMMC with boot0')
p=matches[0]
if (p/'device/cid').read_text().strip()!=cid or (p/'device/type').read_text().strip()!='MMC':
    raise SystemExit('eMMC CID/type guard failed')
if kind=='lk':
    part=Path('/sys/class/block',p.name+'p1')
    target='/dev/'+part.name
    label=subprocess.check_output(['blkid','-p','-s','PART_ENTRY_NAME','-o','value',target],text=True).strip()
    capacity=int((part/'size').read_text())*512
    if label!='lk' or capacity!=0x80000 or len(data)>capacity:
        raise SystemExit('lk partition label/size guard failed')
    payload=data+b'\xff'*(capacity-len(data));offset=0
elif kind=='bundle':
    target='/dev/'+p.name;offset=0x800000
    for part in Path('/sys/class/block').glob(p.name+'p*'):
        start=int((part/'start').read_text())*512
        end=start+int((part/'size').read_text())*512
        if offset<end and start<offset+0x800000:
            raise SystemExit('raw bundle range overlaps GPT partition')
    payload=data
else: raise SystemExit('invalid deployment kind')
digest=hashlib.sha256(payload).hexdigest()
fd=os.open(target,os.O_RDWR|os.O_SYNC)
try:
    os.lseek(fd,offset,os.SEEK_SET)
    view=memoryview(payload)
    while view:
        done=os.write(fd,view)
        if done<=0: raise SystemExit('short device write')
        view=view[done:]
    os.fsync(fd);os.sync();os.lseek(fd,offset,os.SEEK_SET)
    readback=bytearray()
    while len(readback)<len(payload):
        block=os.read(fd,len(payload)-len(readback))
        if not block: raise SystemExit('short readback')
        readback.extend(block)
    if hashlib.sha256(readback).hexdigest()!=digest:
        raise SystemExit('readback SHA mismatch')
finally: os.close(fd)
print(json.dumps({'kind':kind,'device':target,'offset':offset,'size':len(payload),
                  'input_sha256':expected,'readback_sha256':digest,'cid':cid}))
'''


def checked_image(path, kind):
    data = path.read_bytes()
    if kind == 'lk':
        meta = json.loads(path.with_suffix('.json').read_text())
        if (not meta.get('persistent_allowed') or meta['target'] not in ['emmc', 'tf'] or
                meta['sha256'] != hashlib.sha256(data).hexdigest() or meta['size'] != len(data) or
                b'x20-lk/rescue' in data or len(data) < 512 or len(data) > 0x80000 or
                struct.unpack_from('<I', data)[0] != 0x58881688):
            raise ValueError('persistent LK manifest/header guard failed')
    else:
        verify(data)
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='root@192.168.31.87')
    p.add_argument('--jump', help='optional SSH ProxyJump host')
    p.add_argument('--kind', choices=['lk', 'bundle'], required=True)
    p.add_argument('--image', type=Path, required=True)
    p.add_argument('--check-only', action='store_true', help='validate local artifact without board access')
    args = p.parse_args()
    try:
        data = checked_image(args.image, args.kind)
        digest = hashlib.sha256(data).hexdigest()
        if args.check_only:
            print(f'{args.kind}: local guards passed sha256={digest}')
            return
        command = 'LC_ALL=C python3 -c ' + shlex.quote(REMOTE) + ' ' + ' '.join(
            shlex.quote(s) for s in [args.kind, digest, CID])
        ssh = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5']
        if args.jump:
            ssh.extend(['-J', args.jump])
        subprocess.run(ssh + [args.host, command], input=data, check=True)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as exc:
        p.exit(1, f'deploy: {exc}\n')


if __name__ == '__main__':
    main()
