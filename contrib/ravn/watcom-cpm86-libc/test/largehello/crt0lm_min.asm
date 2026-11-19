        name    crt0lm_min
; ---------------------------------------------------------------------------
; crt0lm_min.asm -- MINIMAL CP/M-86 LARGE-model ("far code, FAR data") startup,
; freestanding (no clib) -- used only to prove that a large-model `.CMD` loads
; and runs under emu2/MAME before committing to a full large-model clib.
;
; Large model == medium's far code (this startup's calls are FAR, and it
; publishes the `_big_code_` model marker EXTRN'd by -ml objects) + compact's
; far data (handled entirely in C-side codegen + loader, nothing here). So the
; startup body is byte-for-byte crt0mm's far-code shape, minus the clib init.
;
; Provides a tiny BDOS console-out helper (bdos_conout, __watcall: char in AL)
; so the C test can print without pulling in the stdio/heap runtime.
; ---------------------------------------------------------------------------
        extrn   main_ : far
        public  _cstart_
        public  _big_code_
        public  bdos_conout_
        public  __STK

DGROUP  group   BEGDATA, _DATA, STACK

BEGTEXT segment word public 'CODE'
        assume  cs:BEGTEXT, ds:DGROUP, ss:DGROUP
; Model marker: -ml C objects EXTRN _big_code_ (far); value is irrelevant, the
; linker only checks that exactly one code-model marker is present.
_big_code_      label   far
; Entry CS:0000. Loader set DS=ES=data group + a scratch stack. Move SS to DS,
; point SP at the top of our own DGROUP stack, then far-call the C main.
_cstart_:
        mov     ax, ds
        mov     ss, ax
        mov     sp, offset DGROUP:stktop
        call    far ptr main_
; MAME done-signal (mamedone.h convention): OUT 0x2FE,AX with low byte = pass
; count. Undecoded on rc759 hardware (side-effect-free) but a Lua write-tap
; snapshots the console + stops the emulator, so the run is confirmed. Harmless
; under emu2 (the port is ignored). Must be AFTER all console output.
        mov     ax, 1                   ; 1 pass, 0 fail
        mov     dx, 02FEh
        out     dx, ax
        xor     dx, dx
        mov     cl, 0                   ; BDOS 0 = System Reset (terminate)
        int     0E0h
; void bdos_conout(int c) -- __watcall passes c in AX; emit AL via BDOS 2.
; __watcall requires the callee to preserve every register it does not use to
; return a value (here: void -> only AX is scratch). The C caller caches the
; far string's SEGMENT in DX and its OFFSET in BX across this call, so we MUST
; preserve BX/DX (and anything BDOS may clobber) or the loop reads garbage
; after one char.
bdos_conout_:
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds
        mov     dl, al
        mov     cl, 2                   ; BDOS 2 = console output
        int     0E0h
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        retf
; Watcom emits `call far ptr __STK` as a stack probe; our startup owns the
; stack, so a bare far RET is the correct no-op.
__STK:
        retf
BEGTEXT ends

BEGDATA segment word public 'BEGDATA'
        db      100h dup(0)             ; base page area DS:0000-00FF
BEGDATA ends

_DATA   segment word public 'DATA'
_DATA   ends

STACK   segment word public 'STACK'
        db      2048 dup(0)
stktop  label   word
STACK   ends
        end     _cstart_
