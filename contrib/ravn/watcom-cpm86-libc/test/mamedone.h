/* mamedone.h -- let a CP/M-86 program signal completion to the MAME rc759 host.
 *
 * mame_done(status): writes `status` as a 16-bit word to I/O port 0x2FE with a
 * single `OUT DX,AX`. The rc759 driver does NOT decode 0x2FE (see rc759_io in
 * mame/src/mame/regnecentralen/rc759.cpp -- floppy ends at 0x290, iSBX starts
 * at 0x300), so the write has no effect on emulated hardware. But a MAME
 * io-space write-tap (mame-tests/done_signal.lua) still observes the bus cycle,
 * so the host can stop the emulator the instant the program finishes and read
 * `status` -- replacing the old "snapshot on a fixed 400s timer and eyeball it".
 *
 * Convention used by mtest.c: low byte = pass count, high byte = fail count, so
 * word 0x0013 means 19 pass / 0 fail. Any 16-bit code works; the Lua side just
 * prints and snapshots.
 *
 * Watcom-only (#pragma aux inline asm); harmless under emu2 (the OUT is ignored)
 * and must be the LAST thing the program does so all output is already flushed.
 */
#ifndef MAMEDONE_H
#define MAMEDONE_H

extern void mame_done(unsigned status);
#pragma aux mame_done =     \
    "mov dx,02FEh"          \
    "out dx,ax"             \
    parm [ax]               \
    modify [dx];

/* mame_out(word): stream ONE 16-bit word to the host on the same undecoded port
 * 0x2FE. Where mame_done squeezes a whole result into one byte-packed word, a
 * test with richer output (e.g. a 16-bit test count > 255) instead sends a SMALL
 * RECORD as a sequence of words -- a tag, then payload fields, then an end
 * sentinel -- in program order. The Lua tap (done_signal.lua / disk_done.lua)
 * collects the words and interprets the record when the sentinel arrives. This
 * carries full 16-bit fields with no guest-memory or mid-instruction register
 * reads, so it is deterministic on real rc759 hardware. Must run after all
 * console output is flushed. Example (disktest.c):
 *     mame_out(0xD15C);            // tag: disk result
 *     mame_out(tests);            // full 16-bit count (e.g. 511)
 *     mame_out(failures);         // 0 == PASS
 *     mame_out(0xE0F0);            // end sentinel -> host snapshots + exits
 */
extern void mame_out(unsigned word);
#pragma aux mame_out =      \
    "mov dx,02FEh"          \
    "out dx,ax"             \
    parm [ax]               \
    modify [dx];

#endif /* MAMEDONE_H */
