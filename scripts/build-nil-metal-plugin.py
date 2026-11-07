#!/usr/bin/env python3
"""Build the minimal nil Metal plugin bundle (route B of the renderer fallback).

Output (ignored, never committed):
  build/adreno-metal-plugin/NilMetal.bundle   ad-hoc signed, principal class returns nil
  build/adreno-metal-plugin/trust.json        its CDHash, in the shape
                                              make-owner-trust-bundle.py reads

The bundle is staged into the boot payload by make-nil-gpu-addon.py and becomes
visible over /System/Library/Extensions through the kernel's sheng-gpu-addon=1
union mount.
"""
import json
import plistlib
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/adreno-metal-plugin'
BUNDLE = OUT / 'NilMetal.bundle'
SOURCE = ROOT / 'drivers/NativeMetal/NativeNilMetalPlugin.mm'

OUT.mkdir(parents=True, exist_ok=True)
if BUNDLE.exists():
    shutil.rmtree(BUNDLE)
BUNDLE.mkdir()

sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
exe = BUNDLE / 'NilMetal'
subprocess.run([
    'xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-isysroot', sdk,
    '-O2', '-g', '-Wall', '-Wextra', '-fno-objc-arc', '-bundle',
    '-framework', 'Foundation', '-framework', 'Metal', '-lobjc',
    '-o', str(exe), str(SOURCE),
], check=True)
subprocess.run(['strip', '-x', str(exe)], check=True)
# strip drops a dSYM next to the executable; it must not end up inside the
# signed bundle (nor in the packed addon).
shutil.rmtree(BUNDLE / 'NilMetal.dSYM', ignore_errors=True)

(BUNDLE / 'Info.plist').write_bytes(plistlib.dumps({
    'CFBundleIdentifier': 'com.example.nilmetal',
    'CFBundleName': 'NilMetal',
    'CFBundleExecutable': 'NilMetal',
    'CFBundlePackageType': 'BNDL',
    'CFBundleVersion': '1',
    'CFBundleShortVersionString': '1.0',
    'NSPrincipalClass': 'NativeNilMetalDevice',
    'MinimumOSVersion': '17.0',
}, fmt=plistlib.FMT_BINARY))

# The target validates code with 4 KiB pages (see build-adreno-metal-plugin.py): an ad-hoc signature
# with the default page size leaves AMFI invalidating the first page, and backboardd crashes with
# SIGSEGV before the bundle's constructor runs (H185).
subprocess.run(['codesign', '--force', '--sign', '-', '--pagesize', '4096',
                '--identifier', 'org.blu-sharky.NilMetal', str(BUNDLE)], check=True)
subprocess.run(['codesign', '--verify', '--strict', str(BUNDLE)], check=True)
details = subprocess.run(['codesign', '-d', '--verbose=4', str(BUNDLE)],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout
if not re.search(r'^Page size=4096$', details, re.M):
    raise SystemExit('the signature must use 4 KiB pages:\n' + details)
match = re.search(r'CDHash=([0-9a-f]{40})', details)
if not match:
    raise SystemExit('no CDHash in codesign output:\n' + details)

trust = {'cdhashes': [match.group(1)], 'plugin': 'NilMetal', 'purpose': 'nil Metal device'}
(OUT / 'trust.json').write_text(json.dumps(trust, indent=2) + '\n')
print('PASS: built', BUNDLE, 'cdhash', match.group(1))
