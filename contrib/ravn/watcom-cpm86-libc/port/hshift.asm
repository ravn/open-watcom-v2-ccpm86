; hshift.asm -- provide _HShift, the huge-pointer normalization shift amount.
;
; Open Watcom's cgsupp huge-pointer helpers (pts.asm -> __PTS / __PTC, pia.asm
; -> __PIA / __PIS) read the byte global `_HShift` to normalize seg:off huge
; pointers before compare/subtract/increment.  On DOS this byte lives in the
; C startup (crwd086.asm) and is set to 12 for real mode / 3 for protected
; mode.  Info-ZIP's ZIPSPLIT compares `char huge *` pointers, so a CP/M-86 link
; that pulls __PTS also needs _HShift.  Our minimal CP/M-86 crt0 does not carry
; the DOS startup data block, so we provide the single byte here as an on-demand
; archive member: it is pulled only when a program actually references a huge-
; pointer helper (ZIPSPLIT does; ZIP/UNZIP/ZIPCLOAK/ZIPNOTE do not).
;
; CP/M-86 runs the 8086 in REAL MODE, so the value is 12 -- identical to the
; DOS real-mode startup (crwd086.asm: "Huge Shift amount (real-mode=12,...)").

; _DATA is the canonical DGROUP data segment; `option dosseg` at link time
; coalesces it into DGROUP, so this byte is DS-addressable like the DOS startup's.
_DATA   segment word public 'DATA'
        public  "C",_HShift
_HShift db      12                      ; real-mode huge shift (DOS uses 12 too)
_DATA   ends

        end
