/* 32-byte FastEntry layout is asserted in objects.c. Produce the same SAT
 * commands as its C reference; unchanged objects take only the checks and
 * stores below. Dirty entries call the shared C cache/conversion helper. */
    .text
    .align 2
    .globl object_fast_draw_68k
object_fast_draw_68k:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    subq.l #4,%sp
    clr.w (%sp)
    clr.w object_fast_failed_misses
    move.w 54(%sp),%d3
    move.w 58(%sp),%d5
    move.w 62(%sp),%d6
    move.w 66(%sp),%d4
    move.w object_frame_no,%d7
    lea R6+0x2400,%a3
    lea object_fast_entries,%a4
    moveq #0,%d0
    move.w %d3,%d0
    lsl.w #3,%d0
    lea vdpSpriteCache,%a1
    adda.w %d0,%a1
    moveq #0,%d0
    move.w %d5,%d0
    addq.w #8,%d0
    .ifdef BLAST_NATIVE
    lsl.w #2,%d0
    .else
    lsl.w #3,%d0
    .endif
    lea object_draw_list,%a0
    adda.w %d0,%a0
    tst.w %d5
    beq .Ldraw_high
.Ldraw_loop:
    .ifdef BLAST_NATIVE
    subq.l #4,%a0
    move.w (%a0),%d0
    lea (%a3,%d0.w),%a5
    .else
    subq.l #8,%a0
    move.l (%a0),%a5
    move.l %a5,%d0
    sub.l %a3,%d0
    .endif
    lea (%a4,%d0.w),%a2
    .ifdef BLAST_NATIVE
    cmpi.b #4,25(%a2)
    beq .Ldraw_variant
    .endif
    move.l 4(%a5),%d0
    cmp.l (%a2),%d0
    bne .Ldraw_prepare
.Ldraw_variant:
    move.b object_fast_variant+1,%d0
    cmp.b 29(%a2),%d0
    bne .Ldraw_prepare
.Ldraw_metadata:
    tst.b 25(%a2)
    beq .Ldraw_next
    .ifdef BLAST_NATIVE
    move.w 2(%a0),%d1
    .else
    move.w 4(%a0),%d1
    .endif
    add.w %d4,%d1
    moveq #0,%d0
    move.b 20(%a2),%d0
    add.w %d0,%d1
    cmpi.w #320,%d1
    bge .Ldraw_next
    moveq #0,%d0
    move.b 22(%a2),%d0
    add.w %d1,%d0
    bmi .Ldraw_next
    moveq #0,%d2
    move.b 21(%a2),%d2
    sub.b 10(%a5),%d2
    andi.w #255,%d2
    subi.w #32,%d2
    cmpi.w #224,%d2
    bge .Ldraw_next
    moveq #0,%d0
    move.b 23(%a2),%d0
    add.w %d2,%d0
    bmi .Ldraw_next
    cmpi.b #2,25(%a2)
    .ifdef BLAST_NATIVE
    beq .Ldraw_owner
    bcc .Ldraw_resident
    bra .Ldraw_prepare
.Ldraw_owner:
    .else
    bne .Ldraw_prepare
    .endif
    move.l 8(%a2),%a6
    tst.b 26(%a2)
    beq .Ldraw_cell
    move.l (%a6),%d0
    cmp.l 4(%a2),%d0
    bne .Ldraw_prepare
    bra .Ldraw_ready
.Ldraw_cell:
    move.w (%a6),%d0
    cmp.w 6(%a2),%d0
    bne .Ldraw_prepare
.Ldraw_ready:
    /* Packed VRAM columns give every prepared entry exactly one sprite. */
    cmpi.w #77,%d3
    bhi .Ldraw_fail
.Ldraw_room:
    move.l 12(%a2),%a6
    move.w %d7,(%a6)
    .ifdef BLAST_NATIVE
    bra .Ldraw_emit
.Ldraw_resident:
    cmpi.w #77,%d3
    bhi .Ldraw_fail
.Ldraw_emit:
    .endif
    addi.w #128,%d1
    addi.w #128,%d2
    addq.w #1,%d3
    move.w %d2,(%a1)
    move.b 28(%a2),2(%a1)
    move.b %d3,3(%a1)
    move.w 16(%a2),4(%a1)
    move.w %d1,6(%a1)
    addq.l #8,%a1
.Ldraw_next:
    subq.w #1,%d5
    bne .Ldraw_loop
.Ldraw_high:
    tst.w %d6
    beq .Ldraw_success
    move.w %d6,%d5
    clr.w %d6
    moveq #0,%d0
    move.w %d5,%d0
    .ifdef BLAST_NATIVE
    lsl.w #2,%d0
    .else
    lsl.w #3,%d0
    .endif
    lea object_draw_list,%a0
    adda.w %d0,%a0
    bra .Ldraw_loop
.Ldraw_prepare:
    move.l %a0,-(%sp)
    move.l %a1,-(%sp)
    lea 8(%sp),%a6
    pea (%a6)
    move.l %d4,-(%sp)
    .ifdef BLAST_NATIVE
    move.w 2(%a0),%d0
    .else
    move.w 4(%a0),%d0
    .endif
    move.l %d0,-(%sp)
    move.l %a5,-(%sp)
    move.l %a2,-(%sp)
    jsr object_prepare_entry
    lea 20(%sp),%sp
    move.l (%sp)+,%a1
    move.l (%sp)+,%a0
    tst.w %d0
    beq .Ldraw_fail
    bra .Ldraw_metadata
.Ldraw_success:
    move.w (%sp),g_object_misses
    move.w %d3,%d0
    addq.w #1,%d0
    bra .Ldraw_return
.Ldraw_fail:
    move.w (%sp),object_fast_failed_misses
    moveq #0,%d0
.Ldraw_return:
    addq.l #4,%sp
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

    .ifdef BLAST_NATIVE
/* Stable priority partition, written directly as offset/dx commands. The
 * source candidates retain original order; only native Pac-Man is skipped. */
    .macro collect_visit next
    tst.b 9(%a0)
    bne \next
    move.w 12(%a0),%d0
    sub.w %d2,%d0
    addi.w #0xe0,%d0
    cmpi.w #0x1c0,%d0
    bcc \next
    subi.w #0xe0,%d0
    btst #6,6(%a0)
    beq .Lcollect_low\@
    cmpi.w #8,%d4
    bcc .Lcollect_low\@
    move.w %d1,(%a3)+
    move.w %d0,(%a3)+
    addq.w #1,%d4
    bra \next
.Lcollect_low\@:
    move.w %d1,(%a1)+
    move.w %d0,(%a1)+
    addq.w #1,%d3
    cmpi.w #32,%d3
    beq .Lcollect_done
    .endm
    .align 2
    .globl object_collect_68k
object_collect_68k:
    movem.l %d2-%d6/%a2-%a4,-(%sp)
    move.w 38(%sp),%d2
    lea R6+0x2400,%a2
    move.l %a2,%a0
    lea object_draw_list,%a3
    lea 32(%a3),%a1
    moveq #0,%d3
    moveq #0,%d4
    moveq #0,%d5
    moveq #0,%d6
    move.b R6+0x20dc,%d5
    beq .Lcollect_candidates
    tst.b g_pac_native
    beq .Lcollect_primary_ready
    lea 32(%a0),%a0
    moveq #32,%d6
    subq.w #1,%d5
    beq .Lcollect_candidates
.Lcollect_primary_ready:
    subq.w #1,%d5
.Lcollect_primary:
    move.w %d6,%d1
    collect_visit .Lcollect_primary_next
.Lcollect_primary_next:
    lea 32(%a0),%a0
    addi.w #32,%d6
    dbra %d5,.Lcollect_primary
.Lcollect_candidates:
    move.w object_candidate_count,%d5
    beq .Lcollect_done
    subq.w #1,%d5
    lea object_candidate_offsets,%a4
.Lcollect_candidate:
    move.w (%a4)+,%d1
    lea (%a2,%d1.w),%a0
    collect_visit .Lcollect_candidate_next
.Lcollect_candidate_next:
    dbra %d5,.Lcollect_candidate
.Lcollect_done:
    moveq #0,%d0
    move.w %d4,%d0
    swap %d0
    move.w %d3,%d0
    movem.l (%sp)+,%d2-%d6/%a2-%a4
    rts
    .endif
