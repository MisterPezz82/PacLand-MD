# Pac-Land MD

An unofficial, AI-assisted Pac-Land conversion for the Sega Mega Drive / Genesis.

I wanted to play Pac-Land on the Mega Drive. This seemed like a fairly reasonable idea until Pac-Man reached the logs and apparently decided he was being paid by the hour.

This project has grown from a rough conversion into a native cartridge with original arcade gameplay running alongside increasingly native Mega Drive systems. It is still a development build. There are bugs, missed frames and unfinished comparisons, and this README is partly an introduction and partly an admission of how we got here.

**AI disclosure, before anyone gets to the bottom and feels ambushed:** Codex and Claude have provided substantial code, tooling, analysis and debugging assistance. I have directed the work, played the builds, supplied feedback and reference material, made the visual and gameplay decisions, and tested on my own console. I am not presenting this as a lovingly hand-written assembly port that I personally crafted over twenty years. Sorry if the AI bit puts you off; I would rather be clear about it.

## What is here?

The current private cartridge is a 3.875 MiB (4,063,232-byte), 320 × 224 SGDK build covering the original 32 layouts. It combines native code with statically recompiled arcade routines; it does not run a complete arcade machine emulator inside the Mega Drive.

Features include:

- Original player and object behaviour, scoring, lives, trip endings, princess sequences and flying boots.
- USA/Japan character artwork when the relevant local ROM input is supplied, plus original/wide pixel proportions.
- Arcade-style Trip 1–5 selection, 1–9 starting lives and several controller presets.
- Seeded whole-trip shuffle, ghost identity remix, and fruit/hidden-item variation.
- Title and attract gameplay, Namco-style menu lettering, pause with Exit to Title, and a sound test.
- SRAM settings and high scores, latched controller presses, and adaptive presentation for busy scenes.
- A modest extra springboard launch height for pool jumps; the map and normal jumps stay unchanged.
- A PEZZ82 fan-project boot sequence in which Pac-Man eats the logo. Obviously this was an essential engineering requirement.

The randomiser does not shuffle every possible enemy or item. Trip groups remain intact, and your selected trip comes first when trip shuffle is enabled.

## Build your own cartridge

There is no ready-made game ROM here. You supply your own Pac-Land arcade ROM set and the maker builds the cartridge on your computer. Please use ROM data you are entitled to use; the maker does not establish permission for you.

### Windows

1. Download **`Pac-Land MD ROM Maker.exe`** from the [Releases page](../../releases).
2. Double-click it. Windows or your antivirus may warn that it is new and unsigned; on the blue "Windows protected your PC" screen choose *More info*, then *Run anyway*.
3. Drag your `pacland.zip` onto the window, or press *Choose...*.
4. Press **Build my ROM**. Your cartridge is saved as `Pac-Land-MD.bin` in `Documents\Pac-Land MD`.

Nothing needs installing. The first time, the app downloads its own build tools (about 500 MB, checksum-verified, from their official sites) into its AppData folder; later builds reuse them. A build takes several minutes.

### Linux

There is no Linux download on the Releases page: the maker is this repository. Download it (the green *Code* button, then *Download ZIP*, or `git clone`), then in its folder run:

```sh
sh setup.sh                       # once: downloads the build tools into this folder
sh make-rom.sh /path/to/pacland.zip
```

The cartridge appears in `build/rom-maker-output`. The Linux route has been tested less than the Windows app; reports are welcome.

### Your ROM set

The maker accepts the MAME `pacland` set in merged, split or non-merged form and finds each file by checksum, so ZIP and file names do not matter. A merged `pacland.zip` already includes the Japanese character artwork; with a split set, keep `paclandj.zip` beside `pacland.zip`. If anything is missing, the maker tells you exactly which files it still needs. ROMs are checked locally and never uploaded.

The [ROM-maker guide](docs/rom-maker.md) covers advanced options. A full development checkout has a separate reference-capture/rebuild workflow, preserved in [development notes](docs/development-notes.md).

## Controls

The default accepts D-pad or A/C for movement, with B to jump. Options also provide D-pad+B, arcade A/C+B, or D-pad with any A/B/C button to jump.

| Screen | Controls |
| --- | --- |
| Title | Start opens Trip selection; B opens options |
| Trip selection | Left/Right or A/C selects; configured Jump or Start confirms; Up returns |
| Options | Up/Down selects; Left/Right or A/C changes values; B returns |
| Gameplay | Start opens pause |
| Pause | Start/B resumes; Up/Down selects; A/C confirms |
| Sound test | Left/Right selects; A plays; C stops; B returns |

In any-button-jump mode, A/B/C confirms Trip selection instead of navigating. Start skips the boot notice. Settings and high scores save automatically; your emulator or cartridge must support SRAM persistence. Hard-reset after changing builds because older save states can contain incompatible RAM layouts.

## How this became a project

### First, make it behave like Pac-Land

The early version had native movement, terrain and rendering, but recreating all the interactions from appearances was not enough. Ghosts, moving scenery, logs, platforms and the player are tied together in the original game. A plausible-looking approximation could still let Pac-Man fall through something he should stand on. Slight problem for a platform game.

The approach changed to statically recompiling the original 6809 object routines into C for the Mega Drive's 68000. The original player routine followed when its object-contact behaviour proved necessary. That recovered much more faithful behaviour, while native systems handled the console side.

Reference checks compared translated execution with an independent original-CPU interpreter and, where applicable, MAME. Those comparisons helped recover activation rules, fixed-point movement, scoring and scene sequences rather than making up replacement enemy behaviour. They did not magically make everything fast.

### Then, persuade it to move

Bringing more original logic across made busy scenes expensive. Early optimisations reduced register and memory overhead, cached graphics, packed sprites and avoided rebuilding unchanged scrolling data. These helped, but later testing found slowdowns the earlier short fixtures had missed.

The return-trip logs with power boots became a particular favourite. Sonic can race around this machine, so why can't Pac-Man manage a few logs? Thus: **Project Blast Processing**. Another blast processing moment, lol.

This grew into shared native systems for ordinary player movement, terrain, bridges, logs, frightened cars and items, active across all 32 layouts. Ordered activation plans retained original interaction order and offscreen effects. Planned art residency, compact draw commands, a body-frame cache and a smaller draw collector reduced rendering work. Special states and cold sequences still use original handlers; this is an evolving mixed engine.

In matched historical fixtures, round 2 rose from 30 to 60 FPS, while the heavy round-4 log case rose from roughly 25 to 56 FPS after the collector work. These are specific measurements, not a promise that the whole game is locked at 60.

A separate clock correction stopped slow drawing from also slowing gameplay. Later adaptive presentation overlaps work in busy scenes, then returns to the quicker-response path when there is headroom. It improved crowded return trips, with a one-field visible-response tradeoff while overlap is active. Some frames still miss their deadline. [Blast Processing](docs/blast-processing.md) and [adaptive presentation](docs/adaptive-presentation.md) record the evidence, rejected experiments and limitations.

### The bits that screenshots hide

Visual work has involved palette compromises, red caps, white boot outlines, doors, scenery edges, title transitions and menu redraws. My job has often been to play it and say, “that still looks wrong,” which has turned out to be quite useful. One report about the first “poop” was supposed to say “pool.” The bug tracker survived.

Audio also took several attempts. An early supposed perfect loop was actually silence after a capture mistake. That was discarded. The later Z80 driver handles music and effects, with fixes for disappearing instruments and the missing game-over stab. Exact listening comparisons remain unfinished.

The boot sound had its own audition: I chose the synthesised `waka1` candidate. It is original synthesis rather than a copied arcade recording or wavetable. The fan notice makes the unofficial status explicit before Pac-Man gets on with eating the branding.

Real hardware supplied another useful reality check. Repeated attract loops developed progressive graphics and colour corruption despite emulator checks looking healthy. Investigation isolated two title-screen VRAM clears that needed completion waits. I tested the minimal correction on an EverDrive Pro and Japanese Mega Drive in USA mode and confirmed that targeted fault stayed stable. That result supports that fix; it does not certify every later build or hardware combination.

### And then the forest humbled us

The white ghost outlines and eyes over foreground trees were reported fixed too early. Some synthetic tests used eyes-only frames. A later “full ghost” fixture was actually purple Pac-Man. Yes, really. Hundreds of passing combinations cannot help when you have tested the wrong character.

Capturing real ghost objects eventually produced the right white contours and eyes. Then moving ghosts behind the trees brought the game to its knees. The opening-demo FPS checks had missed the expensive section. A controlled moving-composite comparison measured about 6.5 FPS with the feature and 60 FPS after removing it. Looking correct in a screenshot was apparently not the same as being playable. Another useful discovery, if a slightly embarrassing one.

So that version was removed completely. I would rather have shipped hidden ghosts than a slideshow with beautifully outlined ones. Its old test results remain development history, not evidence for the release build.

Then a cheaper idea arrived. The Mega Drive already hides low-priority sprites behind high-priority scenery for free, so there is no need to work anything out pixel by pixel. When a ghost overlaps a front tree, a second sprite is drawn in front of the trees holding only the ghost's outer edge, its dark line art and eyes, plus the pixels the arcade itself draws over its front layer, all in the ghost's own colours. The ordinary body stays behind and the trees hide it. In the open the two copies are identical, so there is no seam. The outline masks are prepared when the cartridge is built, so the console only copies them. In emulator tests the forest rounds kept the same frame counts as the build without the feature. It still needs real-console play-testing, and the busiest later rounds may show the occasional extra dropped frame.

The large Fairy Queen also briefly acquired a black dress when the room swapped sprite palettes. Her pink dress now has a reserved colour, while Pac-Man and the little fairies keep their existing pens. Both alternating queen palettes are handled so her skin stays the same colour. A small correction, and considerably less ambitious than trying to make every tree transparent.

## What still needs work

This is playable development work, with substantial automated coverage and real player feedback. It is not a finished, universally verified release.

Sustained NTSC 60 FPS, exact audio fidelity, some cross-sprite masking and round-19 lighting remain open. Ghost outlines through foreground trees are new and await real-console confirmation. Full uninterrupted trips, broader randomiser coverage and physical SRAM persistence also need testing. Endings/respawn fixtures often inject positions; passing all regional ending checks does not mean someone completed every route normally. PAL functionality remains covered, but PAL performance is currently a lower priority.

The [issue list](ISSUES.md) is the current status source. The [documentation index](docs/README.md) links the technical notes, which preserve some of the development history, including mistakes and superseded claims. The full internal working log is not published.

Pac-Land and its original code, graphics and music belong to their respective rights holders. Credit to Namco for the original game, SGDK for the development kit, and Codex/Claude for the substantial AI assistance described above. The project's [MIT licence](LICENSE) covers original project code and documentation only; third-party components retain their notices. It does not license arcade content or provide legal clearance. Generated cartridges, decoded assets, reference captures, private fonts and music stay outside public distribution.
