        name    crt0cpp
        extrn   main_ : near
        extrn   wc_heap_init_ : near
        extrn   __CommonInit_ : near
        extrn   _edata : byte           ; wlink/dosseg: start of BSS class
        extrn   _end   : byte           ; wlink/dosseg: end of BSS class
        public  _cstart_
        public  _small_code_
_small_code_    equ     0
        public  __STK

; C++ startup for the CP/M-86 OW-clib port (rc7xx-work#9, rebased per #12).
; It is the C crt0sm.asm (wc_heap_init + __CommonInit -> __InitFiles for the REAL
; Watcom near-heap + stdio) EXTENDED with the C++ static ctor/dtor table walk.
;
; DGROUP must bracket the compiler-emitted static-init/fini tables XIB/XI/XIE and
; YIB/YI/YIE (class 'DATA'). The C++ compiler drops a 6-byte rt_init record for
; each global object's ctor into segment XI (and dtor into YI); _Start_XI/_End_XI
; (and _Start_YI/_End_YI) bracket them so __init_rtns/__fini_rtns can walk them.
; Without this walk, the iostream library's predefined cout/cin/cerr are never
; constructed -> the first `std::cout <<` dereferences an uninitialised streambuf
; and dies (observed on emu2: HALT / unimplemented-opcode on the real Watcom
; C++ runtime). Pure-C programs have an EMPTY XI (_Start_XI == _End_XI), so the
; walk is a no-op there -- but this crt0 is used ONLY by build-cpp.sh; the seven
; C targets keep the leaner port/crt0sm.asm.
DGROUP  group   BEGDATA, _DATA, XIB, XI, XIE, YIB, YI, YIE, _BSS, STACK

_TEXT   segment word public 'CODE'
        assume  cs:_TEXT, ds:DGROUP, ss:DGROUP
; Entry CS:0000. Loader already set DS=ES=data group + a 96-byte scratch stack.
; Move SS to DS and set SP to the top of our DGROUP stack; do NOT touch DS/ES.
_cstart_:
        mov     ax, ds
        mov     ss, ax
        mov     sp, offset DGROUP:stktop
; Zero-fill BSS: the CP/M-86 loader allocates G-Min but only writes G-Length
; from the file image; the gap is uninitialised BSS.  _edata/_end are the
; 'BSS'-class bounds wlink emits under `option dosseg` (see wlink loadfile.c
; GetBSSSize).  ES must point at DGROUP for stosb (AX already = DS here).
        mov     es, ax
        cld
        mov     di, offset DGROUP:_edata
        mov     cx, offset DGROUP:_end
        sub     cx, di
        xor     al, al
        rep     stosb
        call    wc_heap_init_           ; seed near heap FIRST (ctors + __InitFiles
                                        ; both allocate from it)
        call    __CommonInit_           ; __InitFiles: attach std FILE buffers so
                                        ; __iob[1] is valid before the cout ctor
                                        ; binds to it via __get_std_stream(1)
        call    __init_rtns             ; run C++/C static constructors (XI table)
        mov     ax, 1                   ; argc = 1 (harmless to arg-less C++ main)
        mov     dx, offset DGROUP:__argv
        call    main_
        call    __fini_rtns             ; run C++ static destructors (YI table)
        xor     dx, dx
        mov     cl, 0
        int     0E0h

; __init_rtns -- replicate Watcom __InitRtns(255): run every XI entry in
; ascending-priority order (0 = highest, runs first). A 16-bit small-model
; rt_init entry is 6 bytes:
;   +0 rtn_type (0=near,1=far,2=PDONE)  +1 priority  +2 rtn (near)  +4 pad
; No-op when XI is empty (_Start_XI == _End_XI).
__init_rtns:
i_outer:
        mov     bx, offset DGROUP:_End_XI
        mov     ah, 0FFh
        mov     si, offset DGROUP:_Start_XI
i_scan:
        cmp     si, offset DGROUP:_End_XI
        jae     i_pick
        cmp     byte ptr [si], 2
        je      i_next
        mov     al, [si+1]
        cmp     al, ah
        ja      i_next
        mov     ah, al
        mov     bx, si
i_next:
        add     si, 6
        jmp     i_scan
i_pick:
        cmp     bx, offset DGROUP:_End_XI
        je      i_done
        mov     ax, [bx+2]
        mov     byte ptr [bx], 2
        or      ax, ax
        jz      i_outer
        push    bx
        push    ds
        call    ax
        pop     ds
        pop     bx
        jmp     i_outer
i_done:
        ret

; __fini_rtns -- replicate Watcom __FiniRtns(0,255): run every YI entry in
; DESCENDING-priority order after main returns (reverse of construction, LIFO).
; Same 6-byte rt_init layout. No-op when YI is empty.
__fini_rtns:
f_outer:
        mov     bx, offset DGROUP:_End_YI
        xor     dl, dl
        mov     si, offset DGROUP:_Start_YI
f_scan:
        cmp     si, offset DGROUP:_End_YI
        jae     f_pick
        cmp     byte ptr [si], 2
        je      f_next
        mov     al, [si+1]
        cmp     al, dl
        jb      f_next
        mov     dl, al
        mov     bx, si
f_next:
        add     si, 6
        jmp     f_scan
f_pick:
        cmp     bx, offset DGROUP:_End_YI
        je      f_done
        mov     ax, [bx+2]
        mov     byte ptr [bx], 2
        or      ax, ax
        jz      f_outer
        push    bx
        push    ds
        call    ax
        pop     ds
        pop     bx
        jmp     f_outer
f_done:
        ret

; Watcom stack-overflow check helper -- no-op stub.
__STK:
        ret
_TEXT   ends

BEGDATA segment word public 'BEGDATA'
        db      100h dup(0)             ; base page area DS:0000-00FF
BEGDATA ends

_DATA   segment word public 'DATA'
        public  __argc
__argc  dw      1                       ; argc marker EXTRN'd by main's object
__argname db    'CPPTEST', 0
        public  __argv
__argv  dw      offset DGROUP:__argname ; argv[0]
        dw      0                       ; argv[1] = NULL
_DATA   ends

; --- C++ static ctor/dtor table brackets (mirror clib xiyi.asm) ---
XIB     segment word public 'DATA'
_Start_XI label byte
XIB     ends
XI      segment word public 'DATA'
XI      ends
XIE     segment word public 'DATA'
_End_XI label byte
XIE     ends
YIB     segment word public 'DATA'
_Start_YI label byte
YIB     ends
YI      segment word public 'DATA'
YI      ends
YIE     segment word public 'DATA'
_End_YI label byte
YIE     ends

_BSS    segment word public 'BSS'
_BSS    ends

STACK   segment word public 'STACK'
        db      512 dup(0)
stktop  label   word
STACK   ends
        end
