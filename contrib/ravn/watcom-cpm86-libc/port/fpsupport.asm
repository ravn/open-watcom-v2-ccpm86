; port/fpsupport.asm -- rc7xx-work #8 soft-float leaf handlers.
;
; Watcom's -fpc double runtime (fdmth086.asm __FDxemu path) tail-jumps to
; F8OverFlow / F8UnderFlow / F8DivZero when a result over/underflows or a divide
; by zero occurs. Stock Watcom supplies these from clib/cgsupp/a/fstat086.asm,
; but that version also notifies errno (__set_ERANGE) and a SIGFPE hook
; (__FPE_handler), which drag the full C-startup FP-exception machinery. On this
; no-8087 CP/M-86 target we provide the IEEE numeric result directly and skip the
; errno/signal side effects (FP-exception *reporting* is deferred along with the
; rest of 8087-less FP; the returned VALUE is the correct IEEE special).
;
; Return convention (matches fstat086 F8RetInf / F8UnderFlow): the 64-bit double
; result is in AX:BX:CX:DX with AX = most-significant word. Entry AX carries the
; result sign in bit 15 (as produced by the __FDxemu callers).
;   overflow / divzero -> +/-Infinity = 0x7FF0_0000_0000_0000 (| sign)
;   underflow          -> 0.0

include langenv.inc
include mdef.inc

        modstart    fpsupport

        xdefp   F8UnderFlow
        xdefp   F8OverFlow
        xdefp   F8DivZero

;
;       F8UnderFlow( void ) : reallong   -- return 0.0
;
        defp    F8UnderFlow
        xor     ax,ax               ; result = 0.0 (all four words zero)
        xor     bx,bx               ; ...
        xor     cx,cx               ; ...
        xor     dx,dx               ; ...
        ret                         ; return
        endproc F8UnderFlow

;
;       F8DivZero( sign : int ) : reallong   -- return +/-Infinity
;       F8OverFlow( sign : int ) : reallong  -- return +/-Infinity
;
        defp    F8DivZero
        defp    F8OverFlow
        and     ax,8000h            ; keep only the sign bit
        or      ax,7ff0h            ; OR in the Infinity exponent (MSW = 7FF0|sign)
        xor     bx,bx               ; low 48 bits = 0
        xor     cx,cx               ; ...
        xor     dx,dx               ; ...
        ret                         ; return
        endproc F8OverFlow
        endproc F8DivZero

        endmod
        end
