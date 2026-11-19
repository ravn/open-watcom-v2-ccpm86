;****************************************************************************
;
;   CP/M-86 "echo command tail" demo (wasm).
;
;   Proves that the emulated CCP populates the base page from the command line:
;   it prints the command tail the CCP stored at offset 0080H (a length byte
;   followed by the characters), one character at a time via BDOS C_WRITE.
;
;       cpm86run_unicorn.py ECHOARG.CMD one two.dat   ->   " ONE TWO.DAT"
;
;   The two filename arguments are also parsed into the default FCBs at 005CH
;   and 006CH (see ccp.py); this program only displays the tail.
;
;   CP/M-86 8080 model: base page occupies 0..0FFH, entry at CS:0100H.
;
;   Build:  wasm echoarg.asm
;           wl format cpm86 name ECHOARG.CMD file echoarg.obj
;           (native CP/M-86 .CMD; the former raw+bin2cmd.py wrap was retired)
;
;****************************************************************************
        .8086

_TEXT   segment byte public 'CODE'
        assume  cs:_TEXT, ds:_TEXT, ss:_TEXT

        org     100h                    ; base page occupies 0..0FFH
start_:
        push    cs
        pop     ds                      ; DS = CS -> base page addressable
        mov     bl, [0080h]             ; BL = command-tail length
        mov     si, 0081h               ; SI -> first tail character
next:
        test    bl, bl
        jz      done
        mov     dl, [si]
        mov     cl, 2                   ; BDOS C_WRITE (console output)
        int     0E0h
        inc     si
        dec     bl
        jmp     next
done:
        mov     cl, 0                   ; BDOS P_TERMCPM
        int     0E0h

_TEXT   ends
        end     start_
