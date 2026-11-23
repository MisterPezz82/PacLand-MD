; Pac-Land sound player for the Mega Drive's Z80 (hand-written; sdasz80).
;
; A port of the C player in src/z80/musdrv.c (which the 68000 build uses as
; the reference): the same requests -> streams -> 8 virtual voices ->
; channels -> chip writes, the same decisions in the same order, so the two
; produce identical YM2612/PSG write sequences (tests/z80_player.mjs checks
; this frame by frame). Data layout: include/musdrv.h. The 68000 side is
; src/music.c.
;
; Conventions: routines clobber AF, BC, DE, HL unless stated; IX points at
; the current stream (SRC_*), IY at the mailbox (fixed).
	.module player

; ---- layout (include/musdrv.h) ----
MBX      = 0x1F00
MB_REQ   = 0x00
MB_CLR   = 0x20
MB_STAT  = 0x24
MB_ALIVE = 0x26
MB_CMD   = 0x27
MB_GO    = 0x28
MB_PROT  = 0x29
MB_BASE  = 0x2A
MB_PAL   = 0x2E
MB_DONE  = 0x2F
MB_DBG   = 0x30
MB_LOG   = 0x50
MB_LOGN  = 0x51
TABLES   = 0x1D00
T_SQUARE = TABLES + 160
LOGBUF   = 0x1400
LOGMAX   = 512
TR_SIZE  = 24
NSRC     = 6
YMA      = 0x4000
BANKREG  = 0x6000
PSGP     = 0x7F11

; stream record
S_ON     = 0
S_REQ    = 1
S_SEQ    = 2
S_NV     = 3
S_FLAGS  = 4
S_FRAME  = 5
S_NF     = 7
S_LOOP   = 9
S_BANK   = 11
S_PTR    = 13          ; address in the 8000h window
S_LOOPOFF= 15
S_VOICES = 18
S_PARTNER= 26
S_VOL    = 30
S_WAVE   = 38
S_FB     = 46
S_PER    = 62
SRC_SIZE = 80

	.area _HEADER (ABS)
	.org 0x0000
	di
	ld sp,#MBX
	im 1
	jp start

	.area _CODE
start:
	ld iy,#MBX
	call drv_init
	ld a,MB_GO(iy)
	ld (last_go),a
main_loop:
	; the 68000's vertical interrupt count (MB_GO) gives the tempo; the
	; update then waits until the game loop says this frame's DMA is over
	; (MB_DONE = MB_GO), so ROM reads never meet a DMA transfer, or, when the
	; game loop runs late, until about a third of a frame has passed (and
	; still never while SGDK's protection flag is up)
	ld a,(last_go)
	ld b,a
1$:	ld a,MB_GO(iy)
	cp b
	jr z,1$
	ld c,a
	ld de,#1100            ; ~ 26 cycles a turn: ~ 28000 cycles
7$:	ld a,MB_DONE(iy)
	cp c
	jr z,8$
	dec de
	ld a,d
	or e
	jr nz,7$
8$:	ld a,c
	ld (last_go),a
	sub b                  ; n = go - last
	cp #9
	jr c,2$
	ld a,#8                ; after a long stall (loading), do not rush
2$:	ld (todo),a
3$:	ld a,(todo)
	or a
	jr z,main_loop
	dec a
	ld (todo),a
	ld a,MB_PAL(iy)
	or a
	jr z,4$
	ld a,(pal_acc)         ; PAL: 6 updates per 5 interrupts
	add a,#6
	ld (pal_acc),a
5$:	ld a,(pal_acc)
	cp #5
	jr c,6$
	sub #5
	ld (pal_acc),a
	call update
	jr 5$
4$:	call update
6$:	inc MB_ALIVE(iy)
	jr 3$

; ======================================================================
; chips
; ======================================================================

; log [D = port 0/2/FFh, E = register, A = value] when MB_LOG is set:
; a ring of LOGMAX entries; MB_LOGN counts every entry ever written (the
; 68000 keeps its own read position, so neither side resets anything the
; other is updating). Keeps every register.
logw:
	ld (logv),a
	ld a,MB_LOG(iy)
	or a
	jr z,9$
	push hl
	push bc
	ld l,MB_LOGN(iy)
	ld h,MB_LOGN+1(iy)
	push hl
	ld a,h                 ; slot = n & (LOGMAX - 1)
	and #(LOGMAX - 1) >> 8
	ld h,a
	ld b,h
	ld c,l
	add hl,hl
	add hl,bc              ; *3
	ld bc,#LOGBUF
	add hl,bc
	ld (hl),d
	inc hl
	ld (hl),e
	inc hl
	ld a,(logv)
	ld (hl),a
	pop hl                 ; the entry is complete: count it
	inc hl
	ld MB_LOGN(iy),l
	ld MB_LOGN+1(iy),h
	pop bc
	pop hl
9$:	ld a,(logv)
	ret

; YM2612 write: D = 0 (part I) or 2 (part II), E = register, A = value
ym:
	call logw
	push bc
	ld c,a
	ld a,d
	or a
	jr nz,2$
1$:	ld a,(YMA)
	rlca
	jr c,1$
	ld a,e
	ld (YMA),a
3$:	ld a,(YMA)
	rlca
	jr c,3$
	ld a,c
	ld (YMA+1),a
	pop bc
	ret
2$:	ld a,(YMA)
	rlca
	jr c,2$
	ld a,e
	ld (YMA+2),a
4$:	ld a,(YMA)
	rlca
	jr c,4$
	ld a,c
	ld (YMA+3),a
	pop bc
	ret

; PSG write: A = byte
psg:
	push de
	ld d,#0xFF
	ld e,#0
	call logw
	pop de
	ld (PSGP),a
	ret

; part (0/2) and channel within the part for FM channel A: D = port, E = c
fm_pc:
	ld e,a
	ld d,#0
	cp #3
	ret c
	sub #3
	ld e,a
	ld d,#2
	ret

; HL = patch for waveform A (the custom test patch when MB_DBG+4 is set)
patch_of:
	ld l,a
	ld a,MB_DBG+4(iy)
	or a
	jr z,1$
	ld hl,#MBX+MB_DBG+10
	ret
1$:	ld h,#0
	ld a,l
	add hl,hl              ; *10 = *8 + *2
	ld c,l
	ld b,h
	add hl,hl
	add hl,hl
	add hl,bc
	ld bc,#TABLES
	add hl,bc
	ret

; fm_silence: A = FM channel
fm_silence:
	ld (ch),a
	ld hl,#fm_quiet
	call add_hl_a
	ld a,(hl)
	or a
	ret nz
	ld (hl),#1
	ld a,(ch)
	ld hl,#fm_patch
	call add_hl_a
	ld a,(hl)
	bit 7,a
	ret nz
	call patch_of          ; HL = patch
	inc hl
	ld a,(hl)
	ld (carriers),a
	ld a,(ch)
	call fm_pc
	ld (part),de
	ld b,#0                ; o
1$:	ld a,(carriers)
	call bit_b
	jr z,2$
	ld a,(ch)              ; fm_tl[ch][o]
	add a,a
	add a,a
	add a,b
	ld hl,#fm_tl
	call add_hl_a
	ld a,(hl)
	cp #127
	jr z,2$
	ld (hl),#127
	push bc
	ld hl,#opofs
	ld a,b
	call add_hl_a
	ld a,(hl)
	add a,#0x40
	ld de,(part)           ; E = c, D = port
	add a,e
	ld e,a
	ld a,#127
	call ym
	pop bc
2$:	inc b
	ld a,b
	cp #4
	jr nz,1$
	ret

; fm_set: A = channel, (w_wave) = waveform, (w_amp) = amplitude, (w_fb) = block/F-number
fm_set:
	ld (ch),a
	ld hl,#fm_quiet
	call add_hl_a
	ld (hl),#0
	ld a,(ch)
	call fm_pc
	ld (part),de
	ld a,(w_wave)
	call patch_of
	ld (mp),hl
	ld a,MB_DBG+4(iy)      ; test hook: reprogram when the custom patch changes
	or a
	jr z,10$
	ld de,#last_custom
	ld b,#10
11$:	ld a,(de)
	cp (hl)
	jr z,12$
	ld a,(hl)
	ld (de),a
	push hl
	ld a,(ch)
	ld hl,#fm_patch
	call add_hl_a
	ld (hl),#0xFF
	pop hl
12$:	inc hl
	inc de
	djnz 11$
10$:	ld a,(ch)
	ld hl,#fm_patch
	call add_hl_a
	ld a,(w_wave)
	cp (hl)
	jp z,20$
	ld (hl),a
	; program the patch
	ld hl,(mp)
	ld a,(hl)
	ld de,(part)
	push af
	ld a,e
	add a,#0xB0
	ld e,a
	pop af
	call ym
	ld de,(part)
	ld a,e
	add a,#0xB4
	ld e,a
	ld a,#0xC0
	call ym
	ld b,#0
13$:	ld hl,#opofs           ; r = opofs[o] + c
	ld a,b
	call add_hl_a
	ld a,(hl)
	ld hl,#part
	add a,(hl)
	ld (reg_r),a
	ld hl,(mp)             ; DT/MUL
	inc hl
	inc hl
	ld a,b
	call add_hl_a
	ld a,(hl)
	ld c,#0x30
	call ym_r
	ld a,#0x1F
	ld c,#0x50
	call ym_r
	xor a
	ld c,#0x60
	call ym_r
	xor a
	ld c,#0x70
	call ym_r
	ld a,#0x0F
	ld c,#0x80
	call ym_r
	xor a
	ld c,#0x90
	call ym_r
	ld a,(ch)              ; HL = &fm_tl[ch][o]
	add a,a
	add a,a
	add a,b
	ld hl,#fm_tl
	call add_hl_a
	push hl
	ld hl,(mp)
	inc hl
	ld a,(hl)
	call bit_b
	pop hl
	jr nz,14$
	push hl                ; modulator: its TL now
	ld hl,(mp)
	ld a,#6
	add a,b
	call add_hl_a
	ld a,(hl)
	pop hl
	ld (hl),a
	ld c,#0x40
	call ym_r
	jr 15$
14$:	ld (hl),#0xFF
15$:	inc b
	ld a,b
	cp #4
	jr nz,13$
	; key off and on: every operator restarts in phase
	ld de,(part)
	ld a,d
	add a,a                ; part<<2: port 2 -> 4
	or e
	ld c,a
	ld de,#0x0028
	call ym
	ld a,c
	or #0xF0
	ld de,#0x0028
	call ym
20$:	; carriers' TL
	ld b,#0
21$:	ld hl,(mp)
	inc hl
	ld a,(hl)
	call bit_b
	jr z,23$
	ld hl,(mp)             ; t = tl + amp_tl[amp]
	ld a,#6
	add a,b
	call add_hl_a
	ld c,(hl)
	ld hl,#amp_tl
	ld a,(w_amp)
	call add_hl_a
	ld a,(hl)
	add a,c
	jr c,22$
	cp #128
	jr c,24$
22$:	ld a,#127
24$:	ld c,a
	ld a,(ch)
	add a,a
	add a,a
	add a,b
	ld hl,#fm_tl
	call add_hl_a
	ld a,c
	cp (hl)
	jr z,23$
	ld (hl),a
	push bc
	ld hl,#opofs
	ld a,b
	call add_hl_a
	ld a,(hl)
	ld hl,#part
	add a,(hl)
	add a,#0x40
	ld de,(part)
	ld e,a
	ld a,c
	call ym
	pop bc
23$:	inc b
	ld a,b
	cp #4
	jr nz,21$
	; block / F-number
	ld a,(ch)
	add a,a
	ld hl,#fm_fbc
	call add_hl_a
	ld de,(w_fb)
	ld a,(hl)
	cp e
	jr nz,25$
	inc hl
	ld a,(hl)
	dec hl
	cp d
	ret z
25$:	ld (hl),e
	inc hl
	ld (hl),d
	ld a,(part)
	add a,#0xA4
	ld hl,(part)
	ld l,a
	ex de,hl               ; HL = fb, DE = port/reg
	ld a,h
	push hl
	call ym
	pop hl
	ld a,(part)
	add a,#0xA0
	ld e,a
	ld a,l
	jp ym

; ym_r: A = value, C = register base; register = C + (reg_r); port from (part+1)
ym_r:
	push bc
	push af
	ld a,(reg_r)
	add a,c
	ld e,a
	ld a,(part+1)
	ld d,a
	pop af
	call ym
	pop bc
	ret

; Z flag = !(A & (1 << B))
bit_b:
	push bc
	inc b
1$:	rrca
	djnz 1$
	pop bc
	bit 7,a                ; after B+1 right rotations bit B is in bit 7
	ret

add_hl_a:
	add a,l
	ld l,a
	ret nc
	inc h
	ret

; PSG
; psg_att_set: B = channel 0..3, A = attenuation
psg_att_set:
	ld c,a
	ld hl,#psg_att
	ld a,b
	call add_hl_a
	ld a,c
	cp (hl)
	ret z
	ld (hl),a
	ld a,b
	rrca
	rrca
	rrca                   ; << 5
	or #0x90
	or c
	jp psg

; psg_tone: B = channel 0..2, (w_amp) = volume, (w_per) = period
psg_tone:
	ld a,b
	add a,a
	ld hl,#psg_per
	call add_hl_a
	ld de,(w_per)
	ld a,(hl)
	cp e
	jr nz,1$
	inc hl
	ld a,(hl)
	dec hl
	cp d
	jr z,2$
1$:	ld (hl),e
	inc hl
	ld (hl),d
	ld a,b
	rrca
	rrca
	rrca
	or #0x80
	ld c,a
	ld a,e
	and #15
	or c
	call psg
	ld a,e                 ; per >> 4
	srl d
	rra
	srl d
	rra
	srl d
	rra
	srl d
	rra
	call psg
2$:	ld hl,#vol_psg
	ld a,(w_amp)
	call add_hl_a
	ld a,(hl)
	jp psg_att_set

; psg_noise: (w_amp), (w_per): noise clocked by tone channel 2
psg_noise:
	ld hl,#psg_per+4
	ld de,(w_per)
	ld a,(hl)
	cp e
	jr nz,1$
	inc hl
	ld a,(hl)
	dec hl
	cp d
	jr z,2$
1$:	ld (hl),e
	inc hl
	ld (hl),d
	ld a,e
	and #15
	or #0xC0
	call psg
	ld a,e
	srl d
	rra
	srl d
	rra
	srl d
	rra
	srl d
	rra
	call psg
2$:	ld a,(noise_mode)
	cp #0xE7
	jr z,3$
	ld a,#0xE7
	ld (noise_mode),a
	call psg
3$:	ld hl,#vol_psg
	ld a,(w_amp)
	call add_hl_a
	ld a,(hl)
	add a,#1               ; NOISE_ATT
	cp #16
	jr c,4$
	ld a,#15
4$:	ld b,#3
	jp psg_att_set

; ch_silence: A = channel 0..8
ch_silence:
	cp #6
	jp c,fm_silence
	sub #6
	ld b,a
	ld a,#15
	jp psg_att_set

; ======================================================================
; ROM (68000 bus through the bank window)
; ======================================================================

; set_bank: DE = bank (68000 address >> 15)
set_bank:
	ld hl,(cur_bank)
	or a
	sbc hl,de
	ret z
	ld (cur_bank),de
	ld hl,#BANKREG
	ld a,e
	ld b,#8
1$:	ld (hl),a
	rrca
	djnz 1$
	ld (hl),d
	ret

; rd: A = next byte (HL = window address, (rb_bank) = bank); keeps BC, DE
rd:
	ld a,(hl)
	inc l
	ret nz
	inc h
	ret nz
	push af                ; left the window: next bank
	push de
	push bc
	ld de,(rb_bank)
	inc de
	ld (rb_bank),de
	call set_bank
	pop bc
	pop de
	pop af
	ld hl,#0x8000
	ret

; at: DE -> 3-byte big-endian offset from the blob start; sets (rb_bank),
; returns HL = window address, bank selected
at:
	ex de,hl
	ld b,(hl)              ; hi
	inc hl
	ld d,(hl)
	inc hl
	ld e,(hl)              ; DE = lo 16 bits
	ld l,MB_BASE+2(iy)     ; base offset in its bank
	ld h,MB_BASE+3(iy)
	ld a,d
	and #0x7F
	ld c,a
	push de
	ld d,c                 ; DE = lo & 7FFFh
	add hl,de
	pop de
	push hl                ; off (may be >= 8000h)
	ld l,MB_BASE(iy)
	ld h,MB_BASE+1(iy)
	ld a,b                 ; + hi << 1
	add a,a
	ld c,a
	ld a,#0
	adc a,#0
	push de
	ld e,c
	ld d,a
	add hl,de
	pop de
	bit 7,d                ; + lo >> 15
	jr z,1$
	inc hl
1$:	ex de,hl               ; DE = bank
	pop hl                 ; HL = off
	bit 7,h
	jr z,2$
	res 7,h
	inc de
2$:	ld (rb_bank),de
	push hl
	call set_bank
	pop hl
	set 7,h                ; window address
	ret

wait_prot:
	ld a,MB_PROT(iy)
	or a
	jr nz,wait_prot
	ret

; load_hdr: A = track index -> thdr
load_hdr:
	ld l,a                 ; x = ti * 24
	ld h,#0
	add hl,hl
	add hl,hl
	add hl,hl
	ld e,l
	ld d,h
	add hl,hl
	add hl,de
	ld a,h
	ld (o3+1),a
	ld a,l
	ld (o3+2),a
	xor a
	ld (o3),a
	call wait_prot
	ld de,#o3
	call at
	ld de,#thdr
	ld b,#TR_SIZE
1$:	call rd
	ld (de),a
	inc de
	djnz 1$
	ret

; ======================================================================
; streams and voices
; ======================================================================

; IX = &src[A]
src_ptr:                       ; keeps BC, DE
	ld ix,#src
	or a
	ret z
	push bc
	push de
	ld b,a
	ld de,#SRC_SIZE
1$:	add ix,de
	djnz 1$
	pop de
	pop bc
	ret

; req_clear: A = request index
req_clear:
	ld c,a
	ld hl,#MBX+MB_REQ
	call add_hl_a
	ld (hl),#0
	ld hl,#req_prev
	ld a,c
	call add_hl_a
	ld (hl),#0
	ld a,c                 ; MB_CLR[n >> 3] |= 1 << (n & 7)
	and #7
	ld b,a
	ld a,#1
	jr z,2$
1$:	add a,a
	djnz 1$
2$:	ld b,a
	ld a,c
	rrca
	rrca
	rrca
	and #3
	ld hl,#MBX+MB_CLR
	call add_hl_a
	ld a,(hl)
	or b
	ld (hl),a
	ret

; src_start: IX = stream, A = track index; header in thdr
src_start:
	ld S_REQ(ix),a
	ld S_ON(ix),#1
	ld a,(seq_next)
	inc a
	ld (seq_next),a
	ld S_SEQ(ix),a
	xor a
	ld S_FRAME(ix),a
	ld S_FRAME+1(ix),a
	ld hl,#thdr+6
	ld a,(hl)
	ld S_NF(ix),a
	inc hl
	ld a,(hl)
	ld S_NF+1(ix),a
	inc hl
	ld a,(hl)
	ld S_LOOP(ix),a
	inc hl
	ld a,(hl)
	ld S_LOOP+1(ix),a
	inc hl
	ld a,(hl)
	ld S_NV(ix),a
	inc hl
	ld a,(hl)
	ld S_FLAGS(ix),a
	push ix
	pop hl
	ld de,#S_LOOPOFF
	add hl,de
	ex de,hl
	ld hl,#thdr+3
	ld bc,#3
	ldir
	push ix
	pop hl
	ld de,#S_VOICES
	add hl,de
	ex de,hl
	ld hl,#thdr+12
	ld bc,#12              ; voices[8] and partner[4]
	ldir
	push ix                ; vol, wave, fb, per = 0
	pop hl
	ld de,#S_VOL
	add hl,de
	ld b,#48
1$:	ld (hl),#0
	inc hl
	djnz 1$
	ld de,#thdr
	call at
	ld a,(rb_bank)
	ld S_BANK(ix),a
	ld a,(rb_bank+1)
	ld S_BANK+1(ix),a
	ld S_PTR(ix),l
	ld S_PTR+1(ix),h
	ld a,#1
	ld (own_dirty),a
	ret

; voice_set: C = voice, (w_vol), (w_wave), (w_fb), (w_per); keeps IX, B
voice_set:
	push bc
	ld a,c
	ld hl,#bitmask
	call add_hl_a
	ld e,(hl)              ; E = 1 << v
	ld a,MB_DBG+3(iy)      ; test hook: mute voices
	and e
	jr z,1$
	xor a
	ld (w_vol),a
1$:	ld hl,#vvol
	ld a,c
	call add_hl_a
	ld d,(hl)              ; D = b (old)
	ld a,(w_vol)
	cp d
	jr nz,3$
	and #15
	jp z,9$                ; silent and unchanged
	ld hl,#vwave
	ld a,c
	call add_hl_a
	ld a,(w_wave)
	cp (hl)
	jr nz,3$
	ld a,c                 ; fb and per
	add a,a
	ld hl,#vfb
	call add_hl_a
	push de
	ld de,(w_fb)
	ld a,(hl)
	cp e
	jr nz,2$
	inc hl
	ld a,(hl)
	cp d
	jr nz,2$
	ld a,c
	add a,a
	ld hl,#vper
	call add_hl_a
	ld de,(w_per)
	ld a,(hl)
	cp e
	jr nz,2$
	inc hl
	ld a,(hl)
	cp d
	pop de
	jp z,9$
	jr 3$
2$:	pop de
3$:	ld a,(m_dirty)
	or e
	ld (m_dirty),a
	; Activity/noise/waveform changes alter channel availability and folds.
	; Releasing a voice matters as much as starting one (noise owns tone 2).
	ld a,(w_vol)
	xor d
	and #0x80
	jr nz,7$
	ld a,d
	and #15
	ld b,a
	ld a,(w_vol)
	and #15
	jr nz,10$
	ld a,b
	or a
	jr nz,7$
	jr 11$
10$:	ld a,b
	or a
	jr z,7$
11$:	ld hl,#vwave
	ld a,c
	call add_hl_a
	ld a,(w_wave)
	cp (hl)
	jr nz,7$
	ld a,(w_vol)
	and #15
	jr z,8$
	ld hl,#vmerged
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,8$               ; folded into its partner
	ld hl,#vch
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr z,7$
	ld b,a                 ; B = channel
	ld a,(w_vol)
	xor d
	and #0x80
	jr nz,7$
	ld a,b
	cp #6
	jr c,8$
	cp #9
	jr nc,8$
	ld hl,#T_SQUARE
	ld a,(w_wave)
	call add_hl_a
	ld a,(hl)
	or a
	jr nz,8$
7$:	ld a,#1
	ld (m_realloc),a
8$:	ld hl,#vvol
	ld a,c
	call add_hl_a
	ld a,(w_vol)
	ld (hl),a
	ld hl,#vwave
	ld a,c
	call add_hl_a
	ld a,(w_wave)
	ld (hl),a
	ld a,c
	add a,a
	ld hl,#vfb
	call add_hl_a
	ld de,(w_fb)
	ld (hl),e
	inc hl
	ld (hl),d
	ld a,c
	add a,a
	ld hl,#vper
	call add_hl_a
	ld de,(w_per)
	ld (hl),e
	inc hl
	ld (hl),d
9$:	pop bc
	ret

; load (w_vol..w_per) from stream IX, track voice index A
src_voice:
	ld b,a
	push ix
	pop hl
	ld de,#S_VOL
	add hl,de
	ld a,b
	call add_hl_a
	ld a,(hl)
	ld (w_vol),a
	ld de,#8
	add hl,de
	ld a,(hl)
	ld (w_wave),a
	push ix
	pop hl
	ld de,#S_FB
	add hl,de
	ld a,b
	add a,a
	call add_hl_a
	ld e,(hl)
	inc hl
	ld d,(hl)
	ld (w_fb),de
	ld de,#S_PER-S_FB-1
	add hl,de
	ld e,(hl)
	inc hl
	ld d,(hl)
	ld (w_per),de
	ret

; src_step: IX = stream, A = its index
src_step:
	ld (si),a
	ld l,S_FRAME(ix)       ; frame >= nf ?
	ld h,S_FRAME+1(ix)
	ld e,S_NF(ix)
	ld d,S_NF+1(ix)
	or a
	sbc hl,de
	jr c,5$
	ld a,S_LOOP(ix)
	and S_LOOP+1(ix)
	inc a
	jr z,1$                ; no loop
	ld hl,#MBX+MB_REQ
	ld a,S_REQ(ix)
	call add_hl_a
	ld a,(hl)
	or a
	jr z,1$
	ld a,S_LOOP(ix)
	ld S_FRAME(ix),a
	ld a,S_LOOP+1(ix)
	ld S_FRAME+1(ix),a
	push ix
	pop hl
	ld de,#S_LOOPOFF
	add hl,de
	ex de,hl
	call at
	ld a,(rb_bank)
	ld S_BANK(ix),a
	ld a,(rb_bank+1)
	ld S_BANK+1(ix),a
	ld S_PTR(ix),l
	ld S_PTR+1(ix),h
	jr 5$
1$:	ld S_ON(ix),#0
	ld a,S_REQ(ix)
	call req_clear
	ld a,#1
	ld (own_dirty),a
	ret
5$:	ld e,S_BANK(ix)
	ld d,S_BANK+1(ix)
	ld (rb_bank),de
	call set_bank
	ld l,S_PTR(ix)
	ld h,S_PTR+1(ix)
	call rd
	ld (mask),a
	ld c,#0                ; k
6$:	ld a,(mask)
	or a
	jp z,20$
	srl a
	ld (mask),a
	jp nc,19$
	call rd                ; fl
	ld b,a
	bit 0,b
	jr z,7$
	push hl
	push ix
	pop hl
	ld a,c
	add a,#S_VOL
	call add_hl_a
	ex de,hl               ; DE = &vol[k]
	pop hl                 ; HL = stream
	call rd
	ld (de),a
7$:	bit 1,b
	jr z,8$
	push hl
	push ix
	pop hl
	ld a,c
	add a,#S_WAVE
	call add_hl_a
	ex de,hl
	pop hl
	call rd
	ld (de),a
8$:	bit 2,b
	jr z,9$
	push hl
	push ix
	pop hl
	ld a,c
	add a,a
	add a,#S_FB
	call add_hl_a
	ex de,hl
	pop hl
	call rd                ; fb hi
	inc de
	ld (de),a
	call rd                ; fb lo
	dec de
	ld (de),a
	push hl
	ld hl,#S_PER-S_FB
	add hl,de
	ex de,hl
	pop hl
	call rd                ; per hi
	inc de
	ld (de),a
	call rd                ; per lo
	dec de
	ld (de),a
9$:	; owner?
	push hl
	push bc
	push ix
	pop hl
	ld a,c
	add a,#S_VOICES
	call add_hl_a
	ld a,(hl)              ; v
	ld b,a
	ld hl,#vown
	call add_hl_a
	ld a,(si)
	cp (hl)
	jr nz,18$
	ld a,c
	call src_voice
	pop bc
	push bc
	push ix
	pop hl
	ld a,c
	add a,#S_VOICES
	call add_hl_a
	ld c,(hl)
	call voice_set
18$:	pop bc
	pop hl
19$:	inc c
	jp 6$
20$:	ld a,(rb_bank)
	ld S_BANK(ix),a
	ld a,(rb_bank+1)
	ld S_BANK+1(ix),a
	ld S_PTR(ix),l
	ld S_PTR+1(ix),h
	inc S_FRAME(ix)
	ret nz
	inc S_FRAME+1(ix)
	ret


; ======================================================================
; update: one 60 Hz step
; ======================================================================

stop_all:
	ld ix,#src
	ld b,#NSRC
	ld de,#SRC_SIZE
1$:	ld S_ON(ix),#0
	add ix,de
	djnz 1$
	ld hl,#req_prev
	ld b,#32
2$:	ld (hl),#0
	inc hl
	djnz 2$
	ld a,#1
	ld (own_dirty),a
	ret

update:
	ld a,MB_CMD(iy)
	or a
	jr z,1$
	call stop_all
	ld MB_CMD(iy),#0
1$:	; requests: start on 0->1, stop on 1->0
	xor a
	ld (ri),a
2$:	ld a,(ri)
	ld hl,#MBX+MB_REQ
	call add_hl_a
	ld c,(hl)              ; r
	ld a,(ri)
	ld hl,#req_prev
	call add_hl_a
	ld a,c
	cp (hl)
	jp z,19$
	ld (hl),a
	or a
	jp z,15$
	ld a,(ri)
	call load_hdr
	ld a,(thdr+6)
	ld hl,#thdr+7
	or (hl)
	jp z,19$               ; empty track
	; already playing?
	ld ix,#src
	ld b,#0
3$:	ld a,S_ON(ix)
	or a
	jr z,4$
	ld a,(ri)
	cp S_REQ(ix)
	jp z,10$
4$:	ld de,#SRC_SIZE
	add ix,de
	inc b
	ld a,b
	cp #NSRC
	jr nz,3$
	; a free stream?
	ld ix,#src
	ld b,#0
5$:	ld a,S_ON(ix)
	or a
	jr z,10$
	ld de,#SRC_SIZE
	add ix,de
	inc b
	ld a,b
	cp #NSRC
	jr nz,5$
	; all busy: the oldest, a non-looping one if any
	ld a,#0xFF
	ld (old),a
	ld b,#0
6$:	ld a,b
	call src_ptr           ; IX = src[j]
	ld a,(old)
	cp #0xFF
	jr z,8$
	push ix
	call src_ptr_old       ; IY' -> src[old] in HL
	pop ix
	; old non-looping and j looping: skip j
	ld de,#S_LOOP
	push hl
	add hl,de
	ld a,(hl)
	inc hl
	and (hl)
	pop hl
	inc a                  ; Z: old loop == FFFFh (non-looping)
	jr nz,7$
	ld a,S_LOOP(ix)
	and S_LOOP+1(ix)
	inc a
	jr nz,9$               ; j loops: continue
	; old non-looping, j non-looping: compare age
	jr 71$
7$:	; old loops: a non-looping j wins
	ld a,S_LOOP(ix)
	and S_LOOP+1(ix)
	inc a
	jr z,8$
71$:	ld de,#S_SEQ
	add hl,de
	ld a,S_SEQ(ix)
	sub (hl)
	cp #128
	jr c,9$
8$:	ld a,b
	ld (old),a
9$:	inc b
	ld a,b
	cp #NSRC
	jr nz,6$
	ld a,(old)
	ld b,a
	call src_ptr
10$:	ld a,(ri)
	call src_start
	ld a,(thdr+11)
	and #1
	jr z,19$
	ld a,(ri)
	call req_clear
	jr 19$
15$:	; stop: the game cleared it
	ld ix,#src
	ld b,#NSRC
16$:	ld a,S_ON(ix)
	or a
	jr z,17$
	ld a,(ri)
	cp S_REQ(ix)
	jr nz,17$
	bit 0,S_FLAGS(ix)
	jr nz,17$
	ld S_ON(ix),#0
	ld a,#1
	ld (own_dirty),a
17$:	ld de,#SRC_SIZE
	add ix,de
	djnz 16$
19$:	ld a,(ri)
	inc a
	ld (ri),a
	cp #32
	jp nz,2$

	xor a
	ld (m_dirty),a
	ld (m_realloc),a
	; step the streams
	ld b,#0
20$:	ld a,b
	push bc
	call src_ptr
	pop bc
	ld a,S_ON(ix)
	or a
	jr z,21$
	call wait_prot
	push bc
	ld a,b
	call src_step
	pop bc
21$:	inc b
	ld a,b
	cp #NSRC
	jr nz,20$
	ld a,(own_dirty)
	or a
	call nz,owners
	; test hook: one voice, given waveform and pitch
	ld a,MB_DBG(iy)
	or a
	call nz,dbg_voice
	ld a,(m_dirty)
	or a
	ret z
	ld a,(m_realloc)
	or a
	call nz,alloc
	jp writes

; HL = &src[(old)]
src_ptr_old:
	ld a,(old)
	ld hl,#src
	or a
	ret z
	push bc
	ld b,a
	ld de,#SRC_SIZE
1$:	add hl,de
	djnz 1$
	pop bc
	ret

; ---- owners: the newest stream using a voice; status bytes ----
owners:
	xor a
	ld (own_dirty),a
	ld (na),a
	ld a,#1
	ld (m_realloc),a
	ld a,#0xFF
	ld (m_dirty),a
	ld b,#0                ; k
1$:	ld a,b
	push bc
	call src_ptr
	pop bc
	ld a,S_ON(ix)
	or a
	jr z,5$
	; insert k into ord, oldest first
	ld a,(na)
	ld c,a                 ; j = na
2$:	ld a,c
	or a
	jr z,4$
	ld hl,#ord-1
	call add_hl_a
	ld a,(hl)              ; ord[j-1]
	push bc
	push hl
	ld b,a
	ld hl,#src+S_SEQ
	ld de,#SRC_SIZE
	or a
	jr z,3$
31$:	add hl,de
	djnz 31$
3$:	ld a,(hl)              ; seq of ord[j-1]
	sub S_SEQ(ix)
	pop hl
	pop bc
	cp #128
	jr nc,4$
	ld a,(hl)              ; shift ord[j-1] -> ord[j]
	inc hl
	ld (hl),a
	dec c
	jr 2$
4$:	ld hl,#ord
	ld a,c
	call add_hl_a
	ld (hl),b
	ld hl,#na
	inc (hl)
5$:	inc b
	ld a,b
	cp #NSRC
	jr nz,1$
	; vown = FFh, then by age
	ld hl,#vown
	ld b,#8
6$:	ld (hl),#0xFF
	inc hl
	djnz 6$
	ld hl,#0
	ld (st),hl
	ld a,(na)
	or a
	jr z,12$
	ld c,#0                ; j
7$:	ld hl,#ord
	ld a,c
	call add_hl_a
	ld a,(hl)
	ld (oj),a
	push bc
	call src_ptr
	pop bc
	ld a,S_REQ(ix)         ; requests 86h..8Fh: playing status
	dec a                  ; (req + 85h - 86h)
	cp #10
	jr nc,8$
	ld b,a
	ld hl,#1
	or a
	jr z,81$
82$:	add hl,hl
	djnz 82$
81$:	ld de,(st)
	ld a,l
	or e
	ld e,a
	ld a,h
	or d
	ld d,a
	ld (st),de
8$:	ld b,S_NV(ix)
	ld a,b
	or a
	jr z,11$
	push ix
	pop hl
	ld de,#S_VOICES
	add hl,de
9$:	ld a,(hl)
	push hl
	ld hl,#vown
	call add_hl_a
	ld a,(oj)
	ld (hl),a
	pop hl
	inc hl
	djnz 9$
11$:	inc c
	ld a,(na)
	cp c
	jr nz,7$
12$:	ld hl,(st)
	ld MB_STAT(iy),l
	ld MB_STAT+1(iy),h
	; every voice from its owner
	ld c,#0
13$:	ld hl,#vown
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,14$
	xor a                  ; no owner: silent, keep the rest
	ld (w_vol),a
	ld hl,#vwave
	ld a,c
	call add_hl_a
	ld a,(hl)
	ld (w_wave),a
	ld a,c
	add a,a
	ld hl,#vfb
	call add_hl_a
	ld e,(hl)
	inc hl
	ld d,(hl)
	ld (w_fb),de
	ld a,c
	add a,a
	ld hl,#vper
	call add_hl_a
	ld e,(hl)
	inc hl
	ld d,(hl)
	ld (w_per),de
	jr 16$
14$:	push bc
	call src_ptr
	pop bc
	push ix
	pop hl
	ld de,#S_VOICES
	add hl,de
	ld b,#0
15$:	ld a,(hl)
	cp c
	jr z,151$
	inc hl
	inc b
	jr 15$
151$:	ld a,b
	push bc
	call src_voice
	pop bc
16$:	call voice_set
	inc c
	ld a,c
	cp #8
	jr nz,13$
	ret

; ---- test hook: voice 0 alone ----
dbg_voice:
	ld hl,#vvol
	ld b,#8
1$:	ld (hl),#0
	inc hl
	djnz 1$
	ld a,#15
	ld (vvol),a
	ld a,MB_DBG+1(iy)
	ld (vwave),a
	ld l,MB_DBG+6(iy)
	ld h,MB_DBG+7(iy)
	ld (vfb),hl
	ld l,MB_DBG+8(iy)
	ld h,MB_DBG+9(iy)
	ld (vper),hl
	ld a,(vch)
	cp #0xFF
	jr z,3$
	cp #6                  ; (vch[0] < 6) == (dbg_psg != 0): move it
	ld b,#0
	jr nc,2$
	inc b
2$:	ld a,MB_DBG+2(iy)
	or a
	ld a,#0
	jr z,21$
	inc a
21$:	cp b
	jr nz,3$
	ld a,(vch)
	ld hl,#chv
	call add_hl_a
	ld (hl),#0xFF
	ld a,#0xFF
	ld (vch),a
3$:	ld a,(vch)
	cp #0xFF
	jr nz,5$
	ld a,MB_DBG+2(iy)
	or a
	ld c,#0
	jr z,4$
	ld c,#6
4$:	ld hl,#chv
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr z,41$
	push hl
	ld hl,#vch
	call add_hl_a
	ld (hl),#0xFF
	pop hl
41$:	ld (hl),#0
	ld a,c
	ld (vch),a
5$:	ld a,#0xFF
	ld (m_dirty),a
	ld a,#1
	ld (m_realloc),a
	ret

; ---- channel assignment ----
alloc:
	; noise: PSG tone channel 2 is its clock
	ld hl,#vvol
	ld b,#8
	ld c,#0
1$:	ld a,(hl)
	and #0x8F
	cp #0x81
	jr c,2$
	ld c,#1
2$:	inc hl
	djnz 1$
	ld a,#9
	dec c
	jr nz,3$
	dec a
3$:	ld (top_psg),a
	cp #8
	jr nz,4$
	ld a,(chv+8)
	cp #0xFF
	jr z,4$
	ld hl,#vch
	call add_hl_a
	ld (hl),#0xFF
	ld a,#0xFF
	ld (chv+8),a
4$:	; more tones than channels: fold detuned partners, highest voices first
	xor a
	ld (merges),a
	ld d,#0                ; need
	ld e,#0                ; sq
	ld c,#0
5$:	ld hl,#vmerged
	ld a,c
	call add_hl_a
	ld (hl),#0xFF
	ld hl,#vvol
	ld a,c
	call add_hl_a
	ld a,(hl)
	ld b,a
	and #15
	jr z,7$
	bit 7,b
	jr nz,7$
	call is_square
	jr z,6$
	inc e
	jr 7$
6$:	inc d
7$:	inc c
	ld a,c
	cp #8
	jr nz,5$
	ld a,(top_psg)         ; if sq > top_psg - 6: need += sq - (top_psg - 6)
	sub #6
	ld b,a
	ld a,e
	sub b
	jr c,8$
	jr z,8$
	add a,d
	ld d,a
8$:	ld a,d
	cp #7
	jp c,20$
	ld c,#7                ; v = 7..1
9$:	ld a,d
	cp #7
	jp c,20$
	ld hl,#vown
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jp z,19$
	push de
	push bc
	call src_ptr
	pop bc
	pop de
	ld hl,#vvol
	ld a,c
	call add_hl_a
	ld a,(hl)
	ld b,a
	and #15
	jr z,19$
	bit 7,b
	jr nz,19$
	; u = partner nibble
	push ix
	pop hl
	push de
	ld de,#S_PARTNER
	add hl,de
	pop de
	ld a,c
	srl a
	call add_hl_a
	ld a,(hl)
	bit 0,c
	jr z,10$
	rrca
	rrca
	rrca
	rrca
10$:	and #15
	cp c
	jr nc,19$              ; u < v
	ld (u),a
	; Only fold partners still owned by the same stream. An effect can
	; temporarily steal either arcade voice from the music.
	ld hl,#vown
	call add_hl_a
	ld b,(hl)
	ld hl,#vown
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp b
	jr nz,19$
	ld hl,#vmerged
	ld a,(u)
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,19$
	ld a,(u)
	ld hl,#vvol
	call add_hl_a
	ld a,(hl)
	ld b,a
	and #15
	jr z,19$
	bit 7,b
	jr nz,19$
	ld a,(u)
	ld hl,#vwave
	call add_hl_a
	ld b,(hl)
	ld a,c
	ld hl,#vwave
	call add_hl_a
	ld a,(hl)
	cp b
	jr nz,19$
	ld hl,#vmerged
	ld a,c
	call add_hl_a
	ld a,(u)
	ld (hl),a
	dec d
	ld a,#1
	ld (merges),a
19$:	dec c
	jp nz,9$
20$:	; keep a voice on its channel while it suits it
	ld c,#0
21$:	ld hl,#vch
	ld a,c
	call add_hl_a
	ld e,(hl)              ; E = c (channel)
	ld hl,#vvol
	ld a,c
	call add_hl_a
	ld b,(hl)              ; B = vvol
	ld a,b
	and #15
	jr z,22$
	ld hl,#vmerged
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr z,23$
22$:	ld a,e                 ; release
	cp #0xFF
	jp z,39$
	call release_ch
	jp 39$
23$:	ld a,e
	cp #0xFF
	jr z,39$
	; ok = noise ? c == 9 : c < 6 || (c < top_psg && square)
	bit 7,b
	jr z,24$
	cp #9
	jp z,39$
	jr 29$
24$:	cp #6
	jp c,39$
	ld hl,#top_psg
	cp (hl)
	jr nc,29$
	call is_square
	jp nz,39$
29$:	ld a,e
	call release_ch
	jr 39$
	; Release all unsuitable channels before allocating, so an earlier
	; voice can reuse a channel released by a later one in the same tick.
39$:	inc c
	ld a,c
	cp #8
	jp nz,21$
	ld c,#0
46$:	ld hl,#vvol
	ld a,c
	call add_hl_a
	ld b,(hl)
	ld a,b
	and #15
	jr z,49$
	ld hl,#vmerged
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,49$
	ld hl,#vch
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,49$
30$:	bit 7,b
	jr z,31$
	ld a,(chv+9)
	cp #0xFF
	ld e,#0xFF
	jr nz,38$
	ld e,#9
	jr 38$
31$:	ld e,#0xFF
	call is_square
	jr z,34$
	ld a,MB_DBG+5(iy)
	or a
	jr nz,34$
	ld d,#6
32$:	ld a,(top_psg)
	cp d
	jr z,34$
	ld hl,#chv
	ld a,d
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr z,33$
	inc d
	jr 32$
33$:	ld e,d
	jr 38$
34$:	ld d,#0
35$:	ld hl,#chv
	ld a,d
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr z,33$
	inc d
	ld a,d
	cp #6
	jr nz,35$
38$:	ld a,e
	cp #0xFF
	jr z,49$
	ld hl,#vch
	ld a,c
	call add_hl_a
	ld (hl),e
	ld hl,#chv
	ld a,e
	call add_hl_a
	ld (hl),c
49$:	inc c
	ld a,c
	cp #8
	jp nz,46$
	; squares left without a channel: any free PSG tone channel
	ld c,#0
40$:	ld hl,#vvol
	ld a,c
	call add_hl_a
	ld b,(hl)
	ld a,b
	and #15
	jr z,45$
	bit 7,b
	jr nz,45$
	ld hl,#vch
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,45$
	ld hl,#vmerged
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,45$
	ld d,#6
41$:	ld a,(top_psg)
	cp d
	jr z,45$
	ld hl,#chv
	ld a,d
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr z,42$
	inc d
	jr 41$
42$:	ld (hl),c
	ld hl,#vch
	ld a,c
	call add_hl_a
	ld (hl),d
45$:	inc c
	ld a,c
	cp #8
	jr nz,40$
	ret

; release the channel in A held by voice C
release_ch:
	ld hl,#chv
	call add_hl_a
	ld (hl),#0xFF
	ld hl,#vch
	ld a,c
	call add_hl_a
	ld (hl),#0xFF
	ret

; NZ if voice C's waveform is a square (keeps BC, DE)
is_square:
	push hl
	ld hl,#vwave
	ld a,c
	call add_hl_a
	ld a,(hl)
	ld hl,#T_SQUARE
	call add_hl_a
	ld a,(hl)
	or a
	pop hl
	ret

; ---- chip writes: every channel after a reassignment, else the changed voices' ----
writes:
	ld hl,#0
	ld a,(m_realloc)
	or a
	jr z,1$
	ld hl,#0x3FF
	jr 5$
1$:	ld c,#0
2$:	ld a,(m_dirty)
	ld b,c
	inc b
3$:	rrca
	djnz 3$
	jr nc,4$               ; bit C of m_dirty (after C+1 rotations it is in carry)
	push hl
	ld hl,#vmerged
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,31$
	ld a,c
31$:	ld hl,#vch
	call add_hl_a
	ld a,(hl)
	pop hl
	cp #0xFF
	jr z,4$
	ld b,a                 ; chd |= 1 << ch
	push hl
	ld hl,#1
	or a
	jr z,33$
32$:	add hl,hl
	djnz 32$
33$:	ex de,hl
	pop hl
	ld a,l
	or e
	ld l,a
	ld a,h
	or d
	ld h,a
4$:	inc c
	ld a,c
	cp #8
	jr nz,2$
5$:	ld (chd),hl
	ld c,#0                ; k
6$:	ld hl,(chd)
	srl h
	rr l
	ld (chd),hl
	jp nc,19$
	ld hl,#chv
	ld a,c
	call add_hl_a
	ld a,(hl)
	cp #0xFF
	jr nz,10$
	ld a,c                 ; free channel: silence
	cp #9
	jr nc,8$
	ld hl,#top_psg
	cp (hl)
	jr nc,7$
	push bc
	call ch_silence
	pop bc
	jp 19$
7$:	push bc
	ld b,#2
	ld a,#15
	call psg_att_set
	pop bc
	jp 19$
8$:	push bc
	ld b,#3
	ld a,#15
	call psg_att_set
	pop bc
	jp 19$
10$:	ld (vv),a              ; voice
	ld hl,#vvol
	call add_hl_a
	ld a,(hl)
	and #15
	ld (w_amp),a
	ld a,(merges)          ; sqrt of the summed power of the folded voices
	or a
	jr z,15$
	ld a,(w_amp)
	call sq_of
	ld (pw),hl
	ld b,#0
11$:	ld hl,#vmerged
	ld a,b
	call add_hl_a
	ld a,(vv)
	cp (hl)
	jr nz,12$
	ld hl,#vvol
	ld a,b
	call add_hl_a
	ld a,(hl)
	and #15
	call sq_of
	ld de,(pw)
	add hl,de
	ld (pw),hl
12$:	inc b
	ld a,b
	cp #8
	jr nz,11$
	ld a,(w_amp)
	call sq_of
	ld de,(pw)
	or a
	sbc hl,de
	jr z,15$
	ld b,#0                ; a = 0; while a < 15 && sq16[a+1] <= p: a++
13$:	ld a,b
	cp #15
	jr z,14$
	inc a
	push bc
	call sq_of
	pop bc
	ld de,(pw)
	ex de,hl
	or a
	sbc hl,de              ; p - sq >= 0 ?
	jr c,14$
	inc b
	jr 13$
14$:	ld a,b
	ld (w_amp),a
15$:	ld a,(vv)
	ld hl,#vwave
	call add_hl_a
	ld a,(hl)
	ld (w_wave),a
	ld a,(vv)
	add a,a
	ld hl,#vfb
	call add_hl_a
	ld e,(hl)
	inc hl
	ld d,(hl)
	ld (w_fb),de
	ld a,(vv)
	add a,a
	ld hl,#vper
	call add_hl_a
	ld e,(hl)
	inc hl
	ld d,(hl)
	ld (w_per),de
	push bc
	ld a,c
	cp #6
	jr nc,16$
	call fm_set
	jr 18$
16$:	cp #9
	jr nc,17$
	sub #6
	ld b,a
	call psg_tone
	jr 18$
17$:	call psg_noise
18$:	pop bc
19$:	inc c
	ld a,c
	cp #10
	jp nz,6$
	ret

; HL = A * A
sq_of:
	ld hl,#sq16
	add a,a
	call add_hl_a
	ld a,(hl)
	inc hl
	ld h,(hl)
	ld l,a
	ret

; ---- start-up ----
drv_init:
	ld hl,#0xFFFF
	ld (cur_bank),hl
	ld b,#6
	ld hl,#fm_patch
1$:	ld (hl),#0xFF
	inc hl
	djnz 1$
	ld hl,#fm_fbc
	ld b,#12
2$:	ld (hl),#0xFF
	inc hl
	djnz 2$
	ld hl,#vch
	ld b,#8
3$:	ld (hl),#0xFF
	inc hl
	djnz 3$
	ld hl,#vmerged
	ld b,#8
4$:	ld (hl),#0xFF
	inc hl
	djnz 4$
	ld hl,#vown
	ld b,#8
5$:	ld (hl),#0xFF
	inc hl
	djnz 5$
	ld hl,#chv
	ld b,#10
6$:	ld (hl),#0xFF
	inc hl
	djnz 6$
	ld hl,#psg_per
	ld b,#6
7$:	ld (hl),#0xFF
	inc hl
	djnz 7$
	ld hl,#psg_att
	ld b,#4
8$:	ld (hl),#0xFF
	inc hl
	djnz 8$
	ld b,#0
9$:	push bc
	ld a,#15
	call psg_att_set
	pop bc
	inc b
	ld a,b
	cp #4
	jr nz,9$
	ld a,#9
	ld (top_psg),a
	ld a,#1
	ld (own_dirty),a
	ld de,#0x002B
	xor a
	call ym
	ld de,#0x0022
	xor a
	call ym
	ld de,#0x0027
	xor a
	call ym
	ld c,#0                ; channel
10$:	ld a,c
	call fm_pc             ; D = port, E = c
	ld a,d
	add a,a
	or e                   ; key off: c | part << 2
	push de
	ld de,#0x0028
	call ym
	pop de
	ld b,#0                ; o = 0, 4, 8, 12
11$:	push de
	ld a,b
	add a,#0x40
	add a,e
	ld e,a
	ld a,#127
	call ym
	pop de
	push de
	ld a,b
	add a,#0x80
	add a,e
	ld e,a
	ld a,#0xFF
	call ym
	pop de
	ld a,b
	add a,#4
	ld b,a
	cp #16
	jr nz,11$
	ld a,c                 ; fm_tl[i][*] = 127, fm_quiet[i] = 1
	add a,a
	add a,a
	ld hl,#fm_tl
	call add_hl_a
	ld b,#4
12$:	ld (hl),#127
	inc hl
	djnz 12$
	ld hl,#fm_quiet
	ld a,c
	call add_hl_a
	ld (hl),#1
	inc c
	ld a,c
	cp #6
	jr nz,10$
	ret

; ---- constants ----
opofs:   .db 0, 8, 4, 12
amp_tl:  .db 127, 35, 23, 16, 11, 8, 6, 4, 3, 2, 2, 1, 1, 0, 0, 0
vol_psg: .db 15, 12, 9, 7, 6, 4, 4, 3, 2, 2, 1, 1, 1, 0, 0, 0
sq16:    .dw 0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225, 256
bitmask: .db 1, 2, 4, 8, 16, 32, 64, 128

	.area _DATA
last_go:   .ds 1
todo:      .ds 1
pal_acc:   .ds 1
src:       .ds SRC_SIZE * NSRC
seq_next:  .ds 1
req_prev:  .ds 32
vvol:      .ds 8
vwave:     .ds 8
vfb:       .ds 16
vper:      .ds 16
vch:       .ds 8
chv:       .ds 10
vmerged:   .ds 8
vown:      .ds 8
own_dirty: .ds 1
m_dirty:   .ds 1
m_realloc: .ds 1
merges:    .ds 1
top_psg:   .ds 1
fm_patch:  .ds 6
fm_tl:     .ds 24
fm_fbc:    .ds 12
fm_quiet:  .ds 6
psg_per:   .ds 6
psg_att:   .ds 4
noise_mode:.ds 1
cur_bank:  .ds 2
rb_bank:   .ds 2
thdr:      .ds TR_SIZE
o3:        .ds 3
last_custom: .ds 10
ch:        .ds 1
part:      .ds 2
carriers:  .ds 1
mp:        .ds 2
reg_r:     .ds 1
w_vol:     .ds 1
w_wave:    .ds 1
w_amp:     .ds 1
w_fb:      .ds 2
w_per:     .ds 2
ri:        .ds 1
old:       .ds 1
si:        .ds 1
mask:      .ds 1
na:        .ds 1
ord:       .ds NSRC
oj:        .ds 1
st:        .ds 2
u:         .ds 1
chd:       .ds 2
vv:        .ds 1
pw:        .ds 2
logv:      .ds 1
