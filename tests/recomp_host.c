/* Host harness for the recompiled object routines.
 * usage: recomp_host maincpu.bin ram.bin out.bin round setup(0/1/2) frames walk
 *   setup 0: start from snapshot; 1: run 8010 set-up first; 2: run 80DA re-init first;
 *   3: zero RAM, rc_new_game(3) then the full rc_setup(round) sequence
 *   walk: pixels added to player X (240C) before every frame.
 * Writes RAM (0000-37FF) after set-up and after each frame, plus M6 and bank. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "recomp_rt.h"
static void rc_jsr_entry(u16 a) { rS = 0x3700; rS -= 2; WR16(rS, 0xFFFF); rc_call(a); }

static u8 rom[0x20000];
static void dump(FILE *f) { fwrite(R6, 1, 0x3800, f); fwrite(M6, 1, sizeof M6, f); fputc(rc_bank, f); }

int main(int argc, char **argv) {
    if (argc < 8) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f || fread(rom, 1, sizeof rom, f) != sizeof rom) return 2; fclose(f);
    f = fopen(argv[2], "rb"); if (!f || fread(R6, 1, 0x3800, f) != 0x3800) return 2; fclose(f);
    if (atoi(argv[5]) == 3) { ROM6 = rom; rc_new_game(3); }
    ROM6 = rom;
    int round = atoi(argv[4]), setup = atoi(argv[5]), frames = atoi(argv[6]), walk = atoi(argv[7]);
    FILE *out = fopen(argv[3], "wb");
    if (setup == 3) rc_setup((u16)round, 0);
    else if (setup) { R6[0x2101] = (u8)round; rc_jsr_entry(setup == 2 ? 0x80DA : 0x8010); }
    dump(out);
    /* walk: >0 hold right, <0 hold left; a jump press every 40 frames */
    /* optional RC_POKE="addr=value@frame": write arcade RAM once (diagnostics) */
    const char *pk = getenv("RC_POKE"); unsigned pa = 0, pv = 0, pf = 0;
    if (pk) sscanf(pk, "%x=%x@%u", &pa, &pv, &pf);
    int jp = getenv("RC_JUMP") ? atoi(getenv("RC_JUMP")) : 40;   /* jump press period */
    for (int i = 0; i < frames; i++) {
        if (pk && (unsigned)i == pf) R6[pa & 0x3FFF] = (u8)pv;
        rc_input(walk < 0, walk > 0, walk && (i % jp) < 4);
        rc_irq(); rc_frame(); dump(out);
        /* optional RC_DEATHS=1: play the death sequence like src/game.c (for
         * prepare_recomp.py coverage only; the reference interpreter in
         * recomp_diff.py does not model it) */
        if (getenv("RC_DEATHS") && R6[0x2063]) {
            int t;
            rc_death_start();
            for (t = 0; t < 250 && i < frames; t++, i++) {
                u8 done; rc_irq(); done = rc_death_tick(); rc_jsr_entry(0xC1FB); dump(out);
                if (done) break;
            }
            rc_setup((u16)round, 1);
        }
    }
    fclose(out);
#ifdef RC_PROFILE
    if (argc > 8) { FILE *pf = fopen(argv[8], "wb"); fwrite(rc_prof, 1, sizeof rc_prof, pf); fclose(pf); }
#endif
    return 0;
}
