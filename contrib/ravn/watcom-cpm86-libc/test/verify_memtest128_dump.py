#!/usr/bin/env python3
"""Independent check for the variable-size BDOS-128 memory probe."""

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("dump", type=Path)
    args = parser.parse_args()
    data = args.dump.read_bytes()
    for index, pattern in enumerate((0x31, 0x32, 0x33, 0x34)):
        run = bytes((pattern,)) * (16 * (1 << (2 * index)))
        if run not in data:
            print(f"FAIL: pattern {index} ({len(run)} bytes) missing")
            return 1
        print(f"pattern {index}: FOUND ({len(run)} bytes)")
    print("PASS: all variable BDOS-128 patterns present in RAM dump")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
