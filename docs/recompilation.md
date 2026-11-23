# Hybrid object system: recompiled arcade routines (15 November 2026)

The cartridge keeps the native Pac-Man (input, physics, terrain), native
scenery rendering and the wide viewport. Everything else that the arcade's
per-frame gameplay loop does with **objects** runs as the original 6809 code,
statically translated to C and executed against an image of the arcade RAM.

## What runs recompiled

Per arcade frame, in the order of the original main loop (E9FA..EA56):

| Step | Original | Cartridge |
|---|---|---|
| object slots: activation gates + update handlers | 8230..82B2 + handlers | native loop with identical semantics (`src/recomp_rt.c slot_pass`), handlers recompiled |
| score / timer bookkeeping | EBF1 | recompiled |
| camera, round-end detection (sets 2062) | D130 | recompiled; its arcade video-RAM column drawing (D2B3/D3A2) is skipped on the cartridge |
| sprite-list build | C1FB/C2E7 | native translation that emits Mega Drive sprites directly (`src/objects.c`) |
| bonus events | CE62 | recompiled |

Round start follows E975..E994: EBC8, CFE9, 8010 (80DA after a lost life),
CE49, D0F0. New-game state mirrors E8C8..E909. Not yet included: the trip
intro (BEF2), the walk-in (F0F1) and other cut-scenes, the MCU (inputs are
native; sound commands written to 6A86.. are ignored), and the player
state machine 8731.

Interface between native and arcade (`src/game.c arcade_tick`): before each
tick the native player writes X (240C), Y (2409) and X velocity (2411) into the
arcade player slot; after it, any change the original code made (hydrants
pushing Pac-Man back) is read back. 2063 (set by ghost routines when Pac-Man
is caught) and 2062 (round end, D16D) drive native death / round clear.

## Toolchain (tools/)

| File | Purpose |
|---|---|
| `m6809.py` | structured 6809 decoder (agrees with MAME's disassembly on 14,491 instructions) |
| `dis6809.py` | recursive-descent disassembler for reading routines (the linear `work/main6809.asm` loses alignment after tables) |
| `closure6809.py` | the routine set needed (entries: type tables 8168/81CC/82C0/8324 and the per-frame/set-up routines), jump-table resolution, hazard report; 348 routines, ~7,150 distinct instructions, 42 jump tables, no banked code |
| `recomp6809.py` | 6809 → C: one C function per routine, gotos for branches, jump tables as switches, per-flag liveness and interprocedural flag analysis (flags are C locals; only C068 returns Z, only AAC9 reads incoming C), access-site specialisation from the profile |
| `recomp_profile.py` | host profile of every memory-access site (RAM-only / ROM-only / mixed) across all rounds |
| `sim6809.py` | independent reference 6809 interpreter (Python) used by the tests |
| `prepare_recomp.py` | cartridge data: arcade program image, arcade sprites as MD tiles, sprite colour LUT, per-round sprite palette chosen by weighted error minimisation over the sprites actually drawn in host runs |
| `recomp.sh` | runs the whole chain and the tests |

Runtime: `include/recomp_rt.h`, `src/recomp_rt.c`. On the cartridge the 6809
registers live in 68000 registers d5-d7/a3-a5 (GCC global register variables),
saved/restored around each entry from ordinary C.

Stubbed routines: 8731 (player update, native) and DE46 (cooperative-task
yield). DE46 is only reached from F272, a wait for the previous bonus event
(2219); the stub completes the wait.

## Validation

* `tests/recomp_diff.py`: the recompiled C (host build, same specialised code
  as the cartridge) against `sim6809.py` running the original ROM, from MAME
  round-entry RAM, with the player walking through the round. All 32 rounds
  byte-identical (300 frames each; 400-700 frames on spot checks; ~3,000
  6809 instructions per frame with ghosts active). Bytes 3600-36FF are excluded
  as stack scratch: no tasks exist in this model and the native loop only
  performs the original's stack pushes when a handler actually runs.
* `tests/recomp_setup_vs_mame.py`: round set-up from zeroed RAM versus MAME's
  RAM at round entry: every slot type in all 32 rounds matches (1,800+ slots).
  Remaining record differences are fields changed after set-up by running code
  (animation codes/flips, the companion slot written by the player routine).
* `tests/emulator.mjs` adds a cartridge regression: round 1 hydrant stops a
  walking Pac-Man at X=996, objects are drawn, no translation trap.
* Both reference implementations are by the same author, so the differential
  tests prove translation fidelity, not 6809 semantics. MAME ground truth so
  far: set-up (above) and the visible behaviour (ghosts in cars, hydrants,
  logs). A per-frame MAME comparison (same inputs) is the next validation step.

## Performance (Genesis Plus GX, NTSC)

With the arcade player routine 8731 running, a walking frame in a busy round
costs roughly 210-260 of 262 lines: recompiled arcade code ~100, native
sprite builder ~40, object loop ~15, renderer and DMA ~25. Most rounds run at
or near 60 fps; the busiest (9, 19, 25, 27) drop 20-28% of frames. See the
session 4 section of HANDOFF-CLAUDE.md for what was optimised and what is
next (keeping D in one 68000 register is the largest remaining lever).

## Known deviations

* The arcade player routine 8731 is run (since session 3), so platforms,
  logs, springboards, steps and water are the original behaviour; the native
  renderer only draws Pac-Man from slot 2400.
* `slot_pass` does not write the PSHS/JSR bytes of the original object loop
  into the stack page (no handler reads them; `RC_STACK_IMAGE` restores it).
* C031 is hand-written C (same results, `NATIVE` in `tools/recomp6809.py`).
* Sprite palette: one 16-colour line shared by Pac-Man and all objects of a
  round (up to 8 object colours, chosen per round). Colour approximation is
  visible on some objects.
* Sprite priority: ordinary VDP priority (objects behind high-priority
  foreground tiles); the CUS29 cookie-cut/lantern effects are not reproduced.
* The arcade's 9-bit sprite X wrap is not reproduced (objects inside the
  wider viewport are drawn at their true position).
* Sound commands from the object code are ignored.
