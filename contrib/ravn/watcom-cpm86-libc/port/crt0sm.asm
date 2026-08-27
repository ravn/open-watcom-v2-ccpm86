        name    crt0sm
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

; The entry lives in BEGTEXT (not _TEXT). Under `option dosseg` wlink keeps the
; 'BEGTEXT' segment in front of every other CODE-class segment, so `_cstart_`
; lands at code-group offset 0 (= the CP/M-86 loader's fixed CS:0000 entry, as
; .CMD has no entry-point field) EVEN WHEN THIS MODULE IS PULLED FROM A LIBRARY
; rather than force-included via `libfile`. That is what lets the CP/M-86
; startup be a plain member of clibs.lib/clibm.lib, auto-selected by the
; object's default-library record exactly like Open Watcom's 16-bit `system
; dos` target (which has NO `libfile` in its linker system block). In small
; model BEGTEXT and _TEXT coalesce into the one CODE group, so the near calls
; below to main_/wc_heap_init_/__CommonInit_ (in _TEXT) resolve within it.
BEGTEXT segment word public 'CODE'
        assume  cs:BEGTEXT, ds:DGROUP, ss:DGROUP
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
; ow#3 streamio: Watcom's main(argc,argv) is __watcall -- argc in AX, argv in DX.
; Parse the CP/M-86 command tail (base page DS:0080h = length byte, DS:0081h.. =
; characters, space-separated) into a real argv vector so hosted programs like
; UnZip see their operands.  argv[0] is a fixed program name; tokens are
; NUL-terminated in place (we own the base-page scratch) and their offsets stored
; in _argvtab.  Example: tail " -l TEST.ZIP" -> argc=3,
; argv={"UNZIP","-l","TEST.ZIP",NULL}.
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
; ow: apply shell-style stdin/stdout redirection (< > >>) present on the command
; tail. __CommonRedirect scans argv, opens the redirect files, compacts argv in
; place (dropping the redirect operands) and returns the surviving argc in AX.
; It is an argc-preserving no-op in builds without the disk layer. DX is call-
; clobbered, so reload the argv pointer before calling main.
        call    __CommonRedirect_
        mov     dx, offset DGROUP:_argvtab   ; argv -> DX (reload after the call)
        call    main_
; ow: flush + commit any redirected stdout file before the CP/M system reset.
; Preserve main()'s return value (AX) across the close call so it can be
; examined by the BDOS termination call.  CP/M-86 fn 0 (P_TERM) ignores DX,
; but passing the exit code there preserves it for any future OS that does.
        push    ax
        call    __CommonRedirectClose_
        pop     ax
        mov     dh, 0
        mov     dl, al                  ; exit code in DL (low byte of return val)
        mov     cl, 0
        int     0E0h
; Watcom stack-overflow check helper — no-op stub (no clib on CP/M-86 yet)
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
_argvtab dw     33 dup(0)               ; argv[]: 32 slots + NULL terminator
_DATA   ends

; Stack size. 512 bytes is far too small for deep call chains: INFO-Zip UnZip's
; DEFLATE path nests inflate -> inflate_block -> inflate_dynamic -> inflate_codes
; -> flush -> write -> __qwrite -> load_record -> _bdos (~13 frames), overflowing
; a 512-byte stack DOWN into the near-heap arena (wc_arena sits just below STACK
; in DGROUP). That corrupted write()'s saved return address, so returning from a
; 32 KB window flush did a wild far jump (printed the " April 2009" version
; far-string). Any file whose inflated size >= WSIZE (32768) triggered it; STORED
; (shallow flush) was unaffected. Default is now 2 KB; override with
; -DWC_STACK_BYTES=<n> at assembly time (build-lib.sh honors WC_STACK_BYTES).
ifndef WC_STACK_BYTES
WC_STACK_BYTES equ 2048
endif
; Stack-overflow canary. The stack is pre-painted with WC_STACK_FILL and grows
; DOWN from stktop, so any byte the program never pushes to keeps the fill. The
; stkfree() helper below counts the intact fill bytes from the bottom (stkbot)
; upward: that count is the stack HEADROOM (bytes never used); 0 means the stack
; was driven all the way to its floor -- i.e. it overflowed into the arena just
; below (exactly the >=32 KB UnZip DEFLATE crash). Precise detection needs a
; distinctive, rarely-pushed fill: build the diagnostic clib with
; -DWC_STACK_FILL=0A5h (default 0 keeps production stack contents unchanged and
; still gives a usable estimate, since real pushes -- return addresses, saved
; BP -- are almost never zero at the very bottom).
ifndef WC_STACK_FILL
WC_STACK_FILL equ 0
endif
STACK   segment word public 'STACK'
        public  stkbot
stkbot  label   word
        db      WC_STACK_BYTES dup(WC_STACK_FILL)
        public  stktop
stktop  label   word
STACK   ends

BEGTEXT segment word public 'CODE'
        assume  cs:BEGTEXT, ds:DGROUP, ss:DGROUP
; unsigned stkfree(void) -- __watcall, result in AX. Scans the STACK segment
; from stkbot up while the byte still equals the WC_STACK_FILL sentinel and
; returns the number of untouched bytes (headroom). 0 => stack overflow.
; SS==DS==DGROUP in small model, so the STACK bytes are addressable via DS.
        public  stkfree_
stkfree_:
        push    si
        mov     si, offset DGROUP:stkbot
        xor     ax, ax
sf_loop:
        cmp     si, offset DGROUP:stktop
        jae     sf_done
        cmp     byte ptr [si], WC_STACK_FILL
        jne     sf_done
        inc     ax
        inc     si
        jmp     sf_loop
sf_done:
        pop     si
        ret
BEGTEXT ends
        end     _cstart_
