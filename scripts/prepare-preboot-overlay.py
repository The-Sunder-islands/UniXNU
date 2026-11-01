#!/usr/bin/env python3
"""Stage the Cryptex metadata a booted SystemOS expects under /private/preboot.

This does not create or modify an APFS image. It extracts only the small
BuildManifest objects (root hash and trust cache) for the matching iPad7,1
identity and writes the directory layout consumed by the imageboot/Preboot
path. The SystemOS image itself stays at its original location on the OS
volume; os.dmg is a symlink, not a copy.

The output tree is intended to be placed on a writable Preboot volume:
    <output>/cryptex1/current/os.dmg
    <output>/cryptex1/current/os.dmg.root_hash
    <output>/cryptex1/current/os.dmg.trustcache
    <output>/Cryptexes/OS

For a remote IPSW, only the zip directory and the two small Cryptex objects are
read with HTTP range requests; the multi-gigabyte OS/SystemOS images are never
downloaded.
"""
import argparse
import hashlib
import json
import os
import plistlib
import struct
import sys
import uuid
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from unixnu.ipsw import RangeFile

DEVICE_CLASS = 'j120ap'
RESTORE_BEHAVIOR = 'Erase'
SYSTEMOS_UUID = '8846FE78-2910-4297-AF97-D59BB6F8FC16'

COMPONENTS = {
    'os': {
        'image': 'Cryptex1,SystemOS',
        'root_hash': 'Cryptex1,SystemVolume',
        'trustcache': 'Cryptex1,SystemTrustCache',
        'target': '/usr/standalone/firmware/Cryptex1/SystemOS.dmg',
    },
    'app': {
        'image': 'Cryptex1,AppOS',
        'root_hash': 'Cryptex1,AppVolume',
        'trustcache': 'Cryptex1,AppTrustCache',
        'target': '/usr/standalone/firmware/Cryptex1/AppOS.dmg',
    },
}


def digest_matches(data, digest):
    algorithm = {20: 'sha1', 32: 'sha256', 48: 'sha384'}.get(len(digest))
    return bool(algorithm) and hashlib.new(algorithm, data).digest() == digest


def trust_cache_info(data):
    if data.startswith(b'0'):
        # IM4P wraps the raw trust-cache module in an ASN.1 sequence. The
        # BuildManifest digest already authenticates the bytes; locate the raw
        # module by its known (version, uuid, count) grammar.
        if len(data) < 4 or data[:2] != b'0\x82':
            raise ValueError('unsupported trust-cache IM4P encoding')
        for pos in range(4, len(data) - 24):
            version, _, count = struct.unpack_from('<I16sI', data, pos)
            width = {0: 20, 1: 22, 2: 24}.get(version)
            if width and count and 24 + count * width == len(data) - pos:
                data = data[pos:]
                break
        else:
            raise ValueError('trust-cache module not found in IM4P')
    version, identifier, count = struct.unpack_from('<I16sI', data, 0)
    width = {0: 20, 1: 22, 2: 24}.get(version)
    if width is None or 24 + count * width != len(data):
        raise ValueError('unsupported trust-cache module')
    return {
        'version': version,
        'uuid': str(uuid.UUID(bytes=identifier)),
        'entries': count,
        'bytes': len(data),
        'sha256': hashlib.sha256(data).hexdigest(),
    }


def build_identity(manifest, device_class):
    for identity in manifest['BuildIdentities']:
        info = identity['Info']
        if info.get('DeviceClass') == device_class and info.get('RestoreBehavior') == RESTORE_BEHAVIOR:
            return identity
    raise SystemExit(f'no {RESTORE_BEHAVIOR} identity for {device_class}')


def stage(archive, manifest, identity, output, device_class, systemos_uuid):
    current = output / 'cryptex1' / 'current'
    current.mkdir(parents=True)
    records = {}
    for kind, spec in COMPONENTS.items():
        for key in ('image', 'root_hash', 'trustcache'):
            component = identity['Manifest'][spec[key]]
            data = archive.read(component['Info']['Path'])
            if not digest_matches(data, component.get('Digest', b'')):
                raise SystemExit(f'BuildManifest digest mismatch: {component["Info"]["Path"]}')
            if key != 'image':
                suffix = 'root_hash' if key == 'root_hash' else 'trustcache'
                (current / (kind + '.dmg.' + suffix)).write_bytes(data)
            record = {
                'path': component['Info']['Path'],
                'bytes': len(data),
                'sha256': hashlib.sha256(data).hexdigest(),
            }
            if key == 'trustcache':
                record['module'] = trust_cache_info(data)
            records[kind + '.' + key] = record
        (current / (kind + '.dmg')).symlink_to(spec['target'])
        (output / 'Cryptexes' / ('OS' if kind == 'os' else 'App')).mkdir(parents=True)
        records[kind + '.dmg'] = {'symlink': spec['target']}

    os.chmod(output / 'cryptex1', 0o755)
    result = {
        'product_version': manifest.get('ProductVersion'),
        'build': manifest.get('ProductBuildVersion'),
        'device_class': device_class,
        'restore_behavior': RESTORE_BEHAVIOR,
        'systemos_volume_uuid': systemos_uuid,
        'objects': records,
        'personalized_manifest_provided': False,
        'full_image_created': False,
        'ready_for_native_graft': False,
        'requires_same_volume_dmg_for_graft': True,
    }
    (output / 'manifest.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return result


def prepare(ipsw, output, device_class, systemos_uuid):
    output = output.resolve()
    if output.exists():
        raise SystemExit(f'refusing to overwrite existing {output}')
    output.mkdir(parents=True)
    remote = str(ipsw).startswith(('http://', 'https://'))
    source = RangeFile(str(ipsw)) if remote else open(ipsw, 'rb')
    try:
        with zipfile.ZipFile(source) as archive:
            manifest = plistlib.loads(archive.read('BuildManifest.plist'))
            identity = build_identity(manifest, device_class)
            return stage(archive, manifest, identity, output, device_class, systemos_uuid)
    finally:
        source.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ipsw', required=True, help='local IPSW path or https URL')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--device-class', default=DEVICE_CLASS)
    parser.add_argument('--systemos-uuid', default=SYSTEMOS_UUID)
    args = parser.parse_args()
    prepare(args.ipsw, args.output, args.device_class, args.systemos_uuid)


if __name__ == '__main__':
    main()
