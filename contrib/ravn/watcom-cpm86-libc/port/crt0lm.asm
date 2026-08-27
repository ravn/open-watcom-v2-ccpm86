        name    crt0lm
; ---------------------------------------------------------------------------
; crt0lm.asm -- CP/M-86 LARGE-model ("FAR code, FAR data") C startup.
;
; Companion to crt0sm (small), crt0mm (medium), crt0cm (compact); used for
; programs compiled with `owcc -bcpm86 -mcmodel=l` (== `wcc -ml`). Large model
; is the UNION of medium and compact:
;   * FAR CODE (like medium): every C function is a FAR proc (`retf`), every
;     inter-module call is `call far ptr`, and -ml objects EXTRN the code-model
;     marker `_big_code_`. So this startup's OWN body is byte-for-byte crt0mm's:
;     far calls to main_/wc_heap_init_/__CommonInit_, far __STK, `_big_code_`.
;   * FAR DATA (like compact): -ml defines __BIG_DATA__, which flips Watcom's
;     fmalloc.c so plain malloc()/calloc()/realloc()/free() redirect to the FAR
;     heap (_fmalloc, port/farheap.c, reserved by the linker's
;     `option farheap=<size>`), and CONST/string literals may live in their own
;     FAR segments outside DGROUP. NONE of that far-data machinery needs an
;     instruction HERE -- it is entirely in the C library + the loader-filled
;     Extra-group base paragraph (exactly crt0cm's note). The near DGROUP
;     symbols this startup defines (__argc/__argv/_argvtab/_prog0) resolve fine
;     when a -ml clib object references them via a far pointer: the address is
;     just {DGROUP, offset}, same as compact.
;
; WHY large (not medium) for the motivating deliverable (Info-ZIP `zip`): zip's
; pristine source prototypes flush_block as `char far *` but K&R-defines it as
; `char *`; only a FAR-DATA model makes those agree (E1129 otherwise), and the
; source may not be edited. See reference_zip_cpm86_needs_large_model.md.
;
; CP/M-86 loader contract (large .CMD): entry at CS:0000 with DS=ES=data group
; and a small scratch stack. `_cstart_` sits in `BEGTEXT` which `option dosseg`
; keeps FIRST within the CODE class, so it lands at code-group offset 0 = the
; loader's entry point EVEN WHEN pulled from clibl.lib as a library member.
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
; Model marker: -ml C objects EXTRN _big_code_ (far); its address value is
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
; allocates the stdout FILE buffer from the heap). __CommonInit is macro-gated
; per build (see port/cominit.c).
        call    far ptr __CommonInit_
; Watcom's main(argc,argv) is __watcall. In the LARGE (far-data) model argc is
; still in AX, but `char **argv` is a FAR pointer passed as offset in BX and
; SEGMENT in CX (verified by disassembling a -ml main: `mov di,bx` / `mov ds,cx`
; to reach argv, then `[di]`/`2[di]` to read each entry). Crucially each argv[i]
; is itself a 4-byte FAR `char *`, so _argvtab slots are DWORD {offset,segment}.
; The tokens and _prog0 all live in DGROUP (base page + _DATA), so every entry's
; segment is just the runtime DS. Parse the command tail (base page DS:0080h =
; length, DS:0081h.. = chars, space-separated) into that far argv vector.
        mov     di, offset DGROUP:_argvtab
        mov     ax, offset DGROUP:_prog0
        mov     [di], ax                ; argv[0].offset = program name
        mov     ax, ds
        mov     word ptr 2[di], ax      ; argv[0].segment = DGROUP (DS)
        add     di, 4
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
        mov     [di], si                ; argv[argc].offset = token start
        mov     ax, ds
        mov     word ptr 2[di], ax      ; argv[argc].segment = DS
        add     di, 4
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
        mov     word ptr [di], 0        ; argv[argc] = NULL far pointer
        mov     word ptr 2[di], 0       ;   (both words zero)
        mov     word ptr ds:__argc, bx  ; publish argc to the marker
        mov     ax, bx                  ; argc -> AX (__watcall arg 1)
        mov     bx, offset DGROUP:_argvtab  ; argv offset -> BX
        mov     cx, ds                  ; argv segment -> CX (far arg 2)
; ow: apply shell-style stdin/stdout redirection (< > >>) on the command tail.
; __CommonRedirect scans argv and returns the surviving argc in AX (a no-op
; returning argc unless the disk layer is compiled in). BX/CX are argument
; registers, so reload the far argv pointer before calling main.
        call    far ptr __CommonRedirect_
        mov     bx, offset DGROUP:_argvtab  ; argv offset -> BX (reload)
        mov     cx, ds                  ; argv segment -> CX (reload)
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
_prog0  db      'ZIP', 0                ; argv[0]
        public  __argv
__argv  label   word                    ; keep the public symbol some objs EXTRN
; Far-data model: each argv[] slot is a 4-byte FAR char* {offset,segment}.
; 32 tokens + argv[0] + NULL terminator = 34 slots * 2 words.
_argvtab dw     68 dup(0)               ; argv[]: far pointers
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
