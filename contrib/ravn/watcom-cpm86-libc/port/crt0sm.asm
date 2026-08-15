        name    crt0sm
        extrn   main_ : near
        extrn   wc_heap_init_ : near
        extrn   __CommonInit_ : near
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
; ow#16: this minimal crt0 does not walk Watcom's XI init table, so run the C
; runtime initializers here (must be AFTER wc_heap_init -- __InitFiles allocates
; the stdout FILE buffer from the near heap). __CommonInit is macro-gated per
; build (see port/cominit.c): empty for the cprintf-only demos, __InitFiles for
; stdio builds, +__setEFGfmt for float-printing builds.
        call    __CommonInit_
; ow#3 streamio: Watcom's main(argc,argv) is __watcall -- argc in AX, argv in DX
; -- and the object also EXTRNs the global __argc marker. We have no command
; tail parser, so synthesise argv = { "IOTEST", NULL }: enough for the streamio
; clibtest's strlwr(argv[0]) banner. Harmless to arg-less mains in other builds.
        mov     ax, 1                   ; argc = 1
        mov     dx, offset DGROUP:__argv
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
        public  __argc
__argc  dw      1                       ; argc marker EXTRN'd by main's object
__argname db    'IOTEST', 0
        public  __argv
__argv  dw      offset DGROUP:__argname ; argv[0]
        dw      0                       ; argv[1] = NULL
_DATA   ends

STACK   segment word public 'STACK'
        db      512 dup(0)
stktop  label   word
STACK   ends
        end
