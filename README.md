# Pac-Land MD

**Pac-Land on the Sega Mega Drive / Genesis, running the arcade game's own logic at full speed.** Version 1.0.

I wanted to play Pac-Land on the Mega Drive. This seemed like a fairly reasonable idea until Pac-Man reached the logs and apparently decided he was being paid by the hour. Many optimisations later, he isn't.

This is a native cartridge. The arcade's own 6809 routines for the player, enemies and objects have been statically recompiled into 68000 code and run alongside native Mega Drive systems for movement, scenery, drawing and sound. It isn't an emulator running inside the console, and it isn't a lookalike remake: Pac-Man, the ghosts and the scenery behave the way they do in the arcade because it's the arcade's code making them do it.

**AI disclosure, before anyone gets to the bottom and feels ambushed:** Codex and Claude have provided substantial code, tooling, analysis and debugging assistance. I have directed the work, played the builds, supplied feedback and reference material, made the visual and gameplay decisions, and tested on my own console. I am not presenting this as a lovingly hand-written assembly port that I personally crafted over twenty years. Sorry if the AI bit puts you off; I would rather be clear about it.

## How it plays

- **Full arcade speed.** The game always runs at the arcade's rate, so timing, jumps and chases feel right.
- **Smooth.** Most of the game runs at a solid 60 frames a second. Even the busiest scenes, like the return trip's logs with the power boots, stay in the mid-to-high 50s, where they used to crawl at 25.
- **Faithful.** Original player and object behaviour, scoring, lives, trip endings, princess sequences and flying boots, all 32 layouts.
- **Tested on real hardware**, on a Japanese Mega Drive with an EverDrive Pro, as well as in emulators.

## What's in the cartridge

- A 3.875 MiB (4,063,232-byte), 320 × 224 cartridge covering all 32 of the original layouts.
- USA or Japan character artwork (Japan needs the Japanese ROM files), with original or wide pixel proportions.
- Arcade-style Trip 1–5 selection, 1–9 starting lives and several controller presets.
- A randomiser: seeded whole-trip shuffle, ghost identity remix, and fruit and hidden-item variation.
- Title and attract mode, Namco-style menu lettering, pause with Exit to Title, and a sound test.
- Settings and high scores saved to SRAM.
- Music and effects on a Z80 sound driver.
- Ghosts behind the forest's front trees keep their outline, line art and eyes, as in the arcade, at no cost to speed.
- A modest extra springboard launch height for the pool jumps; the maps and normal jumps are unchanged.
- A PEZZ82 fan-project boot sequence in which Pac-Man eats the logo. Obviously this was an essential engineering requirement.

The randomiser keeps trip groups intact, and when trip shuffle is on, the trip you choose comes first.

## Build your own cartridge

There is no ready-made game ROM here. You supply your own Pac-Land arcade ROM set, and the maker builds the cartridge on your computer. Please use ROM data you are entitled to use; the maker does not establish permission for you.

Download the ROM maker for your system from the [Releases page](../../releases/latest):

| System | Download | How to run it |
| --- | --- | --- |
| Windows 10/11 (64-bit) | `PacLand-ROM-Maker-Windows-x64.exe` | Double-click it |
| Linux (x86-64) | `PacLand-ROM-Maker-Linux-x64.AppImage` | Mark it executable, then run it |
| Mac (Apple Silicon) | `PacLand-ROM-Maker-macOS-Apple-Silicon.dmg` | Open the DMG and run the app inside |

Then drop your `pacland.zip` (or the folder it is in) onto the window, choose where to save the cartridge and press **Build my ROM**. All the build tools are included, so nothing else needs installing and nothing is downloaded. A build takes several minutes.

The apps are not code-signed, so your system or antivirus may warn about them. On Windows, on the blue "Windows protected your PC" screen choose *More info*, then *Run anyway*.

The desktop app was contributed by [Anime0t4ku](https://github.com/Anime0t4ku); see [the desktop app notes](docs/desktop-rom-maker.md) for details, including building it yourself. Developers can also run the maker straight from this repository: `sh setup.sh` once, then `sh make-rom.sh /path/to/pacland.zip`.

### Your ROM set

The maker accepts the MAME `pacland` set in merged, split or non-merged form and finds each file by checksum, so ZIP and file names do not matter. A merged `pacland.zip` already includes the Japanese character artwork; with a split set, keep `paclandj.zip` beside `pacland.zip`. If anything is missing, the maker tells you exactly which files it still needs. ROMs are checked locally and never uploaded.

The [ROM-maker guide](docs/rom-maker.md) covers advanced options. A full development checkout has a separate reference-capture and rebuild workflow, described in the [development notes](docs/development-notes.md).

## Controls

By default, the D-pad or A/C moves and B jumps. Options also provide D-pad + B, arcade-style A/C + B, or D-pad with any of A/B/C to jump.

| Screen | Controls |
| --- | --- |
| Title | Start opens Trip selection; B opens options |
| Trip selection | Left/Right or A/C selects; Jump or Start confirms; Up returns |
| Options | Up/Down selects; Left/Right or A/C changes values; B returns |
| Gameplay | Start opens pause |
| Pause | Start/B resumes; Up/Down selects; A/C confirms |
| Sound test | Left/Right selects; A plays; C stops; B returns |

In any-button-jump mode, A/B/C confirms Trip selection instead of navigating. Start skips the boot notice. Settings and high scores save automatically; your emulator or cartridge must support SRAM saving. Hard-reset after changing builds, because older save states can contain incompatible RAM layouts.

## How this became a project

### First, make it behave like Pac-Land

The early version had native movement, terrain and rendering, but recreating all the interactions from appearances was not enough. Ghosts, moving scenery, logs, platforms and the player are tied together in the original game. A plausible-looking approximation could still let Pac-Man fall through something he should stand on. Slight problem for a platform game.

So the approach changed: the original 6809 object routines were statically recompiled into C for the Mega Drive's 68000, and the original player routine followed once its object-contact behaviour proved necessary. That brought back the arcade's real behaviour, while native systems handled the console side.

Every step was checked against an independent interpreter of the original CPU and, where applicable, MAME. Those comparisons recovered activation rules, fixed-point movement, scoring and scene sequences, rather than making up replacement enemy behaviour.

### Then, make it fly

Bringing the original logic across made busy scenes expensive. The first optimisations cut register and memory overhead, cached graphics, packed sprites and stopped rebuilding scrolling data that hadn't changed.

Then came the return-trip logs with the power boots. Sonic can race around this machine, so why can't Pac-Man manage a few logs? Thus: **Project Blast Processing**. Another blast processing moment, lol.

It grew into shared native systems for ordinary player movement, terrain, bridges, logs, frightened cars and items, across all 32 layouts. Ordered activation plans keep the original interaction order and offscreen effects. Planned art residency, compact draw commands, a body-frame cache and a smaller draw collector cut the rendering work. The arcade's own handlers still run the special states and cold sequences.

The results:
- Round 2 went from 30 to 60 FPS.
- The heavy round-4 log section went from about 25 to 56 FPS.
- Rounds 1, 2 and 27 run at 59–60 FPS, and rounds 25, 3 and 11 at 54–58.

A separate clock correction means a busy frame never slows the game itself down: it always runs at the arcade's speed. Adaptive presentation overlaps work in the busiest scenes and returns to the quickest-response path as soon as there's headroom. [Blast Processing](docs/blast-processing.md) and [adaptive presentation](docs/adaptive-presentation.md) have the measurements and the experiments that didn't make the cut.

### The bits that screenshots hide

Visual work has involved palette compromises, red caps, white boot outlines, doors, scenery edges, title transitions and menu redraws. My job has often been to play it and say, "that still looks wrong," which has turned out to be quite useful. One report about the first "poop" was supposed to say "pool." The bug tracker survived.

Audio also took several attempts. An early supposedly perfect loop was actually silence after a capture mistake. That was discarded. The Z80 driver now handles all the music and effects, with fixes for disappearing instruments and the missing game-over stab.

The boot sound had its own audition: I chose the synthesised `waka1` candidate. It is original synthesis rather than a copied arcade recording or wavetable. The fan notice makes the unofficial status explicit before Pac-Man gets on with eating the branding.

Real hardware supplied a useful reality check. Repeated attract loops developed progressive graphics and colour corruption that emulators never showed. It came down to two title-screen VRAM clears that needed to wait for completion. Fixed, and confirmed on an EverDrive Pro and a Japanese Mega Drive in USA mode.

### And then the forest humbled us

In the arcade, ghosts behind the forest's front trees still show their white outline and eyes. The first attempt at this was reported fixed too early. Some synthetic tests used eyes-only frames, and a later "full ghost" test turned out to be purple Pac-Man. Yes, really. Hundreds of passing combinations cannot help when you have tested the wrong character.

Capturing real ghost objects eventually produced the right outlines and eyes. Then moving ghosts behind the trees brought the game to its knees: about 6.5 FPS, against 60 without the feature. I would rather have shipped hidden ghosts than a slideshow with beautifully outlined ones, so that version was removed completely.

Then a cheaper idea arrived. The Mega Drive already hides low-priority sprites behind high-priority scenery for free, so there is no need to work anything out pixel by pixel. When a ghost overlaps a front tree, a second sprite is drawn in front of the trees holding only the ghost's outer edge, its dark line art and eyes, plus the pixels the arcade itself draws over its front layer, all in the ghost's own colours. The ordinary body stays behind and the trees hide it. In the open the two copies are identical, so there is no seam. The outline masks are prepared when the cartridge is built, so the console only copies them, and the forest rounds run exactly as fast as they did without the feature.

The large Fairy Queen also briefly acquired a black dress when the room swapped sprite palettes. Her pink dress now has a reserved colour, while Pac-Man and the little fairies keep their existing pens. Both alternating queen palettes are handled, so her skin stays the same colour.

## Known issues

- **The very busiest moments** can occasionally skip a frame. The game itself never slows down. PAL consoles get less attention than NTSC.
- **Colours:** a few rounds use more scenery colours than the Mega Drive can show at once, so some shades are the nearest match. Cross-sprite masking and round 19's lighting effects aren't reproduced.
- **Randomiser:** it doesn't shuffle every enemy or item type.

The [issue list](ISSUES.md) has the details, and the [documentation index](docs/README.md) links the technical notes, including the development history and the ideas that didn't work out.

## Credits and licence

Pac-Land and its original code, graphics and music belong to their respective rights holders. Credit to Namco for the original game, SGDK for the development kit, and Codex/Claude for the substantial AI assistance described above. The project's [MIT licence](LICENSE) covers original project code and documentation only; third-party components retain their notices. It does not license arcade content or provide legal clearance. Generated cartridges, decoded assets, reference captures, private fonts and music stay outside public distribution.
