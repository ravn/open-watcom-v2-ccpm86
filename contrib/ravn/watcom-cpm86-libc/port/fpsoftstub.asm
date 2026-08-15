; port/fpsoftstub.asm -- rc7xx-work #8 soft-float support bytes.
;
; The Watcom -fpc __FDx runtime dispatches on __real87 (0 => no 8087 => take the
; pure-software __FDxemu path) and on __chipbug (0 => no buggy divider). Stock
; Watcom supplies these bytes from clib/startup/a/_8087086.asm and
; clib/fpu/a/chipvar.asm, but those also drag the C init routine __chk8087 (an
; XI-table FPU probe our minimal crt0 never walks) and __verify_problems. On a
; machine with NO 8087 we want neither -- the answer is statically "no 8087" --
; so we define just the data bytes here, all zero, which is exactly the
; no-coprocessor state. No 8087 is ever detected, probed, or used.
;
; Matches the public symbols / sizes of the stock files:
;   _8087086.asm : __8087 db, __real87 db, __dos87emucall dw, __dos87real db
;   chipvar.asm  : ___chipbug LABEL byte / __chipbug dd 0

include langenv.inc
include mdef.inc

        modstart    fpsoftstub

datasegment
        public  __8087
        public  __real87
        public  __dos87emucall
        public  __dos87real
        public  ___chipbug
        public  __chipbug
        public  ___FPE_handler

__8087          db  0   ; 0 => no real 80x87 and no EMU present
__real87        db  0   ; 0 => no real 80x87 used  => __FDxemu software path
__dos87emucall  dw  0   ; 0 => no 80x87 EMU control routine
__dos87real     db  0   ; 0 => no real 80x87 installed
___chipbug      LABEL   byte
__chipbug       dd  0   ; 0 => no buggy 8087 divider (irrelevant: no 8087)
; FP-exception handler slot: the C startup (cstrtw16.asm) normally defines this
; DWORD far-pointer via `public "C",__FPE_handler` (C linkage adds a leading _,
; so the external symbol is ___FPE_handler). Default 0 = "no user SIGFPE handler
; installed => take the library default (return the IEEE special result)".
; fstat086 / mathlib's _matherr reference it; our minimal crt0 never installs
; one, so 0 is correct.
___FPE_handler  dd  0   ; 0 => no user FP-exception handler
enddata

        endmod
        end
