#!/usr/bin/env python3
"""X20 UART rescue V2: bounded RAM transfer, retry, cancel and Debian boot."""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import time
import zlib


class Rescue:
    def __init__(self, port):
        self.port = port

    def read(self, n, timeout=3):
        end = time.monotonic() + timeout
        data = bytearray()
        while len(data) < n and time.monotonic() < end:
            data.extend(self.port.read(n - len(data)))
        if len(data) != n:
            raise TimeoutError(f"UART expected {n} bytes, received {len(data)}")
        return bytes(data)

    def request(self, packet, timeout=3):
        self.port.write(packet)
        self.port.flush()
        reply = self.read(8, timeout)
        return reply[:4], struct.unpack_from("<I", reply, 4)[0]

    def ping(self):
        # Allow a partial command to expire and the startup banner to drain.
        # A fresh boot can still be switching from console text to protocol.
        for attempt in range(3):
            self.port.reset_input_buffer()
            try:
                if self.request(b"PING", timeout=2)[0] == b"PONG":
                    return
            except TimeoutError:
                pass
            if attempt < 2:
                time.sleep(1.1)
        raise RuntimeError("UART rescue V2 endpoint did not answer PING")

    def cancel(self):
        if self.request(b"CANC") != (b"CANC", 0):
            raise RuntimeError("cancel rejected")

    def upload(self, data):
        if not 1024 <= len(data) <= 0x800000:
            raise ValueError("bundle must be 1 KiB..8 MiB")
        if self.request(b"LOAD" + struct.pack("<II", len(data), zlib.crc32(data))) != (b"RDY!", 0):
            raise RuntimeError("LOAD rejected")
        for off in range(0, len(data), 4096):
            chunk = data[off:off+4096]
            packet = b"DATA" + struct.pack("<III", off, len(chunk), zlib.crc32(chunk)) + chunk
            for attempt in range(3):
                try:
                    result = self.request(packet)
                    if result == (b"OKAY", off + len(chunk)):
                        break
                    if result != (b"RETR", off):
                        raise RuntimeError(f"DATA rejected at {off}: {result}")
                except TimeoutError:
                    if attempt == 2:
                        raise
                    self.port.reset_input_buffer()
            else:
                raise RuntimeError(f"CRC retry limit at {off}")
        return len(data)

    def boot(self, size):
        if self.request(b"BOOT", timeout=30) != (b"BOOT", size):
            raise RuntimeError("BOOT rejected; a valid Debian initramfs entry is required")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--bundle", type=Path)
    parser.add_argument("--boot", action="store_true")
    parser.add_argument("--cancel", action="store_true")
    args = parser.parse_args()
    if args.boot and not args.bundle:
        parser.error("--boot requires --bundle")
    import serial
    try:
        # WCH on Linux needs this transition for the non-standard baud rate.
        with serial.Serial(args.port, 9600, timeout=0.05, write_timeout=2, exclusive=True) as port:
            port.baudrate = 921600
            endpoint = Rescue(port)
            endpoint.ping()
            if args.cancel:
                endpoint.cancel()
            if args.bundle:
                data = args.bundle.read_bytes()
                endpoint.upload(data)
                print(f"RAM: {len(data)} bytes sha256={hashlib.sha256(data).hexdigest()}", flush=True)
                if args.boot:
                    endpoint.boot(len(data))
            print("UART rescue V2: OK")
    except (OSError, ValueError, TimeoutError, RuntimeError, serial.SerialException) as exc:
        parser.exit(1, f"UART rescue: {exc}\n")


if __name__ == "__main__":
    main()
