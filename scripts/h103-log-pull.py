#!/usr/bin/env python3
"""Pull and decode the H103 persistent kernel log from TWRP.

The H103de NativeUfsProbe writer stores an SHLOG001 v3 header at block 0 and
the kernel message buffer at blocks 1..256 in the last 32 MiB of the iOS
``userdata_ios`` GPT partition. This tool only reads that window; it never
writes the partition.

Typical use (phone already booted into TWRP, adb connected):

    python scripts/h103-log-pull.py --adb adb

or parse an already pulled 32 MiB window:

    python scripts/h103-log-pull.py --input h103log.bin
"""
from __future__ import annotations

import argparse
import datetime as _dt
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import time

BLOCK = 4096
WINDOW_BLOCKS = 8192
MAGIC = b"SHLOG001"
DEFAULT_PART = "/dev/block/by-name/userdata_ios"
ALT_PART = "/dev/block/bootdevice/by-name/userdata_ios"
HDR = struct.Struct("<8sIIQQ" + "I" * 14 + "32s" + "I" * 5)


def resolve_adb(explicit: str | None) -> str:
    candidates = [explicit, os.environ.get("ADB"), shutil.which("adb")]
    candidates += [
        r"D:\platform-tools\adb.exe",
        r"C:\platform-tools\adb.exe",
        os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk",
                     "platform-tools", "adb.exe"),
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    raise SystemExit("adb not found; pass --adb <path>")


def adb(adb_path: str, serial: str | None, *args: str, timeout: int = 120) -> str:
    cmd = [adb_path] + (["-s", serial] if serial else []) + list(args)
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if result.returncode != 0:
        raise SystemExit(f"adb failed ({result.returncode}): {' '.join(cmd)}\n"
                         f"{result.stdout}{result.stderr}")
    return result.stdout


def wait_adb(adb_path: str, serial: str | None, timeout: float = 30.0) -> None:
    deadline = time.monotonic() + timeout
    while True:
        out = adb(adb_path, serial, "devices")
        states = {parts[1] for parts in
                  (line.split() for line in out.splitlines()[1:]) if len(parts) > 1}
        if states & {"device", "recovery"}:
            return
        if time.monotonic() > deadline:
            raise SystemExit(f"no adb device (states={sorted(states) or ['none']})")
        time.sleep(1)


def pick_partition(adb_path: str, serial: str | None, explicit: str | None) -> str:
    candidates = [explicit, DEFAULT_PART, ALT_PART]
    for part in candidates:
        if not part:
            continue
        try:
            adb(adb_path, serial, "shell", f"test -b {part}")
            return part
        except SystemExit:
            continue
    raise SystemExit("userdata_ios block device not found in TWRP")


def pull_window(adb_path: str, serial: str | None, part: str,
                 output: pathlib.Path) -> int:
    size_text = adb(adb_path, serial, "shell", f"blockdev --getsize64 {part}").strip()
    try:
        size = int(size_text)
    except ValueError:
        raise SystemExit(f"cannot read partition size: {size_text!r}")
    blocks = size // BLOCK
    if blocks < WINDOW_BLOCKS:
        raise SystemExit(f"partition too small: {size} bytes")
    skip = blocks - WINDOW_BLOCKS
    remote = "/tmp/h103log.bin"
    # The UFS window may have been pulled before; TWRP can serve stale block
    # cache pages even after the kernel rewrote the late snapshot.
    try:
        adb(adb_path, serial, "shell", f"blockdev --flushbufs {part}", timeout=30)
    except SystemExit:
        pass
    dd_cmd = (f"dd if={part} of={remote} bs={BLOCK} skip={skip} "
              f"count={WINDOW_BLOCKS}")
    try:
        adb(adb_path, serial, "shell", dd_cmd + " iflag=direct", timeout=300)
    except SystemExit:
        adb(adb_path, serial, "shell", dd_cmd, timeout=300)
    adb(adb_path, serial, "pull", remote, str(output), timeout=300)
    adb(adb_path, serial, "shell", f"rm -f {remote}")
    return size


def decode_test(data: bytes) -> str:
    text = data[:256].split(b"\0", 1)[0].decode("ascii", "replace")
    return text.replace("\r", " ").replace("\n", " ")


def decode_header(data: bytes) -> dict:
    if len(data) < HDR.size:
        raise SystemExit("window is shorter than the header")
    values = HDR.unpack_from(data, 0)
    fields = (
        "magic", "version", "header_bytes", "boot_id", "first_lba",
        "window_blocks", "msg_first", "msg_blocks", "msg_bytes",
        "slot_first", "slot_blocks", "slot_count", "slots_written",
        "uptime_s", "rounds", "write_errors", "gpt_open", "gpt_last",
        "state", "tag", "diag_cmds", "diag_last_lba",
        "diag_last_bytes", "diag_flags", "diag_loops",
    )
    out = dict(zip(fields, values))
    if out["magic"] != MAGIC:
        raise SystemExit("no SHLOG001 header (writer did not run or window is empty)")
    if out["version"] != 3:
        raise SystemExit(f"unexpected SHLOG001 version {out['version']}")
    out["tag"] = out["tag"].split(b"\0", 1)[0].decode("ascii", "replace")
    return out


def write_report(header: dict, text: str, path: pathlib.Path) -> None:
    lines = [
        "H103 persistent kernel log",
        _dt.datetime.now().isoformat(timespec="seconds"),
        "=" * 72,
    ]
    lines += [f"{key}: {value}" for key, value in header.items()]
    lines += ["=" * 72, text]
    path.write_text("\n".join(lines), encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--adb", help="adb executable")
    parser.add_argument("-s", "--serial", help="adb serial when several devices are attached")
    parser.add_argument("--partition", help="userdata_ios block device path")
    parser.add_argument("--input", type=pathlib.Path,
                        help="parse an existing 32 MiB window instead of pulling")
    parser.add_argument("--output", type=pathlib.Path,
                        help="write the report here (default: h103-log-<time>.txt)")
    parser.add_argument("--keep-window", type=pathlib.Path,
                        help="keep the pulled 32 MiB window at this path")
    args = parser.parse_args(argv)

    if args.input:
        window = args.input.read_bytes()
        size = None
    else:
        adb_path = resolve_adb(args.adb)
        wait_adb(adb_path, args.serial)
        part = pick_partition(adb_path, args.serial, args.partition)
        window_path = args.keep_window or pathlib.Path("h103log.bin")
        size = pull_window(adb_path, args.serial, part, window_path)
        window = window_path.read_bytes()

    if window[:8] != MAGIC:
        test_at = window.find(b"H103TEST")
        if test_at >= 0:
            text = decode_test(window[test_at:])
            report = args.output or pathlib.Path(
                "h103-log-" + time.strftime("%Y%m%d-%H%M%S") + ".txt")
            report.write_text("H103 write self-test\n" + text + "\n", encoding="utf-8")
            print(f"H103 write self-test block present at window +{test_at}:")
            print(text)
            print(f"report: {report}")
            return 0

    header = decode_header(window)
    slot_count = header["slot_count"]
    slots_written = header["slots_written"]
    if slots_written and slot_count:
        slot = (slots_written - 1) % slot_count
        slot_blocks = header["slot_blocks"]
        slot_offset = (header["slot_first"] + slot * slot_blocks) * BLOCK
        msg_bytes = min(header["msg_bytes"], slot_blocks * BLOCK)
        body = window[slot_offset:slot_offset + msg_bytes]
        text = body.decode("utf-8", "replace").rstrip("\0 \n")
        report = args.output or pathlib.Path(
            "h103-log-" + time.strftime("%Y%m%d-%H%M%S") + ".txt")
        write_report(header, text, report)
        print(f"partition bytes: {size if size is not None else 'from file'}")
        print(f"window: first_lba={header['first_lba']} blocks={header['window_blocks']} "
              f"slot={slot} slots_written={slots_written}/{slot_count} "
              f"msg_bytes={msg_bytes} state={header['state']} tag={header['tag']}")
        print(f"report: {report}")
        print("-" * 72)
        print("\n".join(text.splitlines()[-80:]))
        return 0

    msg_bytes = min(header["msg_bytes"], header["msg_blocks"] * BLOCK)
    body = window[header["msg_first"] * BLOCK:
                   header["msg_first"] * BLOCK + msg_bytes]
    text = body.decode("utf-8", "replace").rstrip("\0 \n")
    report = args.output or pathlib.Path(
        "h103-log-" + time.strftime("%Y%m%d-%H%M%S") + ".txt")
    write_report(header, text, report)

    print(f"partition bytes: {size if size is not None else 'from file'}")
    print(f"window: first_lba={header['first_lba']} blocks={header['window_blocks']} "
          f"msg_bytes={msg_bytes} state={header['state']} tag={header['tag']}")
    print(f"report: {report}")
    print("-" * 72)
    print("\n".join(text.splitlines()[-80:]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
