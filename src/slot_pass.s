/* Native object scan. a2=RAM; a3/a4/a5=X/Y/U; d5/d6/d7=S/A/B.
 * Eight-byte ROM descriptors hold gate width, additive bias and update fn.
 * Preserve original slot order, handler registers, and emulated stack depth. */
    .text
    .align 2
    .globl rc_slot_pass
rc_slot_pass:
    movem.l %d2-%d4/%a6,-(%sp)
    move.w 22(%sp),%d0
    move.w 26(%sp),%d3
    lea (%a2,%d0.w),%a6
    move.w %d0,%a3
    move.b %d3,%d7
    tst.w %d3
    beq .Lpass_done
    move.w 0x240c(%a2),%d4
    lea rc_slot_info,%a1
.Lpass_loop:
    moveq #0,%d0
    move.b 1(%a6),%d0
    lsl.w #3,%d0
    move.w (%a1,%d0.w),%d1
    beq .Lpass_next
    cmpi.w #2,%d1
    bls .Lpass_small
    move.w 12(%a6),%d7
    sub.w %d4,%d7
    move.w %d7,%d2
    add.w 2(%a1,%d0.w),%d2
    cmp.w %d1,%d2
    bcc .Lpass_next
    bra .Lpass_run
.Lpass_small:
    cmpi.w #1,%d1
    beq .Lpass_count
    tst.b 2(%a6)
    beq .Lpass_next
.Lpass_count:
    move.b %d3,%d7
.Lpass_run:
    move.w %d5,%d2
    subi.w #9,%d5
    move.l %a6,%a3
    suba.l %a2,%a3
    move.b 1(%a6),%d6
    add.b %d6,%d6
    move.w #0x8324,%a4
    move.l 4(%a1,%d0.w),%a0
    jsr (%a0)
    lea rc_slot_info,%a1
    move.w %d2,%d5
    move.w 0x240c(%a2),%d4
.Lpass_next:
    lea 32(%a6),%a6
    subq.w #1,%d3
    bne .Lpass_loop
    move.l %a6,%a3
    suba.l %a2,%a3
    clr.b %d7
.Lpass_done:
    movem.l (%sp)+,%d2-%d4/%a6
    rts
