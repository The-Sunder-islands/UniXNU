#!/usr/bin/env python3
"""Pack the nil Metal plugin bundle into the boot payload's GPU addon file.

  make-nil-gpu-addon.py OUTPUT

Same NGPUADD2 container as scripts/make-gpu-addon.py (documented there), but it
carries only addons/NilMetal.bundle/** from build/adreno-metal-plugin, so the
kernel writes the bundle into the Data volume and the sheng-gpu-addon=1 union
mount shows it over /System/Library/Extensions.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / 'build/adreno-metal-plugin'
BUNDLE = PLUGIN / 'NilMetal.bundle'
ADDON_NAME = 'NilMetal.bundle'


def pad(data: bytes) -> bytes:
    return data + bytes(-len(data) % 8)


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    output = Path(sys.argv[1]).resolve()
    if output.exists():
        sys.exit('Refuse to overwrite ' + str(output))
    if not (BUNDLE / 'NilMetal').is_file():
        sys.exit('Build the plugin first: python3 scripts/build-nil-metal-plugin.py')
    files = []
    for path in sorted(p for p in BUNDLE.rglob('*') if p.is_file()):
        mode = 0o755 if path.name == 'NilMetal' else 0o644
        files.append(('addons/' + ADDON_NAME + '/' + path.relative_to(BUNDLE).as_posix(),
                      mode, path.read_bytes()))
    body = b''
    for name, mode, data in files:
        encoded = name.encode() + b'\0'
        if len(encoded) > 160 or '..' in name.split('/') or name.startswith('/'):
            sys.exit('Bad addon path ' + name)
        packed = zlib.compress(data, 9)
        body += struct.pack('<IIQQ', len(encoded), mode, len(data), len(packed)) + pad(encoded) + pad(packed)
    blob = b'NGPUADD2' + struct.pack('<IIQ', len(files), 0, 24 + len(body)) + body
    output.write_bytes(blob)
    for name, mode, data in files:
        print(f'{mode:o} {len(data):>10} {name}')
    print(f'{output}: {len(files)} files, {len(blob)} bytes')
    return 0


if __name__ == '__main__':
    sys.exit(main())
