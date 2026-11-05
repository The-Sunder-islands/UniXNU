#!/usr/bin/env python3
"""Derive the IOAcceleratorES base ABI from the current prelinked kernel.

The historical scripts/data/ioaccelerator-base-abi.json was captured from a
different prelink candidate and its absolute metaclass address is not valid for
the current IOGPUFamily placement.  This tool reads the candidate kernel and
its prelink report, locates the IOGPUFamily IOAcceleratorES vtable, decodes its
getMetaClass body to recover the real metaclass address, and derives the
constructor/destructor bindings for the same candidate.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'kits/NativePrelink'))
from native_prelink import backed_offset, exports, macho

FAMILY = 'com.apple.iokit.IOGPUFamily'
INSTANCE_BYTES = 136
VIRTUAL_ENTRIES = 168


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--prelink-report', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()

    raw = args.kernel.read_bytes()
    image = macho(raw)
    plan = json.loads(args.prelink_report.read_text())
    if plan.get('candidate_sha256') != sha(raw):
        raise SystemExit('kernel does not match the prelink report candidate_sha256')

    extents = [e for e in plan['extents'] if e['module'] == FAMILY]
    text = next(e for e in extents if e['segment'] == '__TEXT_EXEC')
    code = raw[backed_offset(image['segments'], text['va'], text['size'], len(raw)):
               backed_offset(image['segments'], text['va'], text['size'], len(raw)) + text['size']]
    if sha(code) != plan['original_executable_sha256'][FAMILY]:
        raise SystemExit('IOGPUFamily __TEXT_EXEC does not match the prelink report')

    names = {name: address for address, group in exports(raw, image).items() for name in group}
    service_vtable = names.get('__ZTV9IOService')
    if not service_vtable:
        raise SystemExit('IOService vtable symbol not found')
    service_base = struct.unpack(
        f'<{VIRTUAL_ENTRIES}Q',
        raw[backed_offset(image['segments'], service_vtable + 16, VIRTUAL_ENTRIES * 8, len(raw)):
            backed_offset(image['segments'], service_vtable + 16, VIRTUAL_ENTRIES * 8, len(raw)) + VIRTUAL_ENTRIES * 8])

    def read(address: int, size: int) -> bytes:
        at = backed_offset(image['segments'], address, size, len(raw))
        return raw[at:at + size]

    def adrp(pc: int, word: int) -> int:
        imm = ((word >> 5) & 0x7ffff) * 4 + ((word >> 29) & 3)
        if imm & (1 << 20):
            imm -= 1 << 21
        return (pc & ~4095) + (imm << 12)

    def getter_target(address: int):
        a, b, c = struct.unpack('<3I', read(address, 12))
        if a & 0x9f00001f == 0x90000000 and b & 0xffc003ff == 0x91000000 and c == 0xd65f03c0:
            return adrp(address, a) + ((b >> 10) & 4095)
        return None

    found = []
    for extent in extents:
        if extent['segment'] == '__TEXT_EXEC':
            continue
        at = backed_offset(image['segments'], extent['va'], extent['size'], len(raw))
        data = raw[at:at + extent['size']]
        for offset in range(0, len(data) - VIRTUAL_ENTRIES * 8 + 1, 8):
            table = struct.unpack_from(f'<{VIRTUAL_ENTRIES}Q', data, offset)
            diff = [index for index, (old, new) in enumerate(zip(service_base, table)) if old != new]
            if diff != [0, 1, 7]:
                continue
            vtable = extent['va'] + offset
            meta = getter_target(table[7])
            if not meta:
                continue
            # The getMetaClass body must return the same metaclass address.
            if getter_target(table[7]) != meta:
                continue
            found.append((vtable, meta, table))
    if len(found) != 1:
        raise SystemExit(f'expected one IOAcceleratorES vtable, found {len(found)}')

    vtable, meta, table = found[0]
    constructors = []
    for offset in range(0, len(code) - 12, 4):
        pc = text['va'] + offset
        a, b, c = struct.unpack_from('<3I', code, offset)
        if (a & 0x9f00001f == 0x90000008 and b & 0xffc003ff == 0x91000108 and
                c == 0xf9000008 and adrp(pc, a) + ((b >> 10) & 4095) == vtable):
            start = pc - 12
            w = struct.unpack('<8I', read(start, 32))
            if not (w[0] == 0xa9bf7bfd and w[1] == 0x910003fd and
                    w[6] == 0xa8c17bfd and w[7] == 0xd65f03c0):
                continue
            constructors.append(start)
    if len(constructors) != 2:
        raise SystemExit(f'expected two IOAcceleratorES constructors, found {len(constructors)}')

    getter = table[7]
    destructor = table[0]
    report = dict(
        candidate_sha256=plan['candidate_sha256'],
        original_iogpu_text_sha256=sha(code),
        metaclass=hex(meta),
        instance_bytes=INSTANCE_BYTES,
        vptr=hex(vtable),
        virtual_entries=VIRTUAL_ENTRIES,
        operational_overrides=[],
        metadata_overrides=[0, 1, 7],
        protected_compatible_constructor_aliases=[hex(a) for a in constructors],
        nondeleting_destructor=hex(destructor),
        get_meta_class=hex(getter),
        original_entry_sha256={
            hex(address): sha(read(address, 32)) for address in (*constructors, destructor, getter)
        },
        native_iogpu_instance_created=False,
        system_device_published=False,
        system_metal=False,
        original_desktop=False,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
