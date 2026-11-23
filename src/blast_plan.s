/* Dense gameplay entity plan, prepared once per scene. Native handlers have
 * normal C signatures and return the number of contiguous entities consumed.
 * Only the fallback boundary materializes the arcade register inputs. */
 .text
 .align 2
 .globl blast_entity_pass
blast_entity_pass:
 movem.l %d2-%d4/%a6,-(%sp)
 lea blast_entities,%a6
 move.w blast_entity_count,%d3
 beq .Lentity_done
 move.w 0x240c(%a2),%d4
.Lentity_loop:
 move.l (%a6),%a0
 move.w 4(%a6),%d1
 bmi .Lentity_span
.Lentity_gate:
 cmpi.w #2,%d1
 bls .Lentity_small
 move.w 12(%a0),%d7
 sub.w %d4,%d7
 move.w %d7,%d0
 add.w 6(%a6),%d0
 cmp.w %d1,%d0
 bcc .Lentity_next
 bra .Lentity_run
.Lentity_small:
 cmpi.w #2,%d1
 bne .Lentity_count
 tst.b 2(%a0)
 beq .Lentity_next
.Lentity_count:
 move.b 17(%a6),%d7
.Lentity_run:
 move.l 12(%a6),%d0
 beq .Lentity_legacy
 /* Avoid an unsuccessful ABI crossing for a car in an unconverted state. */
 moveq #0,%d1
 move.b 18(%a6),%d1
 beq .Lentity_native
 cmpi.w #12,%d1
 bne .Lentity_car_check
 cmpi.b #1,19(%a0)
 bls .Lentity_native
 cmpi.b #8,19(%a0)
 bcs .Lentity_legacy
 cmpi.b #10,19(%a0)
 bls .Lentity_native
 cmpi.b #16,19(%a0)
 beq .Lentity_native
 cmpi.b #17,19(%a0)
 beq .Lentity_native
 bra .Lentity_legacy
.Lentity_car_check:
 cmpi.w #5,%d1
 bhi .Lentity_native
 move.b 19(%a0),%d1
 andi.b #0xf0,%d1
 cmpi.b #0x40,%d1
 bne .Lentity_legacy
.Lentity_native:
 move.l %d0,%a1
 moveq #0,%d0
 move.w %d3,%d0
 move.l %d0,-(%sp)
 move.l %a0,-(%sp)
 jsr (%a1)
 addq.l #8,%sp
 tst.w %d0
 beq .Lentity_legacy
 add.w %d0,g_blast_native
 move.w 0x240c(%a2),%d4
.Lentity_advance_span:
 subq.w #1,%d0
 beq .Lentity_next
 sub.w %d0,%d3
 move.w %d0,%d1
 lsl.w #3,%d1
 lsl.w #4,%d0
 add.w %d1,%d0
 adda.w %d0,%a6
 bra .Lentity_next
.Lentity_legacy:
 /* Whole reference actors own their graphics until they return. Invalidate
  * their descriptor before the call, including the two attachment slots. */
 moveq #0,%d0
 move.b 18(%a6),%d0
 cmpi.w #5,%d0
 bhi .Lentity_legacy_ready
 move.l (%a6),%a0
 suba.l %a2,%a0
 lea object_fast_entries-0x2400,%a1
 adda.l %a0,%a1
 tst.w %d0
 beq .Lentity_legacy_invalidate
 tst.b 31(%a1)
 beq .Lentity_legacy_ready
 clr.b 31(%a1)
 clr.b 63(%a1)
 clr.b 95(%a1)
.Lentity_legacy_invalidate:
 move.b #1,25(%a1)
 move.b #1,57(%a1)
 move.b #1,89(%a1)
.Lentity_legacy_ready:
 addq.w #1,g_blast_fallback
 move.w %d5,%d2
 subi.w #9,%d5
 move.l (%a6),%a3
 suba.l %a2,%a3
 move.b 18(%a6),%d6
 add.b %d6,%d6
 move.w #0x8324,%a4
 move.l 8(%a6),%a0
 jsr (%a0)
 move.w %d2,%d5
 move.w 0x240c(%a2),%d4
.Lentity_next:
 lea 24(%a6),%a6
 subq.w #1,%d3
 bne .Lentity_loop
.Lentity_done:
 movem.l (%sp)+,%d2-%d4/%a6
 rts
.Lentity_span:
 move.w %d4,%d0
 sub.w 20(%a6),%d0
 cmp.w 22(%a6),%d0
 bcs .Lentity_span_inside
 moveq #0,%d0
 move.b 19(%a6),%d0
 bra .Lentity_advance_span
.Lentity_span_inside:
 andi.w #0x7fff,%d1
 bra .Lentity_gate
