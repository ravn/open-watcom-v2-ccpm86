#!/usr/bin/env python3
"""verify_farheap_dump.py -- INDEPENDENT far-heap content oracle (variable size).

farheap_smalltest.c grabs as much far heap as the loader grants -- N blocks of a
VARIABLE segment size SEG, block i filled with the ramp byte[j]=(i*97+1+j)&0xFF --
and verifies the round-trip ITSELF. That self-check both writes and reads via the
same guest path, so it is an EQUIVALENCE oracle, not a correctness one (AGENTS.md
"ask where expected comes from").

This script closes that loop from OUTSIDE the guest: it reads the physical-RAM
snapshot that cpm86run_unicorn.py --dump / the MAME farheap_done_dump.lua wrote
AFTER the program ended (any size, up to a megabyte or more -- it just uses the
whole file) and confirms the far blocks are really present in RAM with the exact
pattern -- a source that does NOT share the guest's write/read path.

METHOD: each block is a strictly-incrementing (mod 256) ramp of >=SEG bytes.
Scan for maximal incrementing-ramp runs of length >=SEG.
  --count N  : verify the N expected block phases (i*97+1)&0xFF are all present
               and >= N*SEG ramped bytes exist (cross-check the guest's own n).
  (no count) : auto -- just report every ramp-run found and PASS if >=1.

Usage: verify_farheap_dump.py DUMP [--seg BYTES] [--count N]
"""
import sys

DEFAULT_SEG = 16384


def find_runs(data, seg):
    """All maximal incrementing-mod-256 runs of length >= seg -> (off,len,start)."""
    runs = []
    n = len(data)
    i = 0
    while i < n:
        j = i
        while j + 1 < n and data[j + 1] == (data[j] + 1) & 0xFF:
            j += 1
        length = j - i + 1
        if length >= seg:
            runs.append((i, length, data[i]))
        i = j + 1
    return runs


def main(argv):
    args = argv[1:]
    seg = DEFAULT_SEG
    count = None
    path = None
    k = 0
    while k < len(args):
        a = args[k]
        if a == "--seg":
            seg = int(args[k + 1], 0); k += 2
        elif a == "--count":
            count = int(args[k + 1], 0); k += 2
        else:
            path = a; k += 1
    if path is None:
        print("usage: verify_farheap_dump.py DUMP [--seg BYTES] [--count N]",
              file=sys.stderr)
        return 2

    data = open(path, "rb").read()
    runs = find_runs(data, seg)
    total_ramp = sum(L for (_o, L, _s) in runs)
    # A block of seg is one ramp-run; a longer run holds floor(L/seg) blocks.
    nblocks = sum(L // seg for (_o, L, _s) in runs)

    print("far-heap RAM-dump scan (%d bytes read): seg=%d -> %d ramp-run(s), "
          "%d block(s), %d B ramped total"
          % (len(data), seg, len(runs), nblocks, total_ramp))

    if count is None:
        # Auto mode: analyse whatever we got.
        for (off, L, s) in runs:
            print("  run @0x%06X seg~0x%04X phase=0x%02X len=%d (%d block[s])"
                  % (off, off >> 4, s, L, L // seg))
        if nblocks >= 1:
            print("INDEPENDENT PASS (auto): %d far block(s) / %d KB present in RAM"
                  % (nblocks, total_ramp >> 10))
            return 0
        print("INDEPENDENT FAIL: no ramp-run >= seg=%d found" % seg)
        return 1

    # Cross-check the guest's reported count: every expected phase must be
    # covered by some run window, and >= count*seg ramped bytes must exist.
    def covered(vi):
        for (off, L, s) in runs:
            span = L - seg
            if span >= 255 or ((vi - s) & 0xFF) <= span:
                return off, s, L
        return None

    bad = 0
    for b in range(count):
        vi = (b * 97 + 1) & 0xFF
        hit = covered(vi)
        if hit:
            off, s, L = hit
            print("  block %d phase=0x%02X : FOUND (run @0x%06X seg~0x%04X)"
                  % (b, vi, off, off >> 4))
        else:
            print("  block %d phase=0x%02X : MISSING from RAM" % (b, vi))
            bad += 1

    if bad == 0 and total_ramp >= count * seg:
        print("INDEPENDENT PASS: all %d far block(s) (%d KB) present in RAM with "
              "correct pattern" % (count, (count * seg) >> 10))
        return 0
    print("INDEPENDENT FAIL: %d block(s) missing / only %d B ramped (< %d)"
          % (bad, total_ramp, count * seg))
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
