        name    crt0cm
; ---------------------------------------------------------------------------
; crt0cm.asm -- CP/M-86 COMPACT-model ("near code, FAR data") C startup.
;
; Companion to crt0sm.asm (small) and crt0mm.asm (medium); used for programs
; compiled with `owcc -bcpm86 -mcmodel=c` (== `wcc -mc`). Compact model keeps
; ONE <=64 KB code segment (near calls, exactly like small model) but makes
; DATA far: the compiler defines __BIG_DATA__, which flips Watcom's own
; bld/clib/heap/c/fmalloc.c so that plain malloc()/calloc()/realloc()/free()
; redirect to _fmalloc()/_fcalloc()/_frealloc()/_ffree() -- the FAR heap.
;
; Why this model exists here (the motivating deliverable): INFO-Zip UnZip's
; DEFLATE path needs a 32 KB inflate window + huft tables that will not fit in
; the single 64 KB small-model DGROUP alongside UnZip's ~22 KB of message
; strings. Compact model moves BOTH off DGROUP: (1) the big allocations become
; far-heap segments carved from the .CMD's Extra group (port/farheap.c's
; __AllocSeg, reserved by the linker's `option farheap=<size>`), and (2) far
; CONST/string literals land in their own FAR_DATA segments outside DGROUP.
; That is precisely the ~35 KB of DGROUP headroom DEFLATE was short of.
;
; What differs from small model: NOTHING in this startup's own code. Compact
; model is NEAR code, so the calls below to main_ / wc_heap_init_ /
; __CommonInit_ stay near and `_small_code_ equ 0` is the correct code-model
; marker (identical to crt0sm.asm). DGROUP (BEGDATA + _DATA + STACK, plus the C
; objects' near CONST/_BSS) is still one <=64 KB near group, so the base-page
; reservation, the DGROUP stack, and the command-tail argv parser are byte-for-
; byte the same as small model. The far-data machinery is entirely in the C
; library (fmalloc.c compiled with __BIG_DATA__) and the loader-filled Extra
; group base paragraph at DS:000F..0010 (read by port/farheap.c) -- neither
; needs a single instruction here.
;
; CP/M-86 loader contract (.CMD): entry at CS:0000 with DS=ES=data group and a
; small scratch stack. `_cstart_` sits in `BEGTEXT` which `option dosseg` keeps
; FIRST within the CODE class, so it lands at code-group offset 0 = the loader's
; entry point EVEN WHEN this startup is pulled from clibc.lib as a library
; member (no `libfile`), the same way Open Watcom's 16-bit `system dos` target
; auto-selects its startup.
; ---------------------------------------------------------------------------
        extrn   main_ : near
        extrn   wc_heap_init_ : near
        extrn   __CommonInit_ : near
        extrn   __CommonRedirect_ : near
        extrn   __CommonRedirectClose_ : near
        public  _cstart_
        public  _small_code_
_small_code_    equ     0
        public  __STK

DGROUP  group   BEGDATA, _DATA, STACK

BEGTEXT segment word public 'CODE'
        assume  cs:BEGTEXT, ds:DGROUP, ss:DGROUP
; Entry CS:0000. Loader already set DS=ES=data group + a small scratch stack.
; Do NOT touch DS/ES; move SS to DS and set SP to the top of our DGROUP stack.
; ES stays == DGROUP; the compiler reloads ES per far-data access as needed.
_cstart_:
        mov     ax, ds
        mov     ss, ax
        mov     sp, offset DGROUP:stktop
        call    wc_heap_init_
; Run the C runtime initializers (must be AFTER wc_heap_init -- __InitFiles
; allocates the stdout FILE buffer). __CommonInit is macro-gated per build
; (see port/cominit.c).
        call    __CommonInit_
; Watcom's main(argc,argv) is __watcall. In COMPACT model (far DATA) a `char *`
; is a FAR pointer, so each argv[] entry is 4 bytes (offset,segment) and argv is
; passed as a FAR pointer in CX:BX (segment CX, offset BX) -- NOT small model's
; near DX. The strings (program name in _prog0, tokens in the base page) all live
; in DGROUP == DS, so every argv[i] segment word is simply DS. Parse the CP/M-86
; command tail (base page DS:0080h = length, DS:0081h.. chars, space-separated)
; into that far argv vector.
        mov     di, offset DGROUP:_argvtab
        mov     ax, offset DGROUP:_prog0
        mov     [di], ax                ; argv[0] offset = program name
        mov     [di+2], ds              ; argv[0] segment = DGROUP
        add     di, 4                   ; far-pointer stride
        mov     bx, 1                   ; argc = 1
        mov     si, 81h                 ; first command-tail character
        mov     cl, byte ptr ds:[80h]   ; tail length
        xor     ch, ch
        mov     bp, si
        add     bp, cx                  ; bp = one past last tail char
ct_skip:
        cmp     si, bp
        jae     ct_done                 ; end of tail -> stop
        cmp     byte ptr [si], ' '
        jne     ct_tok                  ; non-space starts a token
        inc     si
        jmp     ct_skip
ct_tok:
        cmp     bx, 32                  ; argv table guard (32 slots)
        jae     ct_done
        mov     [di], si                ; argv[argc] offset = token start
        mov     [di+2], ds              ; argv[argc] segment = DGROUP
        add     di, 4                   ; far-pointer stride
        inc     bx
ct_scan:
        cmp     si, bp
        jae     ct_end                  ; token runs to end of tail
        cmp     byte ptr [si], ' '
        je      ct_cut                  ; space ends the token
        inc     si
        jmp     ct_scan
ct_cut:
        mov     byte ptr [si], 0        ; terminate token in place
        inc     si
        jmp     ct_skip
ct_end:
        mov     byte ptr [si], 0        ; terminate final token (within base page)
ct_done:
        mov     word ptr [di], 0        ; argv[argc] = NULL (far ptr = 0:0)
        mov     word ptr [di+2], 0
        mov     word ptr ds:__argc, bx  ; publish argc to the marker
        mov     ax, bx                  ; argc -> AX (__watcall)
        mov     bx, offset DGROUP:_argvtab   ; argv offset -> BX
        mov     cx, ds                       ; argv segment -> CX (far ptr CX:BX)
; ow: apply shell-style stdin/stdout redirection (< > >>) on the command tail.
; __CommonRedirect scans argv, opens the redirect files, compacts argv in place
; and returns the surviving argc in AX (no-op in builds without the disk layer).
; CX:BX are call-clobbered, so reload the far argv pointer before calling main.
        call    __CommonRedirect_
        mov     bx, offset DGROUP:_argvtab   ; argv offset -> BX (reload)
        mov     cx, ds                       ; argv segment -> CX (reload)
        call    main_
; ow: flush + commit any redirected stdout file before the CP/M system reset.
        push    ax
        call    __CommonRedirectClose_
        pop     ax
        mov     dh, 0
        mov     dl, al
        mov     cl, 0                   ; BDOS 0 = System Reset (terminate)
        int     0E0h
; Watcom emits `call __STK` as a stack-depth probe; our startup owns the stack,
; so a bare near RET is the correct no-op (compact = near code).
__STK:
        ret
BEGTEXT ends

BEGDATA segment word public 'BEGDATA'
        db      100h dup(0)             ; base page area DS:0000-00FF
BEGDATA ends

_DATA   segment word public 'DATA'
        public  __argc
__argc  dw      1                       ; argc marker EXTRN'd by main's object
_prog0  db      'UNZIP', 0              ; argv[0]
        public  __argv
__argv  label   word                    ; keep the public symbol some objs EXTRN
_argvtab dd     33 dup(0)               ; argv[]: 32 FAR-pointer slots + NULL (compact: 4 B each)
_DATA   ends

; Stack size: 512 B overflows deep call chains (see crt0sm.asm for the UnZip
; DEFLATE >=32 KB flush corruption this caused). Default 2 KB; override with
; -DWC_STACK_BYTES=<n> (build-lib.sh honors WC_STACK_BYTES).
ifndef WC_STACK_BYTES
WC_STACK_BYTES equ 2048
endif
STACK   segment word public 'STACK'
        db      WC_STACK_BYTES dup(0)
stktop  label   word
STACK   ends
        end     _cstart_
