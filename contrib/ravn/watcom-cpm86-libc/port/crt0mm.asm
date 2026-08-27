        name    crt0mm
; ---------------------------------------------------------------------------
; crt0mm.asm -- CP/M-86 MEDIUM-model ("far code, near data") C startup.
;
; Companion to the small-model crt0sm.asm; used for programs compiled with
; `owcc -bcpm86 -mcmodel=m` (== `wcc -mm`), whose code may span >64 KB across
; multiple per-function `*_TEXT` segments coalesced by wlink into ONE CP/M-86
; Code Group Descriptor (Stage B, see tasks/plan-cpm86-big-model-2026-08-18.md).
;
; What differs from small model (verified 2026-08-19 by disassembling a
; `wcc -bt=dos -mm -zm` object with `wdis -a`):
;   * every C function is a FAR proc terminated by `retf`, and every inter-
;     module call is `call far ptr <name>` -- so this crt0's own calls to
;     main_ / wc_heap_init_ / __CommonInit_ MUST be far, and __STK (the
;     compiler's stack-probe stub, emitted as `call far ptr __STK`) MUST be a
;     far proc;
;   * `-mm` objects EXTRN the model marker `_big_code_` (bld/clib/startup/a/
;     cmodel.asm: `_big_code_ label far`), NOT `_small_code_` -- so we publish
;     `_big_code_` here to satisfy Watcom's code-model-mismatch link check.
;
; Data stays NEAR: DGROUP (BEGDATA + _DATA + STACK, plus the C objects' CONST/
; CONST2/_BSS) is one <=64 KB group exactly as in small model -- the base-page
; reservation, DGROUP stack, and command-tail argv parser are unchanged.
;
; CP/M-86 loader contract (medium .CMD -- verified from CCP/M-86 kern/load.sup
; init_base): entry at CS:0000 with DS=ES=data group and a small scratch stack.
; `_cstart_` sits in `BEGTEXT` which `option dosseg` keeps FIRST within the CODE
; class, so it lands at code-group offset 0 = the loader's entry point EVEN WHEN
; this startup is pulled from clibm.lib as a library member (no `libfile`), the
; same way Open Watcom's 16-bit `system dos` target auto-selects its startup.
; ---------------------------------------------------------------------------
        extrn   main_ : far
        extrn   wc_heap_init_ : far
        extrn   __CommonInit_ : far
        extrn   __CommonRedirect_ : far
        extrn   __CommonRedirectClose_ : far
        public  _cstart_
        public  _big_code_
        public  __STK

DGROUP  group   BEGDATA, _DATA, STACK

BEGTEXT segment word public 'CODE'
        assume  cs:BEGTEXT, ds:DGROUP, ss:DGROUP
; Model marker: `-mm` C objects EXTRN _big_code_ (far); its address value is
; irrelevant -- the linker only checks that exactly one model marker exists.
_big_code_      label   far
; Entry CS:0000. Loader already set DS=ES=data group + a small scratch stack.
; Do NOT touch DS/ES; move SS to DS and set SP to the top of our DGROUP stack.
_cstart_:
        mov     ax, ds
        mov     ss, ax
        mov     sp, offset DGROUP:stktop
        call    far ptr wc_heap_init_
; Run the C runtime initializers (must be AFTER wc_heap_init -- __InitFiles
; allocates the stdout FILE buffer from the near heap). __CommonInit is
; macro-gated per build (see port/cominit.c).
        call    far ptr __CommonInit_
; Watcom's main(argc,argv) is __watcall -- argc in AX, argv in DX. Parse the
; CP/M-86 command tail (base page DS:0080h = length, DS:0081h.. = chars,
; space-separated) into a real argv vector. Identical to small model: argv
; and the base page are NEAR data, unaffected by far code.
        mov     di, offset DGROUP:_argvtab
        mov     ax, offset DGROUP:_prog0
        mov     [di], ax                ; argv[0] = program name
        add     di, 2
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
        mov     [di], si                ; argv[argc] = token start
        add     di, 2
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
        mov     word ptr [di], 0        ; argv[argc] = NULL
        mov     word ptr ds:__argc, bx  ; publish argc to the marker
        mov     ax, bx                  ; argc -> AX (__watcall)
        mov     dx, offset DGROUP:_argvtab   ; argv -> DX
; ow: apply shell-style stdin/stdout redirection (< > >>) on the command tail.
; __CommonRedirect scans argv, opens the redirect files, compacts argv in place
; and returns the surviving argc in AX (no-op in builds without the disk layer).
; DX is call-clobbered, so reload the argv pointer before calling main.
        call    far ptr __CommonRedirect_
        mov     dx, offset DGROUP:_argvtab   ; argv -> DX (reload after the call)
        call    far ptr main_
; ow: flush + commit any redirected stdout file before the CP/M system reset.
        push    ax
        call    far ptr __CommonRedirectClose_
        pop     ax
        mov     dh, 0
        mov     dl, al
        mov     cl, 0                   ; BDOS 0 = System Reset (terminate)
        int     0E0h
; Watcom emits `call far ptr __STK` as a stack-depth probe at each function
; entry; our own startup owns the stack, so a bare far RET is the correct no-op.
__STK:
        retf
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
_argvtab dw     33 dup(0)               ; argv[]: 32 slots + NULL terminator
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
