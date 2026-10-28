#!/usr/bin/env python3
"""Build an independent Debian ARM64 RAM root from an existing Debian tree."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import posixpath
import re
import stat
import subprocess

INIT = b'''#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
exec </dev/console >/dev/console 2>&1
mount -t devtmpfs devtmpfs /dev
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mkdir -p /tmp /run /mnt
echo 0 > /proc/sys/kernel/sysrq
export PS1='root@x20-ram:~# '
echo 'X20_RAM_ROOT_READY: Debian rescue; MMC filesystems unmounted'
while :; do /bin/sh -i; done
'''


def build(root):
    files = {}
    manifest = {}
    environment = dict(os.environ, LC_ALL='C')

    def resolve(name):
        name = posixpath.normpath('/' + name.lstrip('/'))
        for _ in range(32):
            parts = name.strip('/').split('/')
            for i in range(len(parts)):
                p = root.joinpath(*parts[:i+1])
                if p.is_symlink():
                    target = str(p.readlink())
                    base = '/' + '/'.join(parts[:i])
                    name = posixpath.normpath(posixpath.join(base, target, *parts[i+1:]))
                    break
            else:
                return root / name.lstrip('/')
        raise ValueError(f'symlink loop: {name}')

    def add_elf(name, source=None):
        name = name.lstrip('/')
        if name in files:
            return
        p = resolve(source or name)
        data = p.read_bytes()
        if data[:4] != b'\x7fELF' or int.from_bytes(data[18:20], 'little') != 183:
            raise ValueError(f'expected Debian ARM64 executable: {p}')
        files[name] = (stat.S_IFREG | 0o755, data, 0, 0)
        manifest[name] = {'source': str(p.relative_to(root)), 'sha256': hashlib.sha256(data).hexdigest()}
        dynamic = subprocess.check_output(['readelf', '-d', str(p)], text=True, env=environment)
        program = subprocess.check_output(['readelf', '-l', str(p)], text=True, env=environment)
        for interpreter in re.findall(r'Requesting program interpreter: (.*?)\]', program):
            add_elf(interpreter)
        for needed in re.findall(r'Shared library: \[(.*?)\]', dynamic):
            add_elf('lib/aarch64-linux-gnu/' + needed)

    for name in ['dash', 'cat', 'mount', 'umount', 'mkdir', 'ls', 'dd',
                 'sync', 'sleep', 'stty', 'dmesg']:
        add_elf('bin/' + ('sh' if name == 'dash' else name), 'usr/bin/' + name)
    for name in ['blkid', 'blockdev']:
        add_elf('sbin/' + name, 'usr/sbin/' + name)
    files['init'] = (stat.S_IFREG | 0o755, INIT, 0, 0)
    files['dev/console'] = (stat.S_IFCHR | 0o600, b'', 5, 1)
    files['dev/null'] = (stat.S_IFCHR | 0o666, b'', 1, 3)
    directories = {'dev', 'proc', 'sys', 'tmp', 'run', 'mnt'}
    for name in list(files):
        parent = posixpath.dirname(name)
        while parent:
            directories.add(parent)
            parent = posixpath.dirname(parent)
    entries = [(d, (stat.S_IFDIR | 0o755, b'', 0, 0)) for d in sorted(directories)]
    entries += sorted(files.items())
    entries.append(('TRAILER!!!', (0, b'', 0, 0)))
    archive = bytearray()
    for inode, (name, (mode, data, major, minor)) in enumerate(entries, 1):
        fields = [inode, mode, 0, 0, 1, 0, len(data), 0, 0, major, minor, len(name)+1, 0]
        archive += b'070701' + ''.join(f'{n:08x}' for n in fields).encode()
        archive += name.encode() + b'\0'
        archive += b'\0' * (-len(archive) % 4)
        archive += data
        archive += b'\0' * (-len(archive) % 4)
    archive += b'\0' * (-len(archive) % 512)
    return gzip.compress(archive, mtime=0), manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        data, manifest = build(args.rootfs.resolve())
        args.output.write_bytes(data)
        args.output.with_suffix('.json').write_text(json.dumps({
            'purpose': 'Debian independent RAM rescue; no automount or automatic writes',
            'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data),
            'inputs': manifest}, indent=2) + '\n')
        print(f'Debian RAM root: {len(data)} bytes')
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f'rescue initramfs: {exc}\n')


if __name__ == '__main__':
    main()
