/* tsecs_test.c -- exercises the CP/M-86 BDOS T_SECONDS timer (fn 155) the same
   way stdcbench's portme.c does: read the time, do a fixed amount of work, read
   the time again, and report the elapsed whole seconds.

   This is the regression oracle for the deterministic emulator clock.  On a
   real machine the elapsed seconds depend on CPU speed; on an emulator whose
   clock is driven by an instruction/byte counter (emu2's cpuGetInstructionCount
   / the Unicorn runner's code-byte ticks, seconds = count / CLOCK_HZ) the value
   is instead a reproducible function of the fixed work below and the CLOCK_HZ
   setting -- so two runs must agree exactly, and halving CLOCK_HZ must double
   the elapsed count.  A build that lacks fn 155 (old emu2) prints ELAPSED 0
   (BDOS returns 0xFF and never fills the seconds field), which fails the gate.

   SPINS is a compile-time knob (-DSPINS=...) so the harness can size the work
   to the emulator's default CLOCK_HZ. */

#ifndef SPINS
#define SPINS 4000000ul
#endif

extern int cprintf(const char *, ...);

/* Concurrent CP/M-86 T_SECONDS (BDOS fn 155): CL=155, DX=TOD offset, DS=TOD
   segment; fills the 5-byte TOD and returns BCD seconds in AL.  DS already
   addresses DGROUP, where the static struct lives, in both memory models. */
struct tod
{
    unsigned day;
    unsigned char hour, min, sec;
};
extern void t_seconds(struct tod *t);
#pragma aux t_seconds = \
    "mov cl,155"        \
    "mov dx,bx"         \
    "int 0E0h"          \
    parm[bx] modify[ax bx cx dx];

#define BCD2BIN(b) ((unsigned)(((b) >> 4) * 10u + ((b) & 0x0Fu)))

/* Fold day:hour:min:sec into a single monotonically increasing second count,
   exactly like portme.c -- only differences are ever used. */
static unsigned long fold(const struct tod *t)
{
    return ((((unsigned long)t->day * 24ul + BCD2BIN(t->hour)) * 60ul)
            + BCD2BIN(t->min)) * 60ul + BCD2BIN(t->sec);
}

int main(void)
{
    static struct tod a, b;
    volatile unsigned sink = 0;
    unsigned long i, start, end, elapsed;

    t_seconds(&a);
    start = fold(&a);

    for(i = 0; i < (unsigned long)SPINS; i++)
        sink += (unsigned)i;

    t_seconds(&b);
    end = fold(&b);
    elapsed = end - start;

    cprintf("SINK %u\r\n", (unsigned)sink);
    cprintf("ELAPSED %lu\r\n", elapsed);
    cprintf(elapsed > 0ul ? "PASS\r\n" : "FAIL\r\n");
    return 0;
}
