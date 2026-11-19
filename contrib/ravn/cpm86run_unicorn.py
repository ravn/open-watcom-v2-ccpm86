#!/usr/bin/env python3
"""Independent CP/M-86 .CMD runner using Unicorn Engine (QEMU's CPU core).

Unlike cpm86run.py (my own hand-written 8086 decoder), here the actual 8086
instruction execution is done by Unicorn/QEMU -- code I did not write -- so a
correct result is independent confirmation that the .CMD's machine code and
entry point are right. Only the CP/M-86 BDOS layer (INT 0E0h) is emulated by
hooking the interrupt.

This is a CPU + BDOS harness, not a full CP/M-86 machine: Unicorn provides the
instruction set (incl. 80186+), and this file emulates the BDOS system calls a
program makes. The console/string group is implemented, plus S_BDOSVER (12) and
the Concurrent CP/M-86 date/time calls T_SET (104) / T_GET (105) / T_SECONDS
(155) -- so a stock DATE.CMD prints the correct date and self-timing programs
(e.g. stdcbench) get advancing seconds. The RC759 XIOS Int 28h fn 19 "16 ms
counter" is deliberately NOT emulated (the real machine's XIOS does not maintain
it). All of this is a superset of plain CP/M-86, so ordinary CP/M-86 programs
keep working. Every other function (disk/file, and the rest of the Concurrent
CP/M / XIOS API) raises a clear "unimplemented BDOS function" error, so
unsupported programs fail loudly instead of silently.

Program load emulates the CCP: the memory model (8080 / small) is read from the
.CMD group descriptors, groups are placed in memory, segment registers and the
entry IP are set per the System Guide (8080 -> CS:0100H, small -> CS:0000H),
and the base page (group descriptors, default FCBs at 005CH/006CH, command tail
at 0080H) is populated from the command line via the ccp module.
"""

import os
import sys
import datetime
from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_CODE,
                     UC_HOOK_BLOCK)
from unicorn.x86_const import (
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_SP, UC_X86_REG_IP, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_AX, UC_X86_REG_BX,
)

import ccp

# CP/M-86 .CMD group-descriptor types (System Guide, CMD-header appendix;
# cross-checked with seasip.info/Cpm/cmdfile.html). Only CODE/DATA are named
# here; the loader below keeps all other group types (3..8) generically.
G_CODE = 0x01
G_DATA = 0x02


def parse_header(data):
    """Parse the 8x9-byte group descriptors of a CP/M-86 .CMD header.

    Each descriptor is type(1) + length(2) + base(2) + min(2) + max(2), all
    little-endian, lengths/bases in 16-byte paragraphs. Stops at the first
    zero-type slot. Returns a list of (gtype, length, base, minp, maxp) tuples.
    """
    groups = []
    for i in range(8):
        off = i * 9
        gtype = data[off]
        if gtype == 0:
            break
        length = data[off + 1] | (data[off + 2] << 8)
        base = data[off + 3] | (data[off + 4] << 8)
        minp = data[off + 5] | (data[off + 6] << 8)
        maxp = data[off + 7] | (data[off + 8] << 8)
        groups.append((gtype, length, base, minp, maxp))
    return groups


LOAD_SEG = 0x1000      # paragraph where the first (code) group is placed
CCP_SEG = 0x0800       # emulated CCP area: stack + a return stub below the program
MEM_BASE = 0x0000
MEM_SIZE = 0x100000    # 1 MB, must be page aligned for Unicorn
MAX_INSNS = 200_000_000  # runaway guard (large enough for real benchmarks)

# --- Concurrent CP/M-86 clock emulation ----------------------------------
# Ordinary CP/M-86 (1.x) has no clock, but Concurrent CP/M-86 / CP/M-86 Plus
# add T_GET (BDOS 105) "get date and time".  We report a BDOS version of 3.1
# from S_BDOSVER (BDOS 12) so date/time-aware programs know T_GET is available,
# while staying backward compatible: the value is still >= 2.2, so plain
# CP/M-86 programs that only check "at least version 2.x" keep working.
BDOS_VERSION = int(os.environ.get("CPM86_BDOS_VERSION", "0x0031"), 0)

# The emulator has no hardware clock, so T_GET's base date/time is the host's
# real wall clock at the moment the program starts (so a stock DATE.CMD prints
# the correct current date), and it then advances by a deterministic virtual
# clock: the number of code bytes executed so far (counted by a block hook),
# divided by CLOCK_HZ, gives the emulated elapsed seconds added on top.  The
# absolute time therefore tracks reality, while elapsed *differences* -- all a
# benchmark's "while(clock()-start<N)" loop actually uses -- stay host
# independent and reproducible, proportional to the work the emulated 8086 does.
# Set CPM86_EPOCH (e.g. "2024-01-01T00:00:00") to pin the base for a fully
# deterministic absolute clock; tune the rate via CPM86_CLOCK_HZ.
CLOCK_HZ = int(os.environ.get("CPM86_CLOCK_HZ", "20000"))

# CP/M's date field is a day number with day 1 == 1 January 1978.
CPM_DAY_ONE = datetime.date(1978, 1, 1)

# --- Relative-time source: BDOS T_SECONDS (not XIOS Int 28h fn 19) ----------
# The RC759 Piccoline runs an Intel 80186 (6-8 MHz), but its three on-chip
# 80186 timers are wired to sound/cassette, not timekeeping (PICCOLINE
# Programmer's Guide sec. 2.3).  The PICCOLINE Guide (App. A, fn 19) documents an
# XIOS "16 ms counter" read via Int 28h function 19, but on the real Concurrent
# CP/M-86 turnkey disk that XIOS does NOT maintain the counter (verified: it
# never advances, so stdcbench's timed loop hung).  So we do NOT model fn 19.
# Program-visible time that DOES advance comes from the ordinary BDOS clock:
# T_GET (fn 105, date + minute) and T_SECONDS (fn 155, date + seconds) -- the
# latter is what self-timing programs (stdcbench's portme.c) use for elapsed
# time on real hardware, at one-second resolution.  Both are driven here by the
# deterministic code-byte virtual clock (seconds = ticks/CLOCK_HZ), so elapsed
# *differences* stay host independent and reproducible.  CPM86_TICK_MS only
# affects the optional 16 ms diagnostic print below, not any guest-visible time.
TICK_MS = int(os.environ.get("CPM86_TICK_MS", "16"))


class Done(Exception):
    pass


class BdosUnimplemented(Exception):
    def __init__(self, func, kind="CP/M-86 / Concurrent CP/M BDOS"):
        super().__init__(f"unimplemented {kind} "
                         f"function {func} (0x{func:02X}) -- refusing to run")
        self.func = func
        self.kind = kind


def _detect_model(groups):
    types = [g[0] for g in groups]
    if types == [G_CODE]:
        return "8080"
    if sorted(types) == [G_CODE, G_DATA]:
        return "small"
    return "compact"


def _apply_fixups(uc, data, group_seg):
    """Apply CP/M-86 load-time relocation ("P_LOAD" fixups) to memory.

    Faithful port of the genuine Concurrent CP/M-86 2.0 loader (`load.sup`
    lines 402-449; see [[reference_cpm86_cmd_header_ccpm_source]]). It runs only
    when header byte 127 (`ch_lbyte`, 0x7F) has bit 7 (0x80) set. The fixup
    table starts at file RECORD number `ch_fixrec` (word at 0x7D), i.e. byte
    offset ch_fixrec*128, and is a packed array of 4-byte records terminated by
    the first all-zero record:

        byte 0   fix_grp   hi nibble = group the LOCATION word lives in
                           lo nibble = TARGET group whose LOAD SEGMENT is ADDED
        byte 1-2 fix_para  paragraph offset of the word within the LOCATION
                           group (little-endian)
        byte 3   fix_offs  byte offset 0..15 of the word within that paragraph

    For each record the loader does `add es:[loc], dx` where `es:loc` is
    (location_group_load_seg + para):offs and `dx` is the target group's actual
    load segment.  The word already holds a *group-relative* paragraph (what the
    linker wrote in place of the final segment); adding the runtime load segment
    turns it into the absolute segment.

    Worked example (DR C `RELOCSEG.CMD`, fixup rec #561 = the CLEARL guard):
    grp=0x11 (location in CODE, add CODE base), para=0x017, offs=6 -> the word
    at code_seg:(0x17*16+6) = code_seg:0x176 (CLEARL's `mov cx,0` immediate)
    gets code_seg added, so it becomes nonzero and CLEARL skips its own
    self-relocation -- exactly the guard that stops loader+crt0 double-applying.

    Returns the number of records applied.
    """
    if not (data[0x7F] & 0x80):          # bit 7 clear -> no load-time fixups
        return 0
    fixrec = data[0x7D] | (data[0x7E] << 8)   # start record number (word, LE)
    pos = fixrec * 128                        # record N -> byte offset N*128
    applied = 0
    while pos + 4 <= len(data):
        grp, para_lo, para_hi, offs = data[pos:pos + 4]
        if grp == 0 and para_lo == 0 and para_hi == 0 and offs == 0:
            break                             # table ends at the first zero rec
        loc_grp = (grp >> 4) & 0x0F           # group the location word lives in
        tgt_grp = grp & 0x0F                  # group whose load seg is added
        para = para_lo | (para_hi << 8)
        loc_seg = group_seg.get(loc_grp)
        tgt_seg = group_seg.get(tgt_grp)
        if loc_seg is None or tgt_seg is None:
            raise ValueError(
                f"fixup record {applied} at 0x{pos:X} references undefined "
                f"group (grp byte 0x{grp:02X}); loaded groups: "
                f"{sorted(group_seg)}")
        addr = ((loc_seg + para) << 4) + offs
        w = uc.mem_read(addr, 2)
        val = ((w[0] | (w[1] << 8)) + tgt_seg) & 0xFFFF
        uc.mem_write(addr, bytes((val & 0xFF, (val >> 8) & 0xFF)))
        applied += 1
        pos += 4
    return applied


def _load(uc, data, cmdline):
    """Load a .CMD image into `uc` and set up registers + base page like the
    CCP. Returns (cs, ip). Raises ValueError on a malformed file."""
    if len(data) < 128 or not (1 <= data[0] <= 9):
        raise ValueError("not a CP/M-86 .CMD file")
    groups = parse_header(data)
    if not groups:
        raise ValueError(".CMD header has no group descriptors")
    model = _detect_model(groups)
    body = data[128:]

    # Slice the body into per-group images, in header order.  Keep EVERY group
    # (not just code/data): the large ("compact") model links DR C's startup
    # (CLEARL) and additional far code/data into extra/stack/auxiliary groups
    # (CMD types 3..8) that must all be present in memory -- and described in
    # the base page -- or the startup aborts ("You must link with LINK86 V1.2").
    images, off = {}, 0        # first image per gtype (code/data convenience)
    all_groups = []            # (gtype, length, minp, maxp, image) in file order
    for (gtype, length, base, minp, maxp) in groups:
        nbytes = length * 16
        img = body[off:off + nbytes]
        if gtype not in images:
            images[gtype] = img
        all_groups.append((gtype, length, minp, maxp, img))
        off += nbytes

    code_img = images.get(G_CODE, b"")
    data_img = images.get(G_DATA, b"")
    code_len = len(code_img)

    code_seg = LOAD_SEG
    uc.mem_write(code_seg << 4, code_img)

    # Map each CP/M-86 group NUMBER (== descriptor type: 1=code, 2=data,
    # 3=extra, 4=stack, 5..8=aux) to the paragraph it is actually loaded at.
    # _apply_fixups() below uses this exactly as the real loader's `tblsrch`
    # does (load.sup): a fixup record names groups by number and the loader adds
    # that group's runtime load segment.
    group_seg = {G_CODE: code_seg}

    if model == "8080":
        data_seg = code_seg
        data_len = code_len
        entry_ip = ccp.BASE_PAGE_SIZE            # 0x100: skip the base page
    else:
        code_paras = (code_len + 15) // 16
        data_seg = code_seg + max(code_paras, 1)
        uc.mem_write(data_seg << 4, data_img)
        entry_ip = 0x0000                        # small/compact enter at CS:0000

        # Give the data group a full 64K segment by default (matching CP/M-86's
        # CCP and emu2's proven loader): the bootstrap stack lives at SS:SP =
        # data_seg:0xFFFE (set below) and the base page's LD field (offset 06H)
        # doubles as the stack top DR C's startup reads ("mov sp,6[bx]"), so the
        # data group must span the whole segment.  Only shrink below 64K when the
        # descriptor's G_MAX explicitly asks for less.  This matters for the
        # large model, whose data descriptor carries G_MAX = 0: without a full
        # segment the bootstrap stack would land on top of the following
        # (extra/stack/aux) groups.
        img_paras = (len(data_img) + 15) // 16
        data_desc = next((g for g in groups
                          if g[0] == G_DATA), None)
        alloc_paras = 0x1000                      # 64K
        if data_desc is not None:
            _, _, _, dminp, dmaxp = data_desc
            if dmaxp and dmaxp >= img_paras and dmaxp < 0x1000:
                alloc_paras = dmaxp
        alloc_paras = max(alloc_paras, img_paras)
        data_len = alloc_paras * 16              # LD = data_len - 1
    group_seg[G_DATA] = data_seg

    # Build the base page from the command line and write it into the data
    # segment (which equals the code segment in the 8080 model).
    fcb1, fcb2, tail = ccp.fcbs_and_tail_from_cmdline(cmdline)
    bp = ccp.build_base_page(model=model, code_seg=code_seg, code_len=code_len,
                             data_seg=data_seg, data_len=data_len,
                             fcb1=fcb1, fcb2=fcb2, tail=tail)

    # Large ("compact") model: place and describe the remaining groups.  CP/M-86
    # gives each group (extra=3, stack=4, auxiliary 1..4 = 5..8) its own segment
    # and records base+length in a 6-byte base-page descriptor:
    #   +0 length in BYTES (24-bit)  +3 segment paragraph (word)  +5 model flag.
    # DR C's large-model startup (CLEARL) walks these descriptors; if the extra
    # code/data groups are absent or undescribed it aborts with "You must link
    # with LINK86 V1.2".  The small/8080 models simply have none of these groups,
    # so this loop is a no-op there and their setup is unchanged.
    DESC_OFF = {0x03: 0x0C, 0x04: 0x12,      # extra, stack
                0x05: 0x18, 0x06: 0x1E,      # aux 1, aux 2
                0x07: 0x24, 0x08: 0x2A}      # aux 3, aux 4
    free_seg = data_seg + (data_len + 15) // 16   # first paragraph past data
    for (gtype, length, minp, maxp, img) in all_groups:
        off = DESC_OFF.get(gtype)
        if off is None:                          # code/data handled above
            continue
        want = maxp if (maxp and maxp >= length) else max(minp, length)
        want = max(want, 1)
        # Clamp to the RAM actually available above this group -- a real CP/M-86
        # loader grants only as much of the type-3 Extra reservation as physically
        # fits in the TPA, then the program uses what it got. Without this a large
        # FARHEAP=<size> (e.g. ~1 MB) would zero-fill past MEM_SIZE and trap
        # UC_ERR_WRITE_UNMAPPED. Example: free_seg=0x1A00, maxp=0xF000 -> want
        # clamped to 0x10000-0x1A00 = 0xE600 paras (~920 KB granted of 960 asked).
        avail = (MEM_SIZE >> 4) - free_seg
        if avail < 1:
            avail = 1
        if want > avail:
            want = avail
        seg = free_seg
        free_seg += want
        group_seg[gtype] = seg
        uc.mem_write(seg << 4, b"\x00" * (want * 16))   # zero-fill (BSS)
        if img:
            uc.mem_write(seg << 4, img)                 # initialised part
        nbytes = want * 16
        bp[off + 0] = nbytes & 0xFF
        bp[off + 1] = (nbytes >> 8) & 0xFF
        bp[off + 2] = (nbytes >> 16) & 0xFF
        bp[off + 3] = seg & 0xFF
        bp[off + 4] = (seg >> 8) & 0xFF
        bp[off + 5] = 0                                  # model flag (0 = 8086)

    # Apply load-time relocation (byte-127 bit7 + ch_fixrec) now that every
    # group is in memory and we know each group's actual load segment -- exactly
    # where the genuine loader does it (load.sup: fixups, THEN base-page init).
    # For DR C .CMDs this makes CLEARL's guard immediate nonzero so its crt0
    # self-relocation is skipped (loader relocated instead); for pure-P_LOAD
    # wlink medium-model output (no crt0 walker) this is the ONLY relocation.
    n_fixups = _apply_fixups(uc, data, group_seg)
    if n_fixups and os.environ.get("CPM86_DEBUG"):
        print(f"[cpm86run] applied {n_fixups} load-time fixup(s)",
              file=sys.stderr)

    uc.mem_write(data_seg << 4, bytes(bp))

    # Segment registers per memory model (System Guide Section 2.3-2.5).
    uc.reg_write(UC_X86_REG_CS, code_seg)
    uc.reg_write(UC_X86_REG_DS, data_seg)
    uc.reg_write(UC_X86_REG_ES, data_seg)

    # SS:SP live in the CCP, not the program. The stack is placed in the
    # program's *data* group so that SS == DS: a real C program (e.g. the
    # Dhrystone benchmark) constantly takes the address of a stack local and
    # passes it as a small-model *near* pointer, which the callee dereferences
    # through DS -- so those pointers only resolve correctly when SS == DS.
    # Give (almost) a full 64K segment of stack and a far-return stub in the
    # CCP area so a program that RETFs also terminates cleanly via BDOS 0.
    uc.mem_write(CCP_SEG << 4, bytes([0xB1, 0x00, 0xCD, 0xE0]))  # mov cl,0; int E0h
    uc.reg_write(UC_X86_REG_SS, data_seg)
    sp = 0xFFFE
    ret_frame = bytes([0x00, 0x00, CCP_SEG & 0xFF, (CCP_SEG >> 8) & 0xFF])
    uc.mem_write((data_seg << 4) + sp - 4, ret_frame)          # [IP=0][CS=CCP]
    uc.reg_write(UC_X86_REG_SP, sp - 4)

    uc.reg_write(UC_X86_REG_IP, entry_ip)
    return code_seg, entry_ip


def _bcd(n):
    """8-bit packed BCD of n (0..99), as CP/M's clock fields are BCD."""
    return ((n // 10) << 4) | (n % 10)


def _clock_base():
    """The base date/time for T_GET: CPM86_EPOCH if set, else the host now."""
    env = os.environ.get("CPM86_EPOCH")
    if env:
        return datetime.datetime.fromisoformat(env)
    return datetime.datetime.now()


def _write_tod(uc, seg, off, base_dt, elapsed_seconds):
    """Write a Concurrent CP/M-86 time-of-day (TOD) structure at seg:off and
    return the seconds field (BCD), which T_GET hands back in AL.

    Layout (System Guide, T_GET): word date (day number, day 1 = 1978-01-01),
    byte hour (BCD), byte minute (BCD); seconds (BCD) are returned in AL.  The
    value is base_dt + elapsed_seconds, so it tracks the real date while still
    advancing monotonically with emulated work."""
    now = base_dt + datetime.timedelta(seconds=elapsed_seconds)
    day = (now.date() - CPM_DAY_ONE).days + 1
    base = (seg << 4) + (off & 0xFFFF)
    uc.mem_write(base, bytes([day & 0xFF, (day >> 8) & 0xFF,
                              _bcd(now.hour), _bcd(now.minute)]))
    return _bcd(now.second)


def run(path, cmdline=None, stdin_bytes=b"", count_insns=False,
        count_cycles=False, dump_path=None):
    """Load and run a .CMD file. Returns the captured console output (str).

    cmdline is the command line as an operator would type it, e.g.
    "ECHOARG A.TXT B.DAT"; its two filename arguments populate the default FCBs
    at DS:005CH / DS:006CH and its tail is placed at DS:0080H, exactly as the
    CCP does. If omitted, the program's own file name is used as the command
    with no arguments.

    stdin_bytes feeds console-input BDOS calls (functions 1/6/10); when it runs
    out, input calls return 0 / empty.

    When count_insns is true, every executed instruction is counted and the
    total is returned as a second element (out, n_instructions); the tally is
    deterministic (host-independent) and is useful as a size/work metric since
    the emulator models no real cycle timing.
    """
    data = open(path, "rb").read()
    if cmdline is None:
        cmdline = os.path.splitext(os.path.basename(path))[0].upper()

    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(MEM_BASE, MEM_SIZE)
    cs, ip = _load(uc, data, cmdline)

    out = bytearray()
    inp = bytearray(stdin_bytes)
    state = {"error": None, "ticks": 0, "base_dt": _clock_base()}

    def set_al(val):
        ax = uc.reg_read(UC_X86_REG_AX) & 0xFF00
        uc.reg_write(UC_X86_REG_AX, ax | (val & 0xFF))

    def set_ax(val):
        uc.reg_write(UC_X86_REG_AX, val & 0xFFFF)

    def next_in():
        return inp.pop(0) if inp else 0

    def hook_intr(uc, intno, user_data):
        if intno == 0x28:                            # RC759 XIOS interface
            # The real RC759 XIOS does NOT maintain the deprecated Int 28h fn 19
            # "16 ms counter" (verified on the Concurrent CP/M-86 turnkey disk:
            # the counter never advances), so we do not emulate it either -- any
            # Int 28h call fails loudly here.  Self-timing programs must use the
            # BDOS T_SECONDS call (fn 155) instead, which the real machine and
            # this runner both support.
            xios_func = uc.reg_read(UC_X86_REG_AX) & 0xFF   # AL
            uc.emu_stop()
            state["error"] = BdosUnimplemented(
                xios_func, kind="RC759 XIOS (Int 28h)")
            return
        if intno != 0xE0:                            # only CP/M-86 BDOS
            uc.emu_stop()
            state["error"] = RuntimeError(f"unexpected INT 0x{intno:02X}")
            return
        func = uc.reg_read(UC_X86_REG_CX) & 0xFF     # CL
        dx = uc.reg_read(UC_X86_REG_DX) & 0xFFFF
        ds = uc.reg_read(UC_X86_REG_DS) & 0xFFFF

        if func == 0:                                # P_TERMCPM
            uc.emu_stop()
            state["done"] = True
        elif func == 1:                              # C_READ (echoed)
            ch = next_in()
            out.append(ch)
            set_al(ch)
        elif func == 2:                              # C_WRITE (DL)
            out.append(dx & 0xFF)
        elif func == 5:                              # L_WRITE (list device)
            out.append(dx & 0xFF)
        elif func == 6:                              # C_RAWIO
            if (dx & 0xFF) == 0xFF:                  # input request
                set_al(next_in())
            else:                                    # output
                out.append(dx & 0xFF)
        elif func == 9:                              # C_WRITESTR ($-terminated)
            off = dx
            while True:
                ch = uc.mem_read((ds << 4) + (off & 0xFFFF), 1)[0]
                if ch == ord("$"):
                    break
                out.append(ch)
                off = (off + 1) & 0xFFFF
            set_al(0x24)
        elif func == 10:                             # C_READSTR (buffered line)
            mx = uc.mem_read((ds << 4) + dx, 1)[0]   # buffer[0] = max length
            line = bytearray()
            while len(line) < mx:
                ch = next_in()
                if ch in (0, 0x0D, 0x0A):
                    break
                line.append(ch)
                out.append(ch)
            uc.mem_write((ds << 4) + dx + 1, bytes([len(line)]))
            uc.mem_write((ds << 4) + dx + 2, bytes(line))
        elif func == 11:                             # C_STAT (console status)
            set_al(1 if inp else 0)
        elif func == 12:                             # S_BDOSVER (version)
            set_ax(BDOS_VERSION)                      # 3.1 -> date/time present
            uc.reg_write(UC_X86_REG_BX, BDOS_VERSION)
        elif func == 13:                             # DRV_ALLRESET (reset disks)
            set_al(0)                                 # no auto-select, OK
        elif func == 14:                             # DRV_SET (select disk)
            set_al(0)                                 # selection accepted
        elif func == 25:                             # DRV_GET (current disk)
            set_al(0)                                 # default drive = A:
        elif func == 104:                            # T_SET (set date/time)
            set_al(0)                                 # accepted, no-op clock
        elif func == 105:                            # T_GET (get date/time)
            elapsed = state["ticks"] // CLOCK_HZ if CLOCK_HZ else 0
            al = _write_tod(uc, ds, dx, state["base_dt"], elapsed)
            set_al(al)                                # AL = seconds (BCD)
        elif func == 155:                            # T_SECONDS (date/time + seconds)
            # Like T_GET but the SECONDS field is also stored in the TOD struct
            # (byte at offset 4), which is exactly how stdcbench's portme.c reads
            # elapsed time on the real RC759 -- verified there (score 13).  The
            # real Concurrent CP/M-86 supports this call; the deprecated XIOS
            # Int 28h fn 19 "16 ms counter" it does NOT (that XIOS never
            # maintains the counter), so self-timing programs must use T_SECONDS.
            #
            # IMPORTANT: the elapsed seconds come from state["ticks"], the code-
            # byte virtual clock, which advances ONLY when the block hook is
            # installed -- i.e. in a --count/--ticks run (see the FULL-SPEED RULE
            # below).  A plain run() has ticks==0, so a self-timing benchmark
            # would see NO elapsed time.  Run such programs with --count.
            elapsed = state["ticks"] // CLOCK_HZ if CLOCK_HZ else 0
            al = _write_tod(uc, ds, dx, state["base_dt"], elapsed)
            uc.mem_write((ds << 4) + ((dx + 4) & 0xFFFF), bytes([al]))
            set_al(al)                                # AL = seconds (BCD)
        else:
            uc.emu_stop()
            state["error"] = BdosUnimplemented(func)

    uc.hook_add(UC_HOOK_INTR, hook_intr)

    # Deterministic virtual clock: count code bytes executed (per basic block,
    # so the overhead is far lower than a per-instruction hook).  T_GET and the
    # XIOS 16 ms tick (Int 28h fn 19) turn this into emulated time via CLOCK_HZ.
    #
    # FULL-SPEED RULE (2026-08-19, @ravn): a plain output run() must execute at
    # native Unicorn speed with NO per-block Python callback.  The virtual clock
    # is only meaningful when we are already paying per-instruction decode cost
    # to count insns/cycles, so we install the block hook ONLY in a counting run.
    # A plain run() therefore has just the (infrequent) INT hook for BDOS.
    if count_insns or count_cycles:
        def hook_block(uc, address, size, user_data):
            state["ticks"] += size
        uc.hook_add(UC_HOOK_BLOCK, hook_block)

    insns = {"n": 0}
    if count_insns and not count_cycles:
        def hook_code(uc, address, size, user_data):
            insns["n"] += 1
        uc.hook_add(UC_HOOK_CODE, hook_code)

    # Estimated 80186 execution cycles ("ticks").  Unicorn models no timing, so
    # we decode each executed instruction (capstone) and add its iAPX 186 clock
    # count from cycles186.Cycle186.  Conditional branches/loops need to know
    # whether they were taken; we defer a branch's cost until the next
    # instruction executes and infer taken = (next address != fall-through).
    cyc = {"n": 0}
    if count_cycles:
        from cycles186 import Cycle186
        model = Cycle186()
        dec = {}                       # linear addr -> (cs_insn, is_cond)
        pend = {"insn": None, "fall": 0}

        def _decode(address, size):
            hit = dec.get(address)
            if hit is not None:
                return hit
            code = bytes(uc.mem_read(address, size))
            ins = next(model.md.disasm(code, address), None)
            hit = (ins, bool(ins) and model.is_conditional_branch(ins))
            dec[address] = hit
            return hit

        def hook_cycles(uc, address, size, user_data):
            insns["n"] += 1
            p = pend["insn"]
            if p is not None:                       # resolve prior branch
                taken = (address != pend["fall"])
                cyc["n"] += model.clocks(p, taken=taken)
                pend["insn"] = None
            ins, is_cond = _decode(address, size)
            if ins is None:
                cyc["n"] += 4
                return
            if is_cond:                             # defer until we see target
                pend["insn"] = ins
                pend["fall"] = address + size
            else:
                cyc["n"] += model.clocks(ins)
        uc.hook_add(UC_HOOK_CODE, hook_cycles)

    try:
        uc.emu_start((cs << 4) + ip, 0, count=MAX_INSNS)
    except Exception:
        pass
    if count_cycles and pend["insn"] is not None:   # flush trailing branch
        cyc["n"] += model.clocks(pend["insn"], taken=True)
    if state.get("error"):
        raise state["error"]
    if dump_path:
        # Full physical-RAM snapshot AFTER the guest finished, for an oracle
        # INDEPENDENT of the guest's own self-check: the host reads back the
        # far-heap bytes straight out of RAM (see verify_farheap_dump.py) rather
        # than trusting the program's printed PASS. 1 MB raw little-endian image,
        # physical address == file offset.
        with open(dump_path, "wb") as fh:
            fh.write(bytes(uc.mem_read(MEM_BASE, MEM_SIZE)))
    if os.environ.get("CPM86_DEBUG_CLOCK"):
        secs = state["ticks"] / CLOCK_HZ if CLOCK_HZ else 0
        ticks16 = int(secs * 1000 // TICK_MS)
        print("cpm86: code-bytes=%d CLOCK_HZ=%d emulated-seconds=%.3f "
              "TICK_MS=%d ticks16ms=%d"
              % (state["ticks"], CLOCK_HZ, secs, TICK_MS, ticks16),
              file=sys.stderr)
    text = out.decode("cp437", errors="replace")
    if count_cycles:
        return text, insns["n"], cyc["n"]
    if count_insns:
        return text, insns["n"]
    return text


def main(argv=None):
    argv = argv or sys.argv[1:]
    count_insns = count_cycles = False
    dump_path = None
    while argv and argv[0] in ("--count", "-c", "--ticks", "-t", "--dump"):
        if argv[0] in ("--count", "-c"):
            count_insns = True
            argv = argv[1:]
        elif argv[0] == "--dump":
            dump_path = argv[1]
            argv = argv[2:]
        else:
            count_cycles = True
            argv = argv[1:]
    if not argv:
        print("usage: cpm86run_unicorn.py [--count] [--ticks] [--dump FILE] "
              "FILE.CMD [ARG ...]",
              file=sys.stderr)
        return 2
    path = argv[0]
    prog = os.path.splitext(os.path.basename(path))[0].upper()
    cmdline = " ".join([prog] + list(argv[1:]))
    try:
        stdin_bytes = b""
        try:
            if not sys.stdin.isatty():
                stdin_bytes = sys.stdin.buffer.read()
        except Exception:
            stdin_bytes = b""
        result = run(path, cmdline=cmdline, stdin_bytes=stdin_bytes,
                     count_insns=count_insns, count_cycles=count_cycles,
                     dump_path=dump_path)
        if count_cycles:
            text, n, ticks = result
            sys.stdout.write(text)
            sys.stdout.flush()
            print(f"\n[{n} instructions, ~{ticks} 80186 clocks (estimate)]",
                  file=sys.stderr)
        elif count_insns:
            text, n = result
            sys.stdout.write(text)
            sys.stdout.flush()
            print(f"\n[{n} instructions executed]", file=sys.stderr)
        else:
            sys.stdout.write(result)
            sys.stdout.flush()
    except BdosUnimplemented as e:
        print(f"\ncpm86run_unicorn: {e}", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
