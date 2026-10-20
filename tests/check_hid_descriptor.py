"""Validate report lengths and battery semantics from the actual firmware ELF.

Run with ESP-IDF's Python (includes pyelftools):
python tests/check_hid_descriptor.py build/esp-logitech-mx-master-3-usb-dongle.elf
"""
import sys
from elftools.elf.elffile import ELFFile

with open(sys.argv[1], "rb") as stream:
    elf = ELFFile(stream)
    symbol = elf.get_section_by_name(".symtab").get_symbol_by_name("hid_report_descriptor")[0]
    section = elf.get_section(symbol["st_shndx"])
    start = symbol["st_value"] - section["sh_addr"]
    descriptor = section.data()[start:start + symbol["st_size"]]

state = {}
usages = []
collections = []
bits = {}
battery = []
pos = 0
while pos < len(descriptor):
    prefix = descriptor[pos]
    pos += 1
    assert prefix != 0xfe, "Unexpected long HID item"
    size = (0, 1, 2, 4)[prefix & 3]
    value = int.from_bytes(descriptor[pos:pos + size], "little")
    pos += size
    kind, tag = (prefix >> 2) & 3, prefix >> 4
    if kind == 1:
        state[tag] = value
    elif kind == 2 and tag == 0:
        usages.append((state.get(0), value))
    elif kind == 0:
        if tag == 10:
            collections.append((value, usages[-1] if usages else None))
        elif tag == 12:
            collections.pop()
        elif tag in (8, 9, 11):
            key = (tag, state.get(8, 0))
            length = state[7] * state[9]
            bits[key] = bits.get(key, 0) + length
            if (6, 0x20) in usages:
                battery.append((tag, state[8], state[1], state[2], length, value))
                assert (1, (1, 2)) in collections, "Battery must belong to mouse application"
        usages = []

assert not collections
assert bits[(8, 1)] == 64, "Keyboard input layout changed"
assert bits[(8, 2)] == 40, "Mouse input layout changed"
assert battery == [(11, 3, 0, 100, 8, 2)], battery
assert bits[(11, 3)] == 8, "Battery feature must have exactly one payload byte"
print("HID descriptor OK: keyboard 8 bytes, mouse 5 bytes, battery feature 1 byte (IDs excluded)")
