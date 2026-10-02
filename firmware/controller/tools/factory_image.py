#!/usr/bin/env python3
"""Build the image a blank board is first programmed with over SWD.

controller-factory.bin is the whole start of flash as one file, to be written
at 0x10000000: the partition table, then the firmware where image slot A
begins. Writing controller.elf (or .bin) there instead puts the firmware on
top of the partition table's place, and the board then runs unpartitioned,
with no slot to update into.
"""

import argparse
import json
import pathlib
import sys

SECTOR = 4096


def size(text: str) -> int:
    """A partition JSON size or offset: 8K, 4096K, 1M, 0x2000, 8192."""
    text = text.strip()
    scale = {"K": 1024, "M": 1024 * 1024}.get(text[-1:].upper())
    return int(text[:-1], 0) * scale if scale else int(text, 0)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--layout", required=True, type=pathlib.Path, help="the partition table JSON")
    ap.add_argument("--table", required=True, type=pathlib.Path, help="partition_table.bin made from it")
    ap.add_argument("--image", required=True, type=pathlib.Path, help="controller.bin")
    ap.add_argument("--out", required=True, type=pathlib.Path)
    args = ap.parse_args()

    slots = [p for p in json.loads(args.layout.read_text())["partitions"] if p.get("name") == "A"]
    if len(slots) != 1 or "start" not in slots[0]:
        sys.exit(f"{args.layout}: want one partition named A with a start")
    start, room = size(slots[0]["start"]), size(slots[0]["size"])

    table = args.table.read_bytes()
    image = args.image.read_bytes()
    if start % SECTOR or not 0 < len(table) <= min(start, SECTOR):
        sys.exit(f"partition table ({len(table)} bytes) does not fit before slot A at {start:#x}")
    if len(image) > room:
        sys.exit(f"image ({len(image)} bytes) is larger than slot A ({room} bytes)")

    args.out.write_bytes(table + b"\xff" * (start - len(table)) + image)
    print(f"{args.out}: partition table, then {len(image)} bytes of firmware in slot A at {start:#x}")


if __name__ == "__main__":
    main()
