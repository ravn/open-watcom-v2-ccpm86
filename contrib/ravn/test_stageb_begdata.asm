; Reserve the CP/M-86 base page (DS:0000-00FF) at the very start of DGROUP,
; exactly as the clib cstartcpm.asm does.  A freestanding -mm -zm test that is
; linked WITHOUT the C startup has no base-page reservation, so the loader's
; init_base step -- which puts the base page at the start of the first DATA
; group and zero-fills it (genuine CCP/M 2.0 kern/load.sup:477 "1st Data Group
; has Base Page") -- would clobber the program's own initialized data if that
; data sat at DS:0000.  Linking this module FIRST forces a 0x100-byte filler
; ahead of the compiler's CONST/CONST2/_DATA, so real data starts at DS:0100
; and survives the loader.  (Real programs get this from cstartcpm.asm; these
; micro-tests are freestanding and supply it themselves.)
BEGDATA segment word public 'BEGDATA'
        db      100h dup(0)
BEGDATA ends
CONST   segment word public 'DATA'
CONST   ends
CONST2  segment word public 'DATA'
CONST2  ends
_DATA   segment word public 'DATA'
_DATA   ends
_BSS    segment word public 'BSS'
_BSS    ends
DGROUP  group   BEGDATA, CONST, CONST2, _DATA, _BSS
        end
