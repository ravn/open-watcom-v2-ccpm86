#!/usr/bin/env python3
"""cmd_check.py -- static linter for CP/M-86 ``.CMD`` load files.

Purpose: catch, WITHOUT running the program, the two far-heap defects that the
runtime oracle ``watcom-cpm86-libc/test/compact_farheap_test.c`` found the hard
way (2026-08-19). A ``.CMD`` is a self-describing image: its 128-byte header
carries up to eight 9-byte GROUP DESCRIPTORS (CP/M-86 System Guide Fig 3-1,
tasks/memory/reference_cpm86_cmd_header.md) plus a load-time fixup table
(tasks/memory/reference_cpm86_p_load_fixups.md). That is enough to prove or
refute the layout invariants the far heap depends on.

Each 9-byte group descriptor:
    +0  G_TYPE   1=Code 2=Data 3=Extra 4=Stack 5..8=Aux  (0 => slot empty)
    +1  G_LENGTH paragraphs of the group actually stored in the file image
    +3  A_BASE   fixed base paragraph (0 => relocatable)
    +5  G_MIN    minimum paragraphs the loader must allocate
    +7  G_MAX    maximum paragraphs the loader may allocate
Header byte 0x7F bit7 => fixup records present; word 0x7D..0x7E = fixup table
FILE RECORD number (byte offset = value * 128); each record is 4 bytes
(fix_grp, fix_para lo/hi, fix_offs), table terminated by a record whose byte 0
is zero.

THE TWO DEFECTS THIS GUARDS AGAINST
-----------------------------------
F1  FAR-HEAP / FAR-DATA OVERLAP (the primary bug). Our compact-model design
    coalesces a program's far data AND the ``OPTION FARHEAP`` reservation into
    the SAME type-3 EXTRA group, and port/farheap.c's __AllocSeg carves heap
    slabs starting at EXTRA paragraph 0. If the program has ANY loaded far data
    (G_LENGTH>0, or the loader reserved it via G_MIN>1) the very first far
    malloc() hands out memory ON TOP of that far data -> silent corruption.
    A safe .CMD (under the current offset-0 carver) therefore has, for a
    type-3 group that also carries a heap (G_MAX>G_MIN): G_LENGTH==0 AND
    G_MIN<=1. With --heap-starts-at-min (a future farheap.c that begins carving
    at G_MIN) the weaker, still-sound invariant G_MIN>=G_LENGTH is checked
    instead.

F2  BASE-PAGE CLOBBER via a mis-placed near arena. The clib near-heap arena
    (port/lowlevel.c ``wc_arena``) MUST live in DGROUP; in compact model a
    missing ``__near`` sends it to a FAR_DATA segment, ``_curbrk`` becomes the
    near offset 0, and the near heap writes over the CP/M-86 base page. That is
    a *symbol placement* fact not visible in the .CMD header, so it is checked
    from the linker .map when one is supplied via --map: any lowlevel*/wc_arena
    contribution in a FAR_DATA/AUTO segment is flagged.

Exit status: 0 = all checks pass, 1 = at least one FAIL, 2 = usage/parse error.
"""

import argparse
import os
import re
import struct
import sys

GTYPE_NAME = {1: "Code", 2: "Data", 3: "Extra", 4: "Stack",
              5: "Aux1", 6: "Aux2", 7: "Aux3", 8: "Aux4"}


class Group:
    __slots__ = ("slot", "type", "length", "base", "gmin", "gmax")

    def __init__(self, slot, t, length, base, gmin, gmax):
        self.slot, self.type = slot, t
        self.length, self.base, self.gmin, self.gmax = length, base, gmin, gmax

    @property
    def name(self):
        return GTYPE_NAME.get(self.type, "?%d" % self.type)


def parse_header(data):
    """Return (groups, fixups_present, fixrec) from the 128-byte header."""
    if len(data) < 128:
        raise ValueError("file shorter than a 128-byte .CMD header (%d bytes)"
                         % len(data))
    groups = []
    for slot in range(8):
        off = slot * 9
        t = data[off]
        if t == 0:
            continue
        length, base, gmin, gmax = struct.unpack_from("<HHHH", data, off + 1)
        groups.append(Group(slot, t, length, base, gmin, gmax))
    fixrec = struct.unpack_from("<H", data, 0x7D)[0]
    fixups_present = bool(data[0x7F] & 0x80)
    return groups, fixups_present, fixrec


def parse_fixups(data, fixrec):
    """Yield (fix_grp, fix_para, fix_offs) records until the terminator."""
    start = fixrec * 128
    recs = []
    off = start
    while off + 4 <= len(data):
        fix_grp = data[off]
        if fix_grp == 0:            # byte0==0 ends the table (loader semantics)
            break
        fix_para = data[off + 1] | (data[off + 2] << 8)
        fix_offs = data[off + 3]
        recs.append((fix_grp, fix_para, fix_offs))
        off += 4
    return recs, start


class Report:
    def __init__(self):
        self.fails = []
        self.warns = []
        self.notes = []

    def fail(self, code, msg):
        self.fails.append((code, msg))

    def warn(self, code, msg):
        self.warns.append((code, msg))

    def note(self, msg):
        self.notes.append(msg)


def check_groups(groups, rep, heap_starts_at_min):
    seen = {}
    for g in groups:
        if g.type in seen:
            rep.fail("F3", "duplicate group type %s (slots %d and %d)"
                     % (g.name, seen[g.type], g.slot))
        seen[g.type] = g.slot
        if g.type not in GTYPE_NAME:
            rep.fail("F3", "slot %d has unknown group type %d" % (g.slot, g.type))
        if g.gmax and g.gmin > g.gmax:
            rep.fail("F3", "%s group: G_MIN(%d) > G_MAX(%d)"
                     % (g.name, g.gmin, g.gmax))
        if g.gmax and g.length > g.gmax:
            rep.fail("F3", "%s group: G_LENGTH(%d) > G_MAX(%d)"
                     % (g.name, g.length, g.gmax))

    data_g = seen_group(groups, 2)
    if data_g is not None:
        alloc = (data_g.gmax or data_g.gmin or data_g.length)
        if alloc * 16 > 0x10000:
            rep.fail("F2", "DGROUP (Data) allocates %d paras = %d bytes > 64 KB"
                     % (alloc, alloc * 16))
        else:
            rep.note("DGROUP %d paras (%d bytes), within the 64 KB ceiling"
                     % (alloc, alloc * 16))

    extra = seen_group(groups, 3)
    if extra is None:
        rep.note("no Extra group -> no far heap in this image")
        return
    has_heap = extra.gmax > extra.gmin
    far_data_paras = max(extra.length, extra.gmin)   # loaded image or reserved min
    rep.note("Extra group: G_LENGTH=%d G_MIN=%d G_MAX=%d paras (far data ~%d p, "
             "heap %s)" % (extra.length, extra.gmin, extra.gmax, far_data_paras,
                           "yes" if has_heap else "no"))
    if not has_heap:
        return
    if heap_starts_at_min:
        # future farheap.c carves from G_MIN: safe iff no image spills past min
        if extra.length > extra.gmin:
            rep.fail("F1", "far heap starts at G_MIN(%d) but loaded far data is "
                     "%d paras -> heap overlaps far data by %d paras"
                     % (extra.gmin, extra.length, extra.length - extra.gmin))
        else:
            rep.note("far heap starts at G_MIN(%d) >= far data(%d) -- no overlap"
                     % (extra.gmin, extra.length))
    else:
        # current farheap.c carves from paragraph 0
        if extra.length > 0 or extra.gmin > 1:
            rep.fail("F1", "Extra group carries loaded far data "
                     "(G_LENGTH=%d, G_MIN=%d) AND a far-heap reservation "
                     "(G_MAX=%d); port/farheap.c carves slabs from Extra "
                     "paragraph 0, so the first far malloc() overwrites the far "
                     "data. Separate the heap from far data (heap in its own "
                     "group, or carve from G_MIN)."
                     % (extra.length, extra.gmin, extra.gmax))
        else:
            rep.note("Extra group is heap-only (no loaded far data) -- safe")


def seen_group(groups, t):
    for g in groups:
        if g.type == t:
            return g
    return None


def check_fixups(data, fixups_present, fixrec, groups, rep):
    if not fixups_present:
        rep.note("no load-time fixups (0x7F bit7 clear)")
        return
    if fixrec * 128 >= len(data):
        rep.fail("F4", "fixup table record %d (offset %d) is past EOF (%d)"
                 % (fixrec, fixrec * 128, len(data)))
        return
    recs, start = parse_fixups(data, fixrec)
    rep.note("%d load-time fixup record(s) at file offset %d (record %d)"
             % (len(recs), start, fixrec))
    valid_types = {g.type for g in groups}
    for i, (fix_grp, fix_para, fix_offs) in enumerate(recs):
        loc_t = (fix_grp >> 4) & 0xF
        tgt_t = fix_grp & 0xF
        if loc_t not in valid_types:
            rep.fail("F4", "fixup #%d: location group type %d not in image"
                     % (i, loc_t))
        if tgt_t not in valid_types:
            rep.fail("F4", "fixup #%d: target group type %d not in image"
                     % (i, tgt_t))
        if fix_offs > 15:
            rep.fail("F4", "fixup #%d: fix_offs=%d exceeds 15" % (i, fix_offs))
        loc = seen_group(groups, loc_t)
        if loc is not None and loc.gmax and fix_para > loc.gmax:
            rep.warn("F4", "fixup #%d: fix_para=%d beyond %s G_MAX=%d"
                     % (i, fix_para, loc.name, loc.gmax))


FARSEG_RE = re.compile(
    r'^\s*(\S+)\s+(\S+)\s+(FAR_DATA|AUTO)\s+.*?\b(?:0x)?[0-9A-Fa-f]+\b', )


def check_map(map_path, rep):
    """Flag clib near-arena (lowlevel*/wc_arena) landing in FAR_DATA (F2)."""
    try:
        with open(map_path, "r", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as e:
        rep.warn("F2", "could not read map %r: %s" % (map_path, e))
        return
    flagged = 0
    for ln in lines:
        low = ln.lower()
        if "far_data" not in low:
            continue
        if "lowlevel" in low or "wc_arena" in low:
            rep.fail("F2", "near-heap arena appears in FAR_DATA: %s"
                     % ln.strip())
            flagged += 1
    if not flagged:
        rep.note("map check: no lowlevel/wc_arena contribution in FAR_DATA")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Static CP/M-86 .CMD far-heap linter")
    ap.add_argument("cmd", help="path to the .CMD file")
    ap.add_argument("--map", help="optional linker .map for the near-arena (F2) check")
    ap.add_argument("--heap-starts-at-min", action="store_true",
                    help="assume farheap.c carves from G_MIN (relaxed F1 rule)")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="print only failures")
    args = ap.parse_args(argv)

    try:
        with open(args.cmd, "rb") as fh:
            data = fh.read()
        groups, fixups_present, fixrec = parse_header(data)
    except (OSError, ValueError) as e:
        print("cmd_check: %s: %s" % (args.cmd, e), file=sys.stderr)
        return 2

    rep = Report()
    check_groups(groups, rep, args.heap_starts_at_min)
    check_fixups(data, fixups_present, fixrec, groups, rep)
    if args.map:
        check_map(args.map, rep)

    name = os.path.basename(args.cmd)
    if not args.quiet:
        for m in rep.notes:
            print("  . %s" % m)
        for c, m in rep.warns:
            print("  ! [%s] %s" % (c, m))
    for c, m in rep.fails:
        print("  X [%s] %s" % (c, m))

    if rep.fails:
        print("cmd_check: %s FAIL (%d issue%s)"
              % (name, len(rep.fails), "" if len(rep.fails) == 1 else "s"))
        return 1
    print("cmd_check: %s OK%s"
          % (name, "" if not rep.warns else " (%d warning%s)"
             % (len(rep.warns), "" if len(rep.warns) == 1 else "s")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
