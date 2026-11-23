> Historical technical README preserved on 22 November 2026. Some status statements below are superseded by [the live issue log](../ISSUES.md), particularly forest ghost visibility. See the [public README](../README.md) for the project introduction.

# Pac-Land MD — development conversion

Current issues and verification are maintained in [ISSUES.md](../ISSUES.md).
The internal working log is not published; see the [documentation index](README.md).
Older handoff entries describe earlier builds.

[Project Blast Processing](blast-processing.md) records the native engine
and performance evidence. The compact collector is now the mainstream private
build at `dist/Pac-Land-MD.bin`, promoted at the user's request on 21 November.
It includes the return-camera correction from `build/return-camera/Pac-Land-MD.bin` (SHA256
`45D216DCE03675C410CA49EA7525C0BD219838DFE9287EB93E96BF6D3D368BFF`).
Shared native systems cover all 32 layouts. Return trips now mirror the
outward camera lead so the pursuer normally trails offscreen to the right. Collector benchmarks before this camera correction
reached 60 FPS in round 2 and 56 FPS on round 4 logs. Sustained 60 FPS
throughout gameplay remains open.
PAL performance is deprioritized. The next reported hotspot is many ghosts
on return trips and later layouts, rather than the now-acceptable logs.

The private development cartridge is `dist/Pac-Land-MD.bin`: 4,194,304 bytes,
320 × 224. This is a native SGDK port using statically recompiled original
arcade gameplay code, converted scenery and a Z80 music/effects driver.
It remains a development build. The attract-mode DMA fix passed the user's targeted physical-console test.
Broader hardware release testing remains.
Generated ROMs and arcade-derived resources stay private and ignored.

The current build includes the original player/object logic, scoring, lives,
trip endings, princess/boots sequences, arcade title and attract gameplay.
Elapsed-time scheduling preserves game speed in busy NTSC/PAL replays;
the regular build still drops drawing frames in busy scenes. Packed tall sprites reduce
object-drawing CPU cost by about 9% in a frozen benchmark and free 4 KiB RAM.
The round 2 pool-floor report is closed after the user stopped seeing it and
a controller-driven replay captured no corruption. The smaller SEGA-style PEZZ82 boot logo is eaten
by Pac-Man; its current chomp audio is synthesised and awaits a reference WAV.

Latest fixes: Japanese red cap, native white flying-boot outline, door colours
through approach/return/departure, and options navigation. Repeated attract
corruption remains open: 12 NTSC + 12 PAL/Japan/random cycles retained title
pixels, palettes and heap, but the reported progressive corruption was not
reproduced. The latest collector retains stable palettes and heap over 12 NTSC
attract cycles. Compare matching fixtures in the
[performance evidence](blast-processing.md).
Hard-reset after replacing ROM; old save states contain an incompatible RAM layout.

## Play and options

Press Start to skip the fan-project boot notice or open Trip selection from the title.
Choose Trip 1–5 with Left/Right or A/C. Jump (normally B) or Start confirms;
Up returns to the title. Any-button jump mode accepts A, B or C.
The last choice is saved. Trip randomisation keeps your chosen trip first.
Options, sound test, title prompts, boot notice and pause use Namco lettering.
Scene changes briefly blank the display while graphics load, then reveal a complete
frame. The original TRIP/READY card is preserved; title redraws stay hidden.
B on the title opens options. USA/Japan artwork, 1–9 starting lives, controls,
pixel aspect, attract demos and randomiser settings are available.
With ATTRACT DEMO OFF, idle playback stays on the title and high-score screens. Life heads
show spare lives: three lives means two heads, then one, then none on the last life.

| Action | Controls |
| --- | --- |
| Default movement / jump | D-pad or A/C movement; B jump |
| Options selection / value | Up/Down; Left/Right or A/C |
| Randomiser details | A on RANDOMISER |
| Start game / return from options | Start / B |
| Sound test | Left/Right select; A play; C stop; B return |
| Next / previous layout (development shortcut) | Hold Start and press Up / Down |
| Game over → title | Start |

Control presets also offer D-pad+B, arcade A/C+B and D-pad+A/B/C jump.
Controller 1 is sampled every VInt; short presses survive a late drawing frame.
Title Up/Down selects original/wide pixels; options provide the same setting.

Randomiser OFF/TRIPS/FULL presets remain, with independent trip, enemy and
item switches in the detail menu. The hexadecimal seed repeats remix choices.
Trips remain complete four-round groups. Enemy remix currently varies ghost
identities; item remix varies fruit and valid hidden-item triggers, not every
possible enemy/item type. Enemy and item choice streams are independent.

Settings and high scores save automatically to battery SRAM when leaving
options or starting a game, and at round-end/game-over boundaries. Emulator
SRAM support is needed to retain them between sessions. Two versioned records
with CRC and commit markers recover from a damaged latest record. Hard-reset
persistence and invalid-record recovery pass in Genesis Plus GX; physical
SRAM and front-end save-file handling have not been tested.

The sound test lists all 32 non-empty original driver requests. Playback tests
check output and stopping; exact audio fidelity still needs listening review.

Current checks include gameplay 10/10, options, attract, title/right-edge,
28 full/C/native sprite-command comparisons, door transition and 1,592 mask
pixels, buffered input, SRAM recovery, randomiser independence and sound test.
All 32 original endings pass for both designs, with bonus/boots/respawn checks.
These use injected finish/pit coordinates and do not prove a normal full-route
play-through. See `build/object-optimisation-*.txt` and `build/trip-sequences.json`.
Native hot leaves also match 5,120 randomized/boundary cases and all 65,536
terrain-lookup inputs against the independent original-CPU interpreter.

## Bring your own ROM maker

Choose a **small source-only edition** if you already have the development tools,
or an **all-in-one offline edition** with Python, Node, compilers and libraries
included. The bundled edition has separate Windows x64 and Linux x64 downloads.
Both validate your own arcade ZIP and generate the same cartridge locally.
See the [download and edition guide](distribution/README.md) for sizes,
requirements and instructions, or the [developer guide](rom-maker.md).
The packages include no arcade ROM or complete Mega Drive cartridge.

## Rebuild from a clean source checkout

Tested development dependencies: Node **24.22.3**, Python **3.13.3**,
romdevtools **0.155.0**, its m68k toolchain **0.4.2**, Genesis Plus GX package
**0.15.2**, XGM2 package **0.2.2**; MAME **0.264** in Ubuntu WSL. Current MAME
source definitions were also inspected and matched against the supplied set.
The SGDK headers/runtime are those pinned by the toolchain package and lockfile.
Host C tests used GCC; GCC is not needed to compile the cartridge.

Install Node 24 or newer from its official distribution, Python, Ubuntu WSL,
and the legitimate tools. In Ubuntu, `apt-get install mame xvfb` supplies the
reference emulator. From this project directory on Windows:

```powershell
npm ci --ignore-scripts
python -m pip install -r requirements.txt
```

In a separate terminal, start romdev:

```powershell
./tools/start-romdev.ps1
```

This session uses port **7332**. The endpoint is
`http://localhost:7332/mcp`; the same tools are available as
`POST http://localhost:7332/tool/{name}`. `ROMDEV_URL` overrides the HTTP base
URL in the project scripts. A Windows Node import hook handles a path-import
bug observed in romdevtools 0.155.0. It changes no system configuration or npm
package source. Headless building/emulation does not need the optional SDL
playtest/native GPU install scripts.

Then run the complete local pipeline:

```powershell
./tools/rebuild.ps1 -Rom 'M:/mame/pacland.zip'
```

The supplied ZIP must match `tools/rom_layout.json`. The script validates all
19 files using size, CRC32 and SHA1, extracts source graphics/tables, captures
each round in local MAME, validates visible scenery, records the original
music excerpt, converts it using romdev, regenerates native resources,
compiles the cartridge and runs pipeline/emulator checks. It copies the input
ZIP only to ignored `build/roms/` for WSL accessibility. Nothing is uploaded.

Linux / headless equivalent (no PowerShell): `tools/linux-dev.sh recomp`
regenerates the recompiled object system (`tools/recomp.sh`) and then builds
and tests. Development tools: `tests/hybrid_smoke.mjs`, `tests/hybrid_tour.mjs`
(screenshots), `tests/bench.mjs` (in-cartridge CPU benchmark).

Normal builds now select the native engine, short-integer kernels and bounded
NTSC early-VBlank path automatically. `PL_BLAST=0` retains the older engine for
comparisons, with a separate `PL_OUTPUT_DIR` required. Lab and timing-probe
builds also require separate output directories.

For code-only edits with generated assets already present:

```powershell
node tools/build.mjs
python tests/test_pipeline.py
node tests/emulator.mjs
gcc -Wall -Wextra -Werror -I include src/simulation.c tests/simulation_test.c -o build/simulation-test.exe
./build/simulation-test.exe
```

Linux users can run the same Python/Node steps and `capture_reference.py`
directly. The convenience `rebuild.ps1` wrapper specifically uses Windows/WSL.

The screenshot comparison figure additionally needs the two presentation
screenshots produced by the emulator comparison procedure; these are private
generated outputs, not source assets. See `tools/make_comparisons.py`.

## Source and local copyrighted data

Original code/tooling is separate from all arcade-derived data. The source
archive excludes `res/generated/`, `build/`, `dist/` and reference ROMs. A fresh
checkout requires the user's legitimate ZIP and local reference execution.
The local output directory includes generated material for the user's review;
it is **not a public distribution package**. Do not publish the ROM, graphics,
music, decoded game tables or screenshots without the appropriate permission.

No ROM was downloaded. Jotego HDL was studied as a hardware specification and
was not incorporated into this cartridge. SGDK notices are retained in
`LICENSE-SGDK` and `COPYING-SGDK-RUNTIME`; its vendored files retain their own
notices. The original project's MIT licence does not cover Namco content.

## Continue development

The next work is translating original object handlers, not inventing enemy
behaviour. `docs/arcade_analysis.md` records recovered addresses and table
formats. `res/generated/levels/manifest.json` contains the 917 raw object
records and interaction records with ROM offsets. Preserve original logical
activation coordinates when extending visibility. Then implement event-driven
audio, special masking, scoring and full trip scripts, and verify every round
through actual completion against MAME.

Ghosts remain fully visible in front of background trees. Front-layer trees hide
the body while retaining the arcade priority eyes/outlines. Extra masks
are bounded by sprite/upload limits; extreme crowds can still drop frames.
