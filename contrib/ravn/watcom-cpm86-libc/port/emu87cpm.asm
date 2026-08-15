; port/emu87cpm.asm -- CP/M-86 variant of Open Watcom's fpuemu/i86/asm/initemu.asm
; (rc7xx-work#8). IDENTICAL to the stock file EXCEPT the xchg_vects proc, whose
; DOS INT 21h get/set-vector calls are replaced by a direct segment-0 IVT swap
; (INT 21h is fatal here). Everything else -- the FIxRQQ emulator entry constants,
; the i34off..i3doff table, __init_87_emulator/__fini_87_emulator, the __int3d /
; __patch34/3c/3d handlers, and the XI/YI init-table registration -- is verbatim
; Watcom. The compute engine (__int34/__int3c/__x87id/__init_8087_emu) is the
; UNCHANGED bld/fpuemu/i86/asm/emu8087.asm, linked alongside. See
; docs/FLOAT_8087_EMULATOR.md.
;
; !!!!! must be assembled with the same options as initemu.asm (-fpi87 model) !!!!!

include langenv.inc
include mdef.inc
include struct.inc

.8087
public  FJSRQQ
FJSRQQ  equ             08000H
public  FISRQQ
FISRQQ  equ             00632H
public  FIERQQ
FIERQQ  equ             01632H
public  FIDRQQ
FIDRQQ  equ             05C32H
public  FIWRQQ
FIWRQQ  equ             0A23DH
public  FJCRQQ
FJCRQQ  equ             0C000H
public  FJARQQ
FJARQQ  equ             04000H
public  FICRQQ
FICRQQ  equ             00E32H
public  FIARQQ
FIARQQ  equ             0FE32H

DGROUP  group   _DATA
        assume  ds:DGROUP

_DATA   segment word public 'DATA'
        extrn   __8087          : byte
        extrn   __real87        : byte
        extrn   __no87          : byte
        extrn   __dos87emucall  : word
        extrn   __dos87real     : byte

i34off  dw      0
i34seg  dw      0
i35off  dw      0
i35seg  dw      0
i36off  dw      0
i36seg  dw      0
i37off  dw      0
i37seg  dw      0
i38off  dw      0
i38seg  dw      0
i39off  dw      0
i39seg  dw      0
i3aoff  dw      0
i3aseg  dw      0
i3boff  dw      0
i3bseg  dw      0
i3coff  dw      0
i3cseg  dw      0
i3doff  dw      0
i3dseg  dw      0
_DATA   ends

_TEXT segment word public 'CODE'

        extrn   __init_8087_emu : near

        extrn   __int34         : near
        extrn   __int3c         : near
        extrn   __x87id         : near
        extrn   ___dos87emucall : near

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;      void _init_87_emulator( void )
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

xchg_vects      proc near
        push    ax                      ; save regs
        push    bx                      ; ...
        push    cx                      ; ...
        push    si                      ; ...
        push    di                      ; ...
        push    es                      ; ...
        xor     ax,ax                   ; ES -> segment 0 (the IVT)
        mov     es,ax                   ; ...
        lea     si,i34off               ; DS:SI -> our {off,seg} x10 table
        mov     di,34H*4                ; ES:DI -> IVT slot for INT 34H (0D0H)
        mov     cx,20                   ; 20 words = 10 vectors (off+seg each)
swapw:  mov     ax,es:[di]              ; - old IVT word
        mov     bx,[si]                 ; - our new word
        mov     es:[di],bx              ; - install new into IVT
        mov     [si],ax                 ; - stash old back into our table
        add     si,2                    ; - next word
        add     di,2                    ; - ...
        loop    swapw                   ; until all 10 vectors swapped
        pop     es                      ; restore regs
        pop     di                      ; ...
        pop     si                      ; ...
        pop     cx                      ; ...
        pop     bx                      ; ...
        pop     ax                      ; ...
        ret                             ; return to caller
xchg_vects      endp

;       __no87 is not 0 if NO87 environment variable is present

public  __init_87_emulator
__init_87_emulator proc near
        push    bx                      ; save bx
        call    __x87id
        mov     __dos87real,al          ; set installed 80x87
        mov     __real87,al             ; set real 80x87 used
        mov     __8087,al               ; set 80x87
        mov     bl,__no87               ; get state of NO87 environment var
        test    al,al                   ; coprocessor is present
        _if     e                       ; if no coprocessor
          inc   bl                      ; - pretend NO87 was set
        _endif                          ; endif
        test    bl,bl                   ; if no 80x87 or no87 set
        _if     ne                      ; then
          mov   __dos87emucall, ___dos87emucall ; set pointer for DOS EMU control
          mov   __real87,0              ; - no real 80x87
          mov   __8087,3                ; - set 80387
          mov   ax,offset __int34       ; - emulate instructions
          mov   i3coff,offset __int3c   ; - ...
          mov   i3doff,offset __int3d   ; - ...
        _else                           ; else
          mov   ax,offset __patch34     ; - patch instructions
          mov   i3coff,offset __patch3c ; - ...
          mov   i3doff,offset __patch3d ; - ...
        _endif                          ; endif
        mov     i34seg,cs               ; set up rest of table
        mov     i34off,ax               ; ...
        mov     i35seg,cs               ; ...
        mov     i35off,ax               ; ...
        mov     i36seg,cs               ; ...
        mov     i36off,ax               ; ...
        mov     i37seg,cs               ; ...
        mov     i37off,ax               ; ...
        mov     i38seg,cs               ; ...
        mov     i38off,ax               ; ...
        mov     i39seg,cs               ; ...
        mov     i39off,ax               ; ...
        mov     i3aseg,cs               ; ...
        mov     i3aoff,ax               ; ...
        mov     i3bseg,cs               ; ...
        mov     i3boff,ax               ; ...
        mov     i3cseg,cs               ; ...
        mov     i3dseg,cs               ; ...
        call    xchg_vects              ; set up vectors
        call    __init_8087_emu         ; initialize real 80x87 and 80x87 EMU
        pop     bx                      ; restore bx
        ret                             ; return to caller
__init_87_emulator endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;      void _fini_87_emulator( void )
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

public __fini_87_emulator
__fini_87_emulator proc near
        mov     word ptr __dos87emucall,0
        call    xchg_vects
        ret
__fini_87_emulator endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;       interrupt int3d()               FWAIT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

__int3d proc    near
        sti                             ; interrupts back on
        push    si                      ; save some registers
        push    ds                      ; ...
        push    bp                      ; ...
        mov     bp,sp                   ; set up stack frame
        lds     si,6[bp]                ; point es:si at return address
        dec     si                      ; point at the 3D
        dec     si                      ; point to the INT instruction
        mov     word ptr [si],09090H    ; zap it with NOPs
        pop     bp                      ; restore some registers
        pop     ds                      ; ...
        pop     si                      ; ...
        iret                            ; return from interrupt
__int3d endp


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;      void patch34()
;;      - turn an int 34H instruction back into a real 8087 instruction
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

__patch34 proc  near
        sti                             ; interrupts back on
        push    si                      ; save some registers
        push    ds                      ; ...
        push    bp                      ; ...
        mov     bp,sp                   ; set up stack frame
        lds     si,6[bp]                ; point ds:si at return address
        dec     si                      ; back up one byte
        add     byte ptr [si],0A4H      ; turn int number into opcode
        dec     si                      ; point to int instruction
        mov     byte ptr [si],09BH      ; insert FWAIT instruction
        mov     6[bp],si                ; zap our return address
        pop     bp                      ; restore some registers
        pop     ds                      ; ...
        pop     si                      ; ...
        iret                            ; return from interrupt
__patch34 endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;      void patch3c()
;;      - Turn an int 3CH instruction into an 8087 instruction with an
;;        appropriate segment override
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
__patch3c proc near
        sti                             ; interrupts back on
        push    si                      ; save some registers
        push    bx                      ; ...
        push    ds                      ; ...
        push    bp                      ; ...
        mov     bp,sp                   ; set up stack frame
        lds     si,8[bp]                ; point ds:si at return address
        mov     bl,[si]                 ; get opcode into bl
        or      byte ptr [si],0C0H      ; fix high two bits of opcode
        not     bl                      ; not the opcode
        and     bl,0C0H                 ; keep original top two bits
        shr     bl,1                    ; move into seg override posn
        shr     bl,1                    ; ...
        shr     bl,1                    ; ...
        or      bl,026H                 ; turn into a segment override
        dec     si                      ; point to the 3C
        mov     [si],bl                 ; zap it with segment override
        dec     si                      ; point to the INT instruction
        mov     byte ptr [si],09BH      ; zap it with an FWAIT
        mov     8[bp],si                ; zap our return address
        pop     bp                      ; restore some registers
        pop     ds                      ; ...
        pop     bx                      ; ...
        pop     si                      ; ...
        iret                            ; return from interrupt
__patch3c endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;      void patch3d()
;;      - Turn an int 3DH instruction into FWAIT // NOP
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
__patch3d proc near
        sti                             ; interrupts back on
        push    si                      ; save some registers
        push    ds                      ; ...
        push    bp                      ; ...
        mov     bp,sp                   ; set up stack frame
        lds     si,6[bp]                ; point ds:si at return address
        dec     si                      ; point at the 3D
        dec     si                      ; point to the INT instruction
        mov     word ptr [si],0909BH    ; zap it with an FWAIT // NOP
        mov     6[bp],si                ; zap our return address
        pop     bp                      ; restore some registers
        pop     ds                      ; ...
        pop     si                      ; ...
        iret                            ; return from interrupt
__patch3d endp

_TEXT   ends


include xinit.inc

        xinitn  __init_87_emulator, INIT_PRIORITY_FPU
        xfinin  __fini_87_emulator, INIT_PRIORITY_FPU

        end
