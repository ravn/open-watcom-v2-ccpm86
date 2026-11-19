;****************************************************************************
;
;   Minimal CP/M-86 C startup stub for Open Watcom (wasm), 8080/small model.
;
;   `wl format raw` concatenates the _TEXT segments of the linked objects in
;   link order and does NOT reorder them to put the entry symbol first -- the
;   CP/M-86 loader simply jumps to the first byte of the code group. For a C
;   program with several functions the entry function is therefore usually NOT
;   first, so we must supply a startup object linked *first* whose first byte
;   is the real entry point. It calls the C entry `cpmmain()` (wcc __watcall
;   name: cpmmain_) and, when that returns, terminates via BDOS P_TERMCPM.
;
;   CS = DS = ES at entry, but SS does NOT equal DS: per the DR CP/M-86 System
;   Guide Sec 4.1.2 (confirmed against DR C 1.11's own startup.a86, m.init.stack,
;   and measured on real hardware -- MAME rc759 running genuine Concurrent
;   CP/M-86 3.1 -- CS=DS=ES=4C86 but SS=4C80), the loader hands the program a
;   small throwaway scratch stack in a segment separate from DS. A conforming
;   program must switch to SS=DS itself, with SP from the base-page word at
;   offset 6 (the data/code group's "top of stack" length field), as its very
;   first act -- exactly what DR C's startup.a86 does (`push ds / pop ss` since
;   8086 has no direct mov ss,ds, then `mov sp,[6]`, interrupts off across the
;   pair so an interrupt can never see a stale SS with the new SP or vice versa).
;
;   Without this switch, near pointers taken to on-stack locals (SS-relative in
;   the CPU's own BP-addressing, but DS-relative by the small-model C pointer
;   convention) resolve to the WRONG physical address whenever the loader's
;   SS happens to differ from DS -- e.g. a program that works under a lenient
;   emulator (which may set SS=DS trivially) but silently writes to unrelated
;   memory on real hardware. Confirmed 2026-08-16: an unmodified Dhrystone port
;   built with the old (missing-switch) cpmstart.asm produced a wrong Int_1_Loc
;   on real MAME rc759 (Proc_2's `*Int_Par_Ref = ...` never reached the actual
;   stack slot) while passing under a too-forgiving emu2; fixed once emu2 was
;   also corrected to hand out a genuinely separate entry-time stack segment.
;
;   Link (native CP/M-86 route):
;       wasm cpmstart.asm ; wcc ... prog.c
;       wl format cpm86 name PROG.CMD file cpmstart.obj file prog.obj
;       (the former raw+bin2cmd.py wrapping route was retired)
;
;****************************************************************************
        .8086

_TEXT   segment byte public 'CODE'
        assume  cs:_TEXT

        extrn   cpmmain_ : near
        public  cpmstart_

cpmstart_:
        cli                             ; SS:SP must change as one atomic unit
        push    ds                      ; SS = DS (8086 has no mov ss,ds)
        pop     ss
        mov     sp, word ptr [6]        ; SP = base-page word 6 (group top)
        sti
        call    cpmmain_                ; run the C program
        mov     cl, 0                   ; BDOS P_TERMCPM
        int     0E0h

_TEXT   ends
        end     cpmstart_
