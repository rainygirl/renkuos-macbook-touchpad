#!/usr/bin/env python3
"""Check the real descriptor's usages, offsets and signed relative fields."""
from pathlib import Path
import re

source = (Path(__file__).resolve().parents[1] / 'src/HIDDevice.cpp').read_text()
body = source.split('static const uint8 descriptor[] = {', 1)[1].split('};', 1)[0]
data = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]+)', body))
global_items = {}
usages = []
fields = []
bit = 0
i = 0
while i < len(data):
    prefix = data[i]
    length = (0, 1, 2, 4)[prefix & 3]
    value = int.from_bytes(data[i + 1:i + 1 + length], 'little')
    kind, tag = (prefix >> 2) & 3, prefix >> 4
    i += 1 + length
    if kind == 1:
        global_items[tag] = value
    elif kind == 2 and tag == 0:
        usages.append((global_items[0], value))
    elif kind == 0:
        if tag == 8:
            size, count = global_items[7], global_items[9]
            if not value & 1:
                assert len(usages) == count
                for usage in usages:
                    fields.append((usage, bit, size, value,
                                   global_items[1], global_items[2]))
                    bit += size
            else:
                bit += size * count
        usages = []
assert bit == 72
assert fields == [
    ((9, 1), 0, 1, 2, 0, 1),
    ((1, 0x30), 8, 16, 6, 0x8000, 0x7fff),
    ((1, 0x31), 24, 16, 6, 0x8000, 0x7fff),
    ((1, 0x38), 40, 16, 6, 0x8000, 0x7fff),
    ((12, 0x238), 56, 16, 6, 0x8000, 0x7fff),
]
print('PASS: HID descriptor button/X/Y/Wheel/AC Pan layout, signs and relative flags')
