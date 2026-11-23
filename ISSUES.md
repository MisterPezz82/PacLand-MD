# Known issues and current status

Updated 23 November 2026 (build 68404d45). This is the public summary. A fix described as
"checked in an emulator" has passed automated emulator tests; it has not
necessarily been confirmed on a real console.

## Open

| Area | Status |
| --- | --- |
| Frame rate | Busy scenes are much smoother than earlier builds and game speed is held at the arcade rate, but some frames are still missed. The heavy round 4 log section measured about 56 FPS. The whole game is not locked at 60 FPS. PAL performance is a lower priority than NTSC. |
| Ghosts behind trees | Implemented again with a cheaper method: ghosts keep their outer edge, line art and eyes in their own colours when behind front trees. The first attempt ran at about 6.5 FPS and was removed; this one uses outline masks prepared at build time and kept the forest rounds' frame counts in emulator tests. Awaiting real-console confirmation; the heaviest late rounds may drop the occasional extra frame. |
| Trip 4 floor | A report of block-shaped floor corruption near the break after the castle on Trip 4 has not been reproduced. A related scrolling-column bug was fixed; this specific report remains open. |
| Colours | Some rounds use more scenery colours than the Mega Drive palettes can hold at once, so a few shades are nearest-colour approximations. The large Fairy Queen's secondary shades are also approximated. Cross-sprite masking and round 19 lighting effects are not reproduced. |
| Audio | The Z80 driver plays all music and effects. A detailed listening comparison against the arcade is unfinished. |
| Testing | Ending and respawn checks inject positions. Complete uninterrupted trips, wider randomiser coverage and SRAM saving on physical cartridges still need play-testing. |
| Randomiser | Does not shuffle every enemy or item type. |

## Fixed in the current build, awaiting real-console confirmation

- Round 5 house column glitch before the church (scrolling data crossing a ROM bank boundary). The fix applies to all rounds.
- Round 6 final pool: slightly higher springboard launch. Maps and normal jumps are unchanged.
- Large Fairy Queen: pink dress, and no skin-colour flicker between her two animation palettes.
- Edible ghosts keep their original purple in every round; BREAK TIME sign and round 4 hydrant colours corrected.
- Clean screen transitions at new game, level change and attract entry.
- Attract mode switched off no longer flashes part of a level.
- Trip 1 to 5 selection, Namco-font menus, pause menu with Exit to Title.

## Confirmed by the developer on real hardware

- Progressive colour and graphics corruption after repeated attract loops (EverDrive Pro, Japanese Mega Drive in USA mode).
