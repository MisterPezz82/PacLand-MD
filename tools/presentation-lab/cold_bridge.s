/* The translated unit reserves 6809 state in callee-saved 68000 registers.
 * Cold setup arithmetic can run before its inner RC_SAVE. Preserve the real
 * caller's complete registers at the assembly boundary, outside that unit. */
 .text
 .align 2
 .macro cold_entry name,args
 .globl \name
\name:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 .if \args == 2
 move.l 52(%sp),-(%sp)
 move.l 52(%sp),-(%sp)
 .elseif \args == 1
 move.l 48(%sp),-(%sp)
 .endif
 jsr \name\()_impl
 .if \args == 2
 addq.l #8,%sp
 .elseif \args == 1
 addq.l #4,%sp
 .endif
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 rts
 .endm
 cold_entry rc_setup,2
 cold_entry rc_new_game,1
 cold_entry rc_round_start,1
 cold_entry rc_death_tick,0
 cold_entry rc_round_end_start,0
