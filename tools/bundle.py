#!/usr/bin/env python3
"""Fixed X20 v1 Debian bundle: kernel (Image.gz[-dtb]), dtb, initramfs only."""
import argparse
import hashlib
from pathlib import Path
import struct
import zlib

MAX = 0x800000
SPECS = {"kernel": (2, 0x46900000, 0x40080000, MAX),
         "dtb": (3, 0x44000000, 0, 0x100000),
         "initramfs": (4, 0x45000000, 0, MAX)}


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def pack(inputs):
    if "kernel" not in inputs or set(inputs) - SPECS.keys():
        raise ValueError("kernel required; only kernel/dtb/initramfs supported")
    data = bytearray(1024)
    struct.pack_into("<8s5I4xQQ", data, 0, b"X20BNDL\0", 1, 1024, 64, len(inputs), 1, 0, 1024)
    for i, (name, payload) in enumerate(inputs.items()):
        kind, load, pc, limit = SPECS[name]
        if not 0 < len(payload) <= limit:
            raise ValueError(f"{name} size outside fixed range")
        off = len(data)
        struct.pack_into("<IIQQQQI4x16s", data, 64 + i * 64,
                         kind, 1, off, len(payload), load, pc, crc(payload), name.encode())
        data.extend(payload)
        data.extend(b"\0" * (-len(data) % 512))
    struct.pack_into("<Q", data, 32, len(data))
    struct.pack_into("<I", data, 48, crc(data[:1024]))
    verify(data)
    return bytes(data)


def verify(data):
    if len(data) < 1024:
        raise ValueError("short header")
    magic, version, hdr, ent, count, flags, total, payload = struct.unpack_from("<8s5I4xQQ", data)
    if (magic, version, hdr, ent, flags, payload) != (b"X20BNDL\0", 1, 1024, 64, 1, 1024):
        raise ValueError("header format")
    if not 1 <= count <= 3 or not 1024 <= total <= MAX or total > len(data) or total % 512:
        raise ValueError("bundle bounds")
    header = bytearray(data[:1024])
    stored, = struct.unpack_from("<I", header, 48)
    header[48:52] = b"\0" * 4
    if stored != crc(header):
        raise ValueError("header CRC")
    seen, ranges, result = set(), [], []
    for i in range(count):
        kind, flags, off, size, load, pc, checksum, raw = struct.unpack_from("<IIQQQQI4x16s", data, 64 + i * 64)
        name = raw.rstrip(b"\0").decode("ascii")
        if name not in SPECS or raw != name.encode().ljust(16, b"\0") or name in seen:
            raise ValueError("entry name/duplicate")
        etype, addr, entry, limit = SPECS[name]
        if kind != etype or flags & ~3 or (kind != 2 and flags & 2):
            raise ValueError("entry type/flags")
        if load not in (0, addr) or pc not in (0, entry) or not 0 < size <= limit:
            raise ValueError("entry destination")
        if off < 1024 or off % 512 or off > total or size > total - off:
            raise ValueError("entry bounds")
        if any(off < b and a < off + size for a, b in ranges):
            raise ValueError("entry overlap")
        if crc(data[off:off+size]) != checksum:
            raise ValueError("entry CRC")
        seen.add(name)
        ranges.append((off, off + size))
        result.append({"name": name, "offset": off, "size": size, "load": hex(addr)})
    if "kernel" not in seen:
        raise ValueError("kernel missing")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    packing = commands.add_parser("pack")
    packing.add_argument("--output", type=Path, required=True)
    packing.add_argument("--kernel", type=Path, required=True)
    packing.add_argument("--dtb", type=Path)
    packing.add_argument("--initramfs", type=Path)
    check = commands.add_parser("verify")
    check.add_argument("file", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "pack":
            inputs = {name: getattr(args, name).read_bytes() for name in SPECS if getattr(args, name)}
            data = pack(inputs)
            args.output.write_bytes(data)
        else:
            data = args.file.read_bytes()
        entries = verify(data)
        print(f"X20 bundle: {len(data)} bytes, sha256={hashlib.sha256(data).hexdigest()}")
        for entry in entries:
            print(f"  {entry['name']}: {entry['size']} bytes at {entry['load']}")
    except (ValueError, OSError, UnicodeError) as exc:
        parser.exit(1, f"bundle: {exc}\n")


if __name__ == "__main__":
    main()
