#!/usr/bin/env python3
"""Add owner CDHashes to an existing boot trust bundle without the IPSW.

  add-owner-trust-hash.py INPUT.bundle OUTPUT.bundle CDHASH [CDHASH...]

The input is the trust bundle already carried by the boot payload (owner module
plus the two unchanged Apple modules).  The owner module is rebuilt with the
given hashes while every other module is copied byte for byte, because the
IPSW needed by build-original-system-trust.py is not available here.
"""
import hashlib
import struct
import sys
import uuid
from pathlib import Path


def unpack_modules(data: bytes):
    count, = struct.unpack_from('<I', data)
    if not 0 < count <= 128:
        raise SystemExit('bad module count %d' % count)
    starts = list(struct.unpack_from('<%dI' % count, data, 4))
    ends = starts[1:] + [len(data)]
    modules = []
    for start, end in zip(starts, ends):
        if start < 4 + 4 * count or start & 3 or end > len(data) or end - start < 24:
            raise SystemExit('bad module bounds %d..%d' % (start, end))
        modules.append(data[start:end])
    return modules


def pack_modules(modules):
    starts, cursor = [], 16
    for module in modules:
        starts.append(cursor)
        cursor += len(module)
    return struct.pack('<%dI' % (len(modules) + 1), len(modules), *starts) + b''.join(modules)


def valid_like_the_loader(data: bytes) -> bool:
    count, = struct.unpack_from('<I', data)
    if not 0 < count <= 128:
        return False
    for index in range(count):
        start, = struct.unpack_from('<I', data, 4 + 4 * index)
        end = struct.unpack_from('<I', data, 8 + 4 * index)[0] if index + 1 < count else len(data)
        if start < 4 + 4 * count or start & 3 or end > len(data) or end - start < 24:
            return False
        if struct.unpack_from('<I', data, start)[0] != 2:
            return False
        entries, = struct.unpack_from('<I', data, start + 20)
        if end - start != 24 + entries * 24:
            return False
        for entry in range(entries):
            at = start + 24 + entry * 24
            if not 1 <= data[at + 20] <= 4:
                return False
            if entry and data[at - 24:at - 4] >= data[at:at + 20]:
                return False
    return True


def main() -> int:
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    source, output = Path(sys.argv[1]), Path(sys.argv[2])
    hashes = sorted(bytes.fromhex(h) for h in sys.argv[3:])
    if any(len(h) != 20 for h in hashes) or len(set(hashes)) != len(hashes):
        sys.exit('CDHashes must be unique 20-byte hex values')
    modules = unpack_modules(source.read_bytes())
    if len(modules) >= 3:
        originals = modules[1:]          # owner module already present
    elif len(modules) == 2:
        originals = modules              # only the two original Apple modules
    else:
        sys.exit('expected two original modules, got %d' % len(modules))
    identifier = uuid.uuid5(uuid.NAMESPACE_URL, 'nil-metal:' + ''.join(h.hex() for h in hashes))
    own = struct.pack('<I16sI', 2, identifier.bytes, len(hashes)) + \
        b''.join(h + bytes([2, 0, 0, 0]) for h in hashes)
    bundle = pack_modules([own, *originals])
    if not valid_like_the_loader(bundle):
        sys.exit('the new bundle would be refused by the boot loader')
    output.write_bytes(bundle)
    print('PASS: %s (%d bytes, sha256 %s, owner hashes %s)' % (
        output, len(bundle), hashlib.sha256(bundle).hexdigest()[:16],
        ','.join(h.hex()[:8] for h in hashes)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
