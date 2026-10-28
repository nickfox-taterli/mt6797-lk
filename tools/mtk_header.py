#!/usr/bin/env python3
"""The fixed vendor Preloader partition header, not an Android boot image."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("raw", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--target", choices=["emmc", "tf", "rescue"], required=True)
args = parser.parse_args()
raw = args.raw.read_bytes()
if not 32 <= len(raw) <= 0x80000 - 512:
    parser.error("LK exceeds the fixed 512 KiB partition")
# mtk mkimage: 512-byte header, payload length, ASCII name, load address,
# mode=-1. This is the header actually consumed by this board's Preloader.
header = bytearray(512)
struct.pack_into("<II32sII", header, 0, 0x58881688, len(raw), b"lk", 0xffffffff, 0xffffffff)
struct.pack_into("<8I", header, 48, 0x58891689, 512, 1, 0, 0, 16, 0, 0)
data = bytes(header) + raw
args.output.write_bytes(data)
manifest = {"target": args.target,
            "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "persistent_allowed": args.target != "rescue"}
args.output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
