#!/usr/bin/env python3
"""Host handshake recovery and lost-ACK retransmission against a wire peer."""
import importlib.util
from pathlib import Path
import struct
import zlib

spec = importlib.util.spec_from_file_location('rescue', Path(__file__).parents[1] / 'tools/uart-rescue.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.time.sleep = lambda seconds: None


class Peer:
    def __init__(self):
        self.pending = b''
        self.pings = 0
        self.next = 0
        self.data = bytearray()
        self.lost = False
        self.timeout = False

    def reset_input_buffer(self):
        self.pending = b''

    def flush(self):
        pass

    def read(self, n):
        if self.timeout:
            self.timeout = False
            raise TimeoutError('injected lost ACK')
        block, self.pending = self.pending[:n], self.pending[n:]
        return block

    def write(self, packet):
        command = packet[:4]
        if command == b'PING':
            self.pings += 1
            self.pending = b'old text' if self.pings == 1 else b'PONG' + struct.pack('<I', self.next)
        elif command == b'CANC':
            self.next = 0
            self.pending = b'CANC' + bytes(4)
        elif command == b'LOAD':
            self.length, self.expected = struct.unpack_from('<II', packet, 4)
            self.next = 0
            self.data = bytearray()
            self.pending = b'RDY!' + bytes(4)
        elif command == b'DATA':
            off, n, crc = struct.unpack_from('<III', packet, 4)
            payload = packet[16:]
            assert len(payload) == n and zlib.crc32(payload) == crc
            if off == self.next:
                self.data.extend(payload)
                self.next += n
            else:
                assert bytes(self.data[off:off+n]) == payload
            self.pending = b'OKAY' + struct.pack('<I', self.next)
            if not self.lost:
                self.lost = self.timeout = True
        elif command == b'BOOT':
            assert len(self.data) == self.length and zlib.crc32(self.data) == self.expected
            self.pending = b'BOOT' + struct.pack('<I', self.next)
        else:
            raise AssertionError(command)
        return len(packet)


peer = Peer()
endpoint = module.Rescue(peer)
endpoint.ping()
assert peer.pings == 2
payload = bytes(range(256)) * 35
assert endpoint.upload(payload) == len(payload)
assert bytes(peer.data) == payload
endpoint.boot(len(payload))
endpoint.cancel()
assert peer.next == 0
print('UART host startup text/lost ACK/idempotent retry: PASS')
