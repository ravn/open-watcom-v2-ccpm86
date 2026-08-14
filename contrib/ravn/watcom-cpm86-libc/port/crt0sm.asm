        name    crt0sm
        extrn   main_ : near
        extrn   wc_heap_init_ : near
        public  _cstart_
        public  _small_code_
_small_code_    equ     0
        public  __STK

DGROUP  group   BEGDATA, _DATA, STACK

_TEXT   segment word public 'CODE'
        assume  cs:_TEXT, ds:DGROUP, ss:DGROUP
; Entry CS:0000. Loader already set DS=ES=data group + a 96-byte scratch stack.
; Do NOT touch DS/ES; just move SS to DS and set SP to the top of our DGROUP stack.
_cstart_:
        mov     ax, ds
        mov     ss, ax
        mov     sp, offset DGROUP:stktop
        call    wc_heap_init_
        call    main_
        xor     dx, dx
        mov     cl, 0
        int     0E0h
; Watcom stack-overflow check helper — no-op stub (no clib on CP/M-86 yet)
__STK:
        ret
_TEXT   ends

BEGDATA segment word public 'BEGDATA'
        db      100h dup(0)             ; base page area DS:0000-00FF
BEGDATA ends

_DATA   segment word public 'DATA'
_DATA   ends

STACK   segment word public 'STACK'
        db      512 dup(0)
stktop  label   word
STACK   ends
        end
