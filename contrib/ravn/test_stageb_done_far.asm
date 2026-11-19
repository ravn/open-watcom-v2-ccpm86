; done-far.asm -- LARGE-model (FAR) MAME completion signals for DR C 1.11 builds.
;
; Two NO-ARG far procs (retf) so no C calling-convention assumption is needed
; (DR C's stack-arg layout for a K&R extern is not what putchar's [bp+6] frame
; implies, so passing a computed status was unreliable -- it read the boot
; memory-test's 0x55AA fill pattern from an unwritten stack cell).  Each drives
; one fixed 16-bit word onto the undecoded I/O port 0x2FE with `OUT DX,AX`, which
; mame-tests/done_signal.lua taps to print + snapshot + stop the emulator:
;   mame_ok  -> 0x0008  (all 8 checks passed: 4 far-call + 4 code-byte)
;   mame_bad -> 0x00FF  (at least one check failed)
; Assembled by Open Watcom bwasm with -nm=MD so DR LINK-86 accepts the module.
	.8086
CGROUP	group	CODE
CODE	segment	byte public 'CODE'
	assume	cs:CODE
	public	mame_ok
	public	mame_bad
mame_ok	proc	far
	mov	dx,02FEh
	mov	ax,0008h
	out	dx,ax
	retf
mame_ok	endp
mame_bad	proc	far
	mov	dx,02FEh
	mov	ax,00FFh
	out	dx,ax
	retf
mame_bad	endp
CODE	ends
	end
