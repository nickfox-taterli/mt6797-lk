#!/usr/bin/env python3
"""Exercise the actual C parsers with truncation, overflow and corrupted data."""
import ctypes as C
import gzip
import importlib.util
from pathlib import Path
import random
import struct
import sys

spec = importlib.util.spec_from_file_location("bundle", Path(__file__).parents[1] / "tools/bundle.py")
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)
lib = C.CDLL(sys.argv[1])
lib.bundle_check.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p]
lib.gzip_decode.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t,
                           C.POINTER(C.c_size_t), C.POINTER(C.c_size_t)]
lib.fdt_prepare.argtypes = [C.c_void_p, C.c_size_t, C.c_size_t, C.c_uint32]


def ccheck(data):
    return lib.bundle_check(C.create_string_buffer(bytes(data)), len(data), C.create_string_buffer(128))


def fix_header(data):
    struct.pack_into("<I", data, 48, 0)
    struct.pack_into("<I", data, 48, b.crc(data[:1024]))
    return data


def bad_field(data, offset, fmt, value):
    data = bytearray(data)
    struct.pack_into(fmt, data, offset, value)
    return fix_header(data)


valid = b.pack({"kernel": b"kernel", "dtb": b"dtb", "initramfs": b"initramfs"})
assert ccheck(valid) == 0
for size in range(len(valid)):
    assert ccheck(valid[:size]) < 0, size
for off, fmt, values in [
    (20, "<I", [0, 4, 15, 0xffffffff]),
    (32, "<Q", [0, 512, 0x800200, 2**64-1]),
    (64, "<I", [1, 5, 6, 0xffffffff]),
    (64+4, "<I", [4, 0xffffffff]),
    (64+8, "<Q", [0, 1025, 2**64-512]),
    (64+16, "<Q", [0, 2**32, 2**64-1]),
    (64+24, "<Q", [0x46000000, 0x44600000, 0x50000000, 0x100000000]),
    (64+32, "<Q", [0x40000000, 0x46000000, 2**64-1]),
    (128+8, "<Q", [1024]),
]:
    for value in values:
        bad = bad_field(valid, off, fmt, value)
        assert ccheck(bad) < 0, (off, value)
        try:
            b.verify(bad)
            raise AssertionError((off, value))
        except ValueError:
            pass
duplicate = bytearray(valid)
duplicate[128:192] = duplicate[64:128]
assert ccheck(fix_header(duplicate)) < 0
payload_bad = bytearray(valid)
payload_bad[1024] ^= 1
assert ccheck(payload_bad) == -2
rng = random.Random(6797)
for _ in range(1500):
    data = bytearray(valid)
    offset = rng.randrange(64, 256)
    data[offset] ^= rng.randrange(1, 256)
    fix_header(data)
    accepted = ccheck(data) == 0
    try:
        b.verify(data)
        pyaccepted = True
    except (ValueError, UnicodeError):
        pyaccepted = False
    assert accepted == pyaccepted, (offset, accepted)


def decode(data, cap):
    buf = C.create_string_buffer(cap + 32)
    C.memset(C.addressof(buf) + cap, 0xa5, 32)
    used, written = C.c_size_t(), C.c_size_t()
    ret = lib.gzip_decode(C.create_string_buffer(data), len(data), buf, cap,
                          C.byref(used), C.byref(written))
    assert buf.raw[cap:] == b"\xa5" * 32, "output overwrite"
    return ret, used.value, buf.raw[:written.value]


for raw in [b"x", bytes(range(256)) * 200, b"A" * 200000]:
    packed = gzip.compress(raw, mtime=0)
    assert decode(packed + b"\xd0\x0d\xfe\xedDTB", len(raw)) == (0, len(packed), raw)
    assert decode(packed, len(raw)-1)[0] < 0
    for size in range(min(len(packed), 500)):
        assert decode(packed[:size], len(raw))[0] < 0
    for i in [-1, -5, -8]:
        bad = bytearray(packed)
        bad[i] ^= 1
        assert decode(bytes(bad), len(raw))[0] < 0
# Optional gzip fields and header CRC.
packed = bytearray(gzip.compress(b"hello", mtime=0))
header = bytearray(packed[:10]); header[3] = 0x1e
header += b"\x03\0abcname\0comment\0"
header += struct.pack("<H", b.crc(header) & 0xffff)
stream = bytes(header + packed[10:])
assert decode(stream, 5) == (0, len(stream), b"hello")

# Build a real FDT in memory, including the two mandatory protected ranges.
tree = C.create_string_buffer(16384)
assert lib.fdt_create_empty_tree(tree, len(tree)) == 0
lib.fdt_setprop.argtypes = [C.c_void_p, C.c_int, C.c_char_p, C.c_void_p, C.c_int]


def prop(node, name, value):
    assert lib.fdt_setprop(tree, node, name.encode(), value, len(value)) == 0


def node(parent, name):
    n = lib.fdt_add_subnode(tree, parent, name.encode())
    assert n >= 0
    return n


prop(0, "compatible", b"archermind,x20-96\0mediatek,mt6797\0")
prop(0, "#address-cells", struct.pack(">I", 2))
prop(0, "#size-cells", struct.pack(">I", 2))
node(0, "memory@40000000")
reserved = node(0, "reserved-memory")
for addr, size in [(0x43f00000, 0x1000), (0x44600000, 0x10000)]:
    n = node(reserved, f"memory@{addr:x}")
    prop(n, "reg", struct.pack(">4I", 0, addr, 0, size))
    prop(n, "no-map", b"")
assert lib.fdt_pack(tree) == 0
size, = struct.unpack_from(">I", tree.raw, 4)
original = tree.raw[:size]
assert lib.fdt_prepare(tree, size, len(tree), 4096) == 0
assert b"linux,initrd-start\0" in tree.raw
new_size, = struct.unpack_from(">I", tree.raw, 4)
assert lib.fdt_prepare(tree, new_size, len(tree), 0) == 0
# The property name can remain in the strings table; query the property itself.
chosen = lib.fdt_path_offset(tree, b"/chosen")
lib.fdt_getprop.restype = C.c_void_p
n = C.c_int()
assert not lib.fdt_getprop(tree, chosen, b"linux,initrd-start", C.byref(n))
assert lib.fdt_prepare(tree, 8, len(tree), 0) < 0
bad = C.create_string_buffer(original, len(tree))
C.memset(C.addressof(bad) + 4, 0xff, 4)
assert lib.fdt_prepare(bad, size, len(tree), 0) < 0
lib.test_image_prepare.argtypes = [C.c_void_p, C.c_size_t]


def boot(inputs):
    data = b.pack(inputs)
    return lib.test_image_prepare(C.create_string_buffer(data), len(data))


image = bytearray(256)
struct.pack_into('<QQQ', image, 8, 0, 4096, 0xa)
struct.pack_into('<I', image, 56, 0x644d5241)
compressed = gzip.compress(image, mtime=0)
assert boot({'kernel': compressed + original}) == 1
assert boot({'kernel': compressed, 'dtb': original}) == 1
assert boot({'kernel': compressed + original, 'initramfs': b'RAM root'}) == 1
assert boot({'kernel': compressed}) == -6
assert boot({'kernel': compressed + b'junk', 'dtb': original}) == -5
assert boot({'kernel': compressed + original + b'junk'}) == -6
for off, fmt, value in [(8, '<Q', 0x100000), (16, '<Q', 0x2000000),
                        (24, '<Q', 1), (56, '<I', 0)]:
    invalid = bytearray(image)
    struct.pack_into(fmt, invalid, off, value)
    assert boot({'kernel': gzip.compress(invalid, mtime=0) + original}) == -7
print("bundle bounds/CRC/overlap/address whitelist, gzip and FDT: PASS")
