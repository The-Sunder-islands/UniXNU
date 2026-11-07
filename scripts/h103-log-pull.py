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
import re
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
# H163: every slot starts with this text stamp. The 32 MiB header is written
# after its slot, so a failed header write leaves slots_written stale; without
# the stamp the merge then mixes slots written by different boots.
SLOT_STAMP = re.compile(
    r"^SHLOGS v1 boot=([0-9a-f]+) seq=(\d+) build=(\S+) uptime=(\d+) bytes=(\d+)$")


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



def _split_records(text: str):
    """Split decoded slot text into (timestamp, payload) records.

    Continuation lines without a leading "[  123.456789]:" timestamp belong to
    the previous record (the ring interleaves userland text that way).
    """
    records = []
    current_ts = None
    current = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("[") and "]: " in stripped:
            if current:
                records.append((current_ts, "\n".join(current)))
            head, _, rest = stripped.partition("]: ")
            try:
                current_ts = float(head[1:].strip())
            except ValueError:
                current_ts = None
            current = [rest]
        else:
            current.append(stripped)
    if current:
        records.append((current_ts, "\n".join(current)))
    return records


def _slot_stamp(body: bytes):
    """Decode the H163 per-slot stamp, or None for slots written before it."""
    head = body.split(b"\n", 1)[0]
    try:
        text = head.decode("ascii").strip()
    except UnicodeDecodeError:
        return None
    match = SLOT_STAMP.match(text)
    if not match:
        return None
    return {"boot": match.group(1), "seq": int(match.group(2)),
            "build": match.group(3), "uptime": int(match.group(4))}


def _slot_text(body: bytes) -> str:
    """Slot payload without the stamp line and without the NUL padding."""
    text = body.decode("utf-8", "replace")
    text = text.split("\0" * 8, 1)[0]
    first, sep, rest = text.partition("\n")
    if sep and SLOT_STAMP.match(first.strip().lstrip("\0")):
        text = rest
    return text


def _slot_bodies(window: bytes, header: dict) -> list[bytes]:
    slot_blocks = header["slot_blocks"] or 32
    slot_count = header["slot_count"] or 0
    if not slot_count:
        return []
    slot_bytes = slot_blocks * BLOCK
    bodies = []
    for index in range(slot_count):
        start = (header["slot_first"] + index * slot_blocks) * BLOCK
        bodies.append(window[start:start + slot_bytes])
    return bodies


def select_slots(window: bytes, header: dict, count: int) -> list[int]:
    """Slot indices in write order, restricted to one boot when stamped.

    The anchor is the slot the header names as newest. Slots from other boots
    (left in the ring by an earlier run) are dropped; without stamps the old
    header-relative window is used unchanged.
    """
    slot_count = header["slot_count"] or 0
    slots_written = header["slots_written"] or 0
    if not slot_count or not slots_written:
        return []
    bodies = _slot_bodies(window, header)
    stamps = [_slot_stamp(body) for body in bodies]
    anchor = (slots_written - 1) % slot_count
    if stamps[anchor]:
        boot = stamps[anchor]["boot"]
        chosen = sorted((stamps[i]["seq"], i) for i in range(slot_count)
                        if stamps[i] and stamps[i]["boot"] == boot)
        if not chosen:
            return []
        header["slot_boot"] = boot
        header["slot_build"] = stamps[anchor]["build"]
        header["slot_seq_max"] = chosen[-1][0]
        keep = max(1, min(count, len(chosen)))
        return [index for _, index in chosen[-keep:]]
    keep = max(1, min(count, slot_count, slots_written))
    return [(slots_written - 1 - offset) % slot_count
            for offset in range(keep - 1, -1, -1)]


def merge_slots(window: bytes, header: dict, count: int) -> str:
    """Reconstruct a longer history from the last `count` delta slots.

    Every slot is a tail snapshot of the kernel message ring, so consecutive
    slots overlap heavily; records are deduplicated by (timestamp, text).
    """
    slot_count = header["slot_count"] or 0
    slots_written = header["slots_written"] or 0
    if not slot_count or not slots_written:
        return ""
    bodies = _slot_bodies(window, header)
    merged = {}
    order = []
    for index in select_slots(window, header, count):
        for ts, payload in _split_records(_slot_text(bodies[index])):
            key = (ts, payload)
            if key in merged:
                continue
            merged[key] = True
            order.append((ts, payload))
    order.sort(key=lambda item: (item[0] is None, item[0] if item[0] is not None else 0.0))
    return "\n".join(f"[{ts:>13.6f}]: {payload}" if ts is not None else payload
                      for ts, payload in order)

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
    parser.add_argument("--slots", type=int, default=1,
                        help="merge the last N delta slots (default 1; the whole ring is "
                             "already inside the pulled window)")
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
                "log-" + time.strftime("%Y%m%d-%H%M%S") + ".txt")
            report.write_text("H103 write self-test\n" + text + "\n", encoding="utf-8")
            print(f"H103 write self-test block present at window +{test_at}:")
            print(text)
            print(f"report: {report}")
            return 0

    header = decode_header(window)
    slot_count = header["slot_count"]
    slots_written = header["slots_written"]
    if slots_written and slot_count:
        order = select_slots(window, header, 1)
        slot = order[-1] if order else (slots_written - 1) % slot_count
        slot_blocks = header["slot_blocks"]
        slot_offset = (header["slot_first"] + slot * slot_blocks) * BLOCK
        msg_bytes = min(header["msg_bytes"], slot_blocks * BLOCK)
        body = window[slot_offset:slot_offset + slot_blocks * BLOCK]
        text = _slot_text(body).rstrip("\0 \n")
        if args.slots > 1:
            merged = merge_slots(window, header, args.slots)
            if merged:
                text = merged
        report = args.output or pathlib.Path(
            "log-" + time.strftime("%Y%m%d-%H%M%S") + ".txt")
        write_report(header, text, report)
        print(f"partition bytes: {size if size is not None else 'from file'}")
        print(f"window: first_lba={header['first_lba']} blocks={header['window_blocks']} "
              f"slot={slot} slots_written={slots_written}/{slot_count} "
              f"msg_bytes={msg_bytes} merged_slots={args.slots} "
              f"state={header['state']} tag={header['tag']}")
        print(f"report: {report}")
        print("-" * 72)
        print("\n".join(text.splitlines()[-80:]))
        return 0

    msg_bytes = min(header["msg_bytes"], header["msg_blocks"] * BLOCK)
    body = window[header["msg_first"] * BLOCK:
                   header["msg_first"] * BLOCK + msg_bytes]
    text = body.decode("utf-8", "replace").rstrip("\0 \n")
    report = args.output or pathlib.Path(
        "log-" + time.strftime("%Y%m%d-%H%M%S") + ".txt")
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
