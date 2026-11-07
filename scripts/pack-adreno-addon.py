#!/usr/bin/env python3
"""Pack the real Adreno Metal plugin as a GPU addon with a 16-character bundle directory name.

make-gpu-addon.py hardcodes addons/NativeAdrenoMetal.bundle; the kernel's _offline-accelerator-plugin
argument copies at most 16 characters, so the staging directory name must be shortened to match what
MetalPluginName can carry ("NativeAdreno"). The bundle's CFBundleExecutable stays NativeAdrenoMetal.
"""
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / 'build/adreno-metal-plugin'
BUNDLE = PLUGIN / 'NativeAdrenoMetal.bundle'
ADDON_NAME = 'NativeAdreno.bundle'


def pad(data: bytes) -> bytes:
    return data + bytes(-len(data) % 8)


def main() -> int:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    output = Path(sys.argv[1]).resolve()
    if output.exists():
        sys.exit('Refuse to overwrite ' + str(output))
    if not (BUNDLE / 'NativeAdrenoMetal').is_file():
        sys.exit('Build the plugin first: python3 scripts/build-adreno-metal-plugin.py')
    files = []
    for path in sorted(p for p in BUNDLE.rglob('*') if p.is_file()):
        mode = 0o755 if path.name in ('NativeAdrenoMetal', 'libvulkan_freedreno.dylib') else 0o644
        files.append(('addons/' + ADDON_NAME + '/' + path.relative_to(BUNDLE).as_posix(), mode, path.read_bytes()))
    files.append(('bin/adrenoprobed', 0o755, (PLUGIN / 'adrenoprobed').read_bytes()))
    for plist in sorted(PLUGIN.glob('*.plist')):
        if not plist.name.endswith('-entitlements.plist'):
            files.append(('daemons/' + plist.name, 0o644, plist.read_bytes()))
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
