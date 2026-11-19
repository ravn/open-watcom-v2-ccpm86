        name    cstartcpm
; ---------------------------------------------------------------------------
; cstartcpm.asm -- CP/M-86 C startup for Open Watcom's "owcc -bcpm86" target.
;
; This is the object the wlink "system begin cpm86" block pulls in via
; `libfile cstartcpm.obj` (see bld/wl/lnk/specs.sp).  It is ALWAYS linked, so it
; carries the program entry (_cstart_), the small-model marker (_small_code_)
; and Watcom's stack-probe stub (__STK); the C library proper (clibs.lib) is
; auto-fetched by wlink from the object's CMT_DEFAULT_LIBRARY "clibs" record.
;
; CP/M-86 loader contract (small / "8080" .CMD model):
;   * loader enters at CS:0000 with DS = ES = the program's data group and a
;     small scratch stack already set up;
;   * we relocate SS to our own DGROUP stack, call the C main, then terminate
;     through BDOS System Reset (function 0: INT 0E0h with CL=0).
; No heap, no stdio init: a freestanding main gets a clean stack and an exit.
; ---------------------------------------------------------------------------
        extrn   main_ : near
        public  _cstart_
        public  _small_code_
        public  __STK
_small_code_    equ     0

DGROUP  group   BEGDATA, _DATA, STACK

_TEXT   segment word public 'CODE'
        assume  cs:_TEXT, ds:DGROUP, ss:DGROUP
_cstart_:
        mov     ax, ds
        mov     ss, ax
        mov     sp, offset DGROUP:stktop
        call    main_
        xor     dx, dx
        mov     cl, 0                   ; BDOS 0 = System Reset (terminate)
        int     0E0h
; Watcom emits "call __STK" as a stack-depth probe at function entry; our own
; startup owns the stack, so a bare RET is the correct no-op here.
__STK:
        ret
_TEXT   ends

BEGDATA segment word public 'BEGDATA'
        db      100h dup(0)             ; base-page area DS:0000-00FF
BEGDATA ends

_DATA   segment word public 'DATA'
_DATA   ends

STACK   segment word public 'STACK'
        db      512 dup(0)
stktop  label   word
STACK   ends
        end     _cstart_
