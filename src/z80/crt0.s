; Z80 sound driver start-up: stack below the mailbox, interrupt mode 1 (the
; VDP's vertical interrupt counts ticks at 0038h), then main().
	.module crt0
	.globl _main
	.globl _ticks
	.area _HEADER (ABS)
	.org 0x0000
	di
	ld sp,#0x1F00
	im 1
	jp start
	.org 0x0038
	push af
	ld a,(_ticks)
	inc a
	ld (_ticks),a
	pop af
	reti			; interrupts stay off until main() has seen the tick (/INT lasts a whole line)
	.org 0x0050
start:
	call _main
halt_loop:
	jr halt_loop
	.area _HOME
	.area _CODE
	.area _INITIALIZER
	.area _GSINIT
	.area _GSFINAL
	.area _DATA
	.area _INITIALIZED
	.area _BSEG
	.area _BSS
	.area _HEAP
