#!/usr/bin/env python3
"""Regression tests for CP/M-86 load-time relocation in cpm86run_unicorn.

Exercises `_apply_fixups()` (the P_LOAD / byte-127-bit7 + ch_fixrec loader
relocation, a port of CCP/M 2.0 `load.sup:402-449`) with a synthetic .CMD and a
tiny fake Unicorn `uc` so the test is self-contained: no built toolchain, no
scratch/ artifacts, and no real 8086 execution.  Run: `python3 test_cpm86_reloc.py`.

The end-to-end proof that these fixups relocate real DR C programs correctly
lives in the A/B check against genuine DR C .CMDs (LL_l/LL_s/MANDEL/TINY63 under
scratch/rc759-cmd-toolchain) -- see the session notes; those depend on external
artifacts and so are not part of this checked-in unit test.
"""
import cpm86run_unicorn as R


class FakeUc:
    """Minimal Unicorn stand-in: a flat byte array with mem_read/mem_write."""
    def __init__(self, size=0x40000):
        self.mem = bytearray(size)

    def mem_read(self, addr, n):
        return bytes(self.mem[addr:addr + n])

    def mem_write(self, addr, b):
        self.mem[addr:addr + len(b)] = b

    def wr16(self, seg, para, offs, val):
        a = ((seg + para) << 4) + offs
        self.mem[a:a + 2] = bytes((val & 0xFF, (val >> 8) & 0xFF))

    def rd16(self, seg, para, offs):
        a = ((seg + para) << 4) + offs
        return self.mem[a] | (self.mem[a + 1] << 8)


def _cmd_with_fixups(records, fixrec=1, bit7=True):
    """Build a synthetic .CMD: 128-byte header (byte-127 bit7 optionally set,
    ch_fixrec = `fixrec`) followed by a fixup table at record `fixrec` made of
    the given 4-byte tuples, zero-terminated."""
    data = bytearray(128 + 128)              # header + one record page
    data[0] = R.G_CODE                       # a plausible first descriptor type
    data[0x7F] = 0x80 if bit7 else 0x00
    data[0x7D] = fixrec & 0xFF
    data[0x7E] = (fixrec >> 8) & 0xFF
    pos = fixrec * 128
    for (grp, para, offs) in records:
        data[pos:pos + 4] = bytes((grp, para & 0xFF, (para >> 8) & 0xFF, offs))
        pos += 4
    # remaining bytes stay zero -> terminates the table
    return bytes(data)


def test_applies_and_adds_target_segment():
    code_seg, data_seg = 0x1000, 0x1200
    group_seg = {R.G_CODE: code_seg, R.G_DATA: data_seg}
    #   grp   para   offs   initial word   expected add
    #   0x11  0x017  6      0x0000         + code_seg  (CLEARL-guard shape)
    #   0x12  0x005  8      0x0002         + data_seg  (mov ax,DATA_seg)
    #   0x22  0x003  0      0x0140         + data_seg  (far ptr in DATA)
    recs = [(0x11, 0x017, 6), (0x12, 0x005, 8), (0x22, 0x003, 0)]
    data = _cmd_with_fixups(recs)
    uc = FakeUc()
    uc.wr16(code_seg, 0x017, 6, 0x0000)
    uc.wr16(code_seg, 0x005, 8, 0x0002)
    uc.wr16(data_seg, 0x003, 0, 0x0140)

    n = R._apply_fixups(uc, data, group_seg)

    assert n == 3, n
    assert uc.rd16(code_seg, 0x017, 6) == code_seg          # 0x0000 + 0x1000
    assert uc.rd16(code_seg, 0x005, 8) == 0x0002 + data_seg  # location=CODE, add DATA
    assert uc.rd16(data_seg, 0x003, 0) == 0x0140 + data_seg  # location=DATA, add DATA


def test_no_fixups_when_bit7_clear():
    group_seg = {R.G_CODE: 0x1000, R.G_DATA: 0x1200}
    data = _cmd_with_fixups([(0x11, 0x017, 6)], bit7=False)
    uc = FakeUc()
    uc.wr16(0x1000, 0x017, 6, 0x1234)
    assert R._apply_fixups(uc, data, group_seg) == 0
    assert uc.rd16(0x1000, 0x017, 6) == 0x1234              # untouched


def test_wraps_mod_65536():
    group_seg = {R.G_CODE: 0x1000, R.G_DATA: 0x2000}
    data = _cmd_with_fixups([(0x11, 0x000, 0)])
    uc = FakeUc()
    uc.wr16(0x1000, 0, 0, 0xF800)
    assert R._apply_fixups(uc, data, group_seg) == 1
    assert uc.rd16(0x1000, 0, 0) == (0xF800 + 0x1000) & 0xFFFF  # 0x0800


def test_undefined_group_raises():
    group_seg = {R.G_CODE: 0x1000}            # no DATA group loaded
    data = _cmd_with_fixups([(0x12, 0x001, 0)])   # needs group 2 (DATA)
    uc = FakeUc()
    try:
        R._apply_fixups(uc, data, group_seg)
    except ValueError:
        return
    raise AssertionError("expected ValueError for undefined group reference")


if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print(f"ok  {t.__name__}")
    print(f"\n{len(tests)} passed")
