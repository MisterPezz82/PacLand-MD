# Adaptive presentation — 21 November 2026

Tested candidate: build/presentation-adaptive-v3/Pac-Land-MD.bin. Mainstream,
the prior fixed-overlap candidate and the ROM-maker kit remain unchanged.
Do not use the failed presentation-adaptive or presentation-adaptive-v2 ROMs.

## Switching policy

Work is measured over input, simulation and drawing, subtracting the wait for
ownership of the previous frame's graphics sources. Sound mailbox work after
publication is outside this sample. SDK subticks are approximate around VBlank;
these are conservative thresholds, not precise millisecond deadline promises.

- Enter overlap at 900 subticks, about 70% of an NTSC field / 11.7 ms.
- Leave only after 24 consecutive samples at or below 700 subticks, about 55%
  / 9.1 ms, and at least 30 overlapped samples since entry.
- Thus a busy period stays overlapped for at least about half a second. Noise
  between the thresholds does not repeatedly switch modes.
- Sudden first spikes can still miss a frame; the policy reacts to measured
  work. It does not predict every coming workload or guarantee locked 60.
- PAL, demos, menus, cold transfers and oversized queues retain the existing
  fallback. Mode resets at scene loads and menu entry.

The light path waits after publishing; the busy path begins the next simulation
update while its predecessor waits for VBlank, fencing before graphics reuse.
The simulation clock accounts for that one predicted tick. It must not treat
retiring the prediction as extra catch-up debt. Publication uses the next safe
blank rather than opportunistic late commits in this adaptive variant.

## Verified response and cadence

Actual VRAM sprite-table response to eight separate one-frame B taps:

| Mode | Simulation response | Visible sprite response |
| --- | --- | --- |
| Mainstream | 2 fields | 3 fields |
| Adaptive, ordinary quiet scene | 2 fields | 3 fields |
| Adaptive, forced overlap | 2 fields | 4 fields |

Thus quiet work recovers the extra frame; the overlap path retains its roughly
16.7 ms visual delay. The quiet test is ordinary round-1 idle play, not a claim
about every possible scene's input latency.

A 540-field frozen scheduling test switches both directions repeatedly: one
physical presentation in every field, at most one predicted simulation tick,
and exactly 540 total updates once back in light mode. Entering prediction can
compute 91 updates over a 90-field interval; retiring it computes89 in the next
90, while presentation remains continuous. A separate actual CPU load spike
activates the automatic policy, remains overlapped through the minimum interval,
and retires after sustained quiet work. Regional sampling gives599/600 NTSC
updates while the initial prediction retires,731/600 PAL; no sustained speedup.

Ordinary controller return attempts, at most6000 fields, all complete:

| Round / mode | Presentations / crowded fields | Nominal FPS |
| --- | --- | --- |
| 12, normal | 1325/1326 | 59.95 |
| 12, random | 1204/1208 | 59.80 |
| 28, normal | 906/906 | 60.00 |
| 28, random | 836/836 | 60.00 |

Rows include active chunks with at least three nearby ghosts using the broad
candidate window. They exclude empty respawn chunks and are not a strict2000
consecutive-active-field gate. The controller policy and seed4660 match prior
experiments; trip shuffle is OFF and enemy/item shuffle ON for random cases.
Adaptive input timing changes deaths/trajectories, so these are not identical
frame-by-frame routes against fixed overlap.

Matched warm imported fixtures:

| Round | Fixed overlap | Adaptive |
| --- | --- | --- |
| 1 | 120/120 | 120/120 |
| 2 | 110/110 | 110/110 |
| 3 | 120/120 | 118/119 |
| 4 | 89/89 | 88/89 |
| 9 | 5/5 | 5/5 |
| 11 | 119/120 | 119/120 |
| 25 | 120/120 | 120/120 |
| 27 | 120/120 | 119/120 |

The five-field round9 fixture dies too early to establish performance. Isolated
misses in other samples remain. Adaptive timing is not a universal60FPS claim.

## Correctness and the cold-call defect

The first implementation recovered quiet response and passed scheduling tests,
but longer play failed after respawn. Disassembly and a real68000 ABI test show
rc_setup can overwrite its caller's D5: setup arithmetic occurs before its inner
RC_SAVE in the unit that reserves callee-saved registers for6809 state. A main
loop keeping its denominator in D5 then loses that value. Moving the logic clock
into its own state did not fix this unrelated register corruption.

Five cold public entrypoints now have assembly boundaries saving all eleven
callee-saved registers, forwarding stack arguments, and retaining D0 results:
rc_setup,rc_new_game,rc_round_start,rc_death_tick,rc_round_end_start. The old v2
call is a negative control that changes D5; all five corrected calls preserve
all register bits. The healthy per-frame model path does not incur this bridge.
This correction currently exists in the adaptive laboratory injection only;
retain it when promoting or refactoring the engine.

Candidate checks passed: gameplay10/10 including death/restart/music PCM/all32
maps; quick B edge;24door positions;64USA/Japan endings;12attract cycles after
normal play with identical repeated title pixels/CRAM and2992free heap bytes.
Six physical DMA publication phases transfer two2KiB/32-op packets each, with
every VRAM word exact, back-queue ownership intact and zero DMA overruns or
duplicate commits. Separate checksum routes verify RAM source ownership across4435packets, with
zero mutations, duplicate commits, allocation failures or DMA overruns. Their
expensive instrumentation must not be used for FPS comparisons.

## Artifacts and rebuild

Set PL_PRESENT_PIPELINE=adaptive and PL_OUTPUT_DIR to a separate build directory,
then use the usual tools/build.mjs command/import hook. Output into dist is
refused. PL_PIPE_VERIFY_LEASE=1 adds expensive source checks; PL_PRESENT_PROBE=1
adds the standalone DMA probe. g_pipe_force is a laboratory test hook (0auto,
1synchronous,2overlap), not a new player-facing option.

Evidence: adaptive-v3-{game,transitions,latency,busy-latency,input,door,trips,
attract,pacing}.txt; adaptive-v3-warm-matrix.json; adaptive-v3-route12/28 reports;
adaptive-cold-abi.json; adaptive-check-packet.json; adaptive-check-route12 reports.
See tools/presentation_lab.mjs, tools/presentation-lab/adaptive.c and
cold_bridge.s for the source injection and ownership policy.

Candidate SHA256: `e781994ff15dab06c225854b5106e427749b1ae72e5cc24d070f2102bad123c6`.
