; crt759.asm -- minimal CP/M-86 startup for a freestanding wlink medium-model
; (-mm -zm) program, so wlink's OWN far-code relocation can be booted on real
; CCP/M-86 in MAME.  Distilled from what the DR C reference (CLEARL) does:
;   * reserve the base page at the start of DGROUP (loader zero-fills it:
;     genuine CCP/M kern/load.sup:477 init_base "1st Data Group has Base Page");
;   * put a STACK area in DGROUP and set SS:SP to it: the loader DOES hand the
;     program a stack (kern/load.sup: u_initss=lod_lstk, u_stack_sp=ls_sp, plus
;     a pushed RETF frame to user_retf), but it is only lstklen bytes -- fine for
;     a trivial program, so we switch to our own roomier stack to be safe, the
;     same thing DR C's CLEARL crt0 does;
;   * enter at CS:0 (kern/load.sup sets the initial IP to 0 for non-8080 models;
;     there is no entry-point field in a .CMD), then far-call the C cpmmain.
BEGDATA	segment	word public 'BEGDATA'
	db	100h dup(0)		; base page (clobbered/filled by the loader)
BEGDATA	ends
CONST	segment	word public 'DATA'
CONST	ends
CONST2	segment	word public 'DATA'
CONST2	ends
_DATA	segment	word public 'DATA'
_DATA	ends
_BSS	segment	word public 'BSS'
_BSS	ends
STK	segment	word public 'DATA'
	db	200h dup(0)
stktop	label	word
STK	ends
DGROUP	group	BEGDATA, CONST, CONST2, _DATA, _BSS, STK

CODE	segment	byte public 'CODE'
	assume	cs:CODE, ds:DGROUP, ss:DGROUP
	extrn	cpmmain_:far
	public	start_
start_	proc	far
	mov	ax,ds			; loader sets DS = base-page (DGROUP) segment
	mov	ss,ax			; small-model: SS = DS
	mov	sp,offset DGROUP:stktop
	call	far ptr cpmmain_
	mov	cl,0			; BDOS 0 = P_TERMCPM
	int	0E0h
start_	endp
CODE	ends
	end
