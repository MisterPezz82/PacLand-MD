; YM2612 write queue for the Z80 (src/music.c fills it).
;
; The 68000 runs the music player and, instead of writing the YM2612 itself
; (each data write leaves the chip busy for ~25 us, which the 68000 had to
; wait out: ~7000 cycles a frame in busy scenes), appends [port, register,
; value] triples to this ring in Z80 RAM once per frame. The Z80 does the
; waiting. Port 0: part I (4000h/4001h), 2: part II (4002h/4003h).
;
; Ring: RING_START..RING_END-1 (3-byte entries); RD (Z80) and WR (68000) are
; little-endian addresses in the ring at QPTR.
	.module ymq
	.area _HEADER (ABS)
	.org 0x0000
RING_START = 0x1000
RING_END   = 0x1C00
RD = 0x1FF0
WR = 0x1FF2
	di
	ld sp,#0x1FE0
	ld hl,(RD)
loop:
	ld de,(WR)
	ld a,l
	cp e
	jr nz,go
	ld a,h
	cp d
	jr z,loop
go:
	ld c,(hl)              ; port 0 / 2
	inc hl
	ld d,(hl)              ; register
	inc hl
	ld e,(hl)              ; value
	inc hl
	ld a,h                 ; wrap
	cp #(RING_END >> 8)
	jr nz,nowrap
	ld hl,#RING_START
nowrap:
	ld ix,#0x4000
	ld a,c
	or a
	jr z,part1
busy2:
	ld a,(0x4000)
	rlca
	jr c,busy2
	ld 2(ix),d
busy2b:
	ld a,(0x4000)
	rlca
	jr c,busy2b
	ld 3(ix),e
	jr done
part1:
busy1:
	ld a,(0x4000)
	rlca
	jr c,busy1
	ld 0(ix),d
busy1b:
	ld a,(0x4000)
	rlca
	jr c,busy1b
	ld 1(ix),e
done:
	ld (RD),hl
	jr loop
	.area _CODE
	.area _DATA
