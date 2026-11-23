# Evidence and reconstruction record

The user designated `M:/mame`; its `pacland.zip` was read locally. All 19 files
match the parent-set definitions in current
[MAME pacland.cpp](https://github.com/mamedev/mame/blob/master/src/mame/namco/pacland.cpp).
Size, CRC32 and SHA1 expectations are centralized in `tools/rom_layout.json`.
MAME 0.264 independently reported “1 romsets found, 1 were OK.” No missing
ROM-data blocker remains.

The current [romdev README](https://github.com/monteslu/romdev), its Genesis
[mental model](https://github.com/monteslu/romdev/blob/main/packages/romdevtools/src/platforms/genesis/MENTAL_MODEL.md)
and [troubleshooting guide](https://github.com/monteslu/romdev/blob/main/packages/romdevtools/src/platforms/genesis/TROUBLESHOOTING.md)
were read. Platformer, tile-engine, two-plane and XGM2 examples informed SDK
use. Their generic game content is not used in the cartridge.

## Hardware sources

[Jotego's Pac-Land core](https://github.com/jotego/jtcores/tree/master/cores/paclan)
was studied, including `jtpaclan_game`, `main`, `video`, `scroll`, `obj`,
`objscan`, `colmix`, `sound`, `jtcus34` and `jtcus36`. The HDL is a hardware
specification, not level/gameplay source. Its GPL code was not incorporated.
MAME's Namco sound-device implementation was also inspected. HDL and MAME
agree on the two scroll generators and palette/masking architecture; their
different presentation offsets must be interpreted in their raster-coordinate
systems, not compared as raw register numbers.

MAME uses a 6.144 MHz pixel clock, 384 clocks/line, 264 lines/frame, visible
X=24..311 and Y=16..239. Refresh is exactly 2000/33 Hz under that clock model.
There are two 64×32 maps of 8×8 2bpp tiles; each cell is `[code, attribute]`.
The low attribute bit extends the code; upper bits control flipping. Foreground
attribute bit 5 supplies priority. Both scroll registers are 9 bits; address
bit 0 supplies the high data bit. Foreground status rows use fixed scrolling.

The palette is four banks of 256 indirect colours. Two PROMs encode RGB;
three further 1024-byte PROMs map tile/sprite pens to indirect colours.
The resistor weights used by MAME are 14,31,67,143, reproduced by the tools.

Sprites are 16×16 4bpp, optionally combined in 2×2 layouts. Three RAM arrays
starting at 2780, 2F80, 3780 supply code/colour, coordinates, size and flips.
Sprite LUT colours 7F and FF are transparent at the output. The OR of sprite
pixels below 80 can override foreground priority, while F0..FE have highest
priority. Colour 7F can erase foreground pixels. This light/mask behaviour in
round 19 is **not yet ported**; ordinary VDP priorities cannot reproduce it
by themselves.

## ROM-derived structure, not an assumed stage count

Zero-based round index is at main RAM **2101**. Code at 8055..8064,
8122..812C and D01C..D029 selects the first 32 definitions directly. From
index 20 hex onward it selects `(index & 0x0f) + 0x10`. Difficulty selection at
C354..C364 separately clamps the trip index to an extra difficulty entry.
This establishes 32 unique definitions and reuse of layouts 17..32 on later
loops; it does not establish all later-loop behaviour by itself.

`extract_levels.py` implements the following recovered formats:

| Item | Parent maincpu region offset / CPU code |
|---|---|
| 32 segment-list pointers | 1DD00; bank 6, CPU 5D00 |
| Segment descriptors | Byte IDs until FF, then start/end segment indices |
| Logical segment length | 288 world pixels, D0AF..D0EF |
| Segment class lookup | DCEA |
| Foreground packed-pointer table | DD0D |
| Background packed-pointer table | DD55 |
| Map column decoding | D2B3..D4A7 |
| Collision-layer counts | D4C5 |
| Collision profile pointer table | D4E8 |
| Object-bank base | 1E000 / bank 7 |
| Per-round object pointer table | Header word at 1E004 |
| Per-round interaction pointer table | Header word at 1E008 |
| Static object list | Header word at 1E002 |
| Object decoding/dispatch | 8010..809D, 80DA..8167; dispatch 8168 / 81CC |
| Interaction initialization | 8559..85AA |

Packed map pointers hold bank information in the high three bits; region
offset is 10000+pointer. A four-column descriptor has six 16-bit pointers.
Each points to four rows for that column; `(column & 3)*8` selects its variant.
Six groups produce 24 rows starting at source tile row 5. Foreground/background
segment widths depend on class: 288/256, 352/288 or 288/288 pixels. The original
world remains based on 288-pixel segments. Code D2D4..D2E9 substitutes graphic
segment 35 for selected foreground segments in round 27; the converter
implements this without changing collision segment IDs.

All 32 lists, 917 four-byte object records and interaction lists are decoded
with source offsets. **Object type and parameter semantics are not fully
identified**, so those records are not presented as implemented enemies or
known secret-item positions. Original terrain profiles have one to four
36-column layers. Reading these profiles is distinct from reproducing all
of the original collision state machine.

## Movement reconstruction

Player object base is 2400. Y integer/fraction starts at +09, X at +0C,
vertical 8.8 velocity at +0F and horizontal velocity at +11. Initial constants
come from 86C2..86E3: 02C0,0268,0134,0108,0134,0016,0005. Routines 892E..8AFA
distinguish fresh direction presses from held directions. A held direction
walks at 0134/256 pixels per update; repeated presses can reach 02C0/256.
Airborne acceleration differs. Baseline jump velocity and gravity follow
8C93..8CEE. Native translations of these isolated calculations have host tests.
The complete player state machine at 8731 onward is **not** translated.

Camera mapping follows D191..D23E. It uses the logical segment plus its
per-plane accumulated pixel offsets. Original visible camera starts at
F(playerX)-112; the native 320-pixel viewport starts at F(playerX)-128, adding
16 source pixels at each side before clamping. Sprite collision geometry
stays in original logical coordinates. Enemy activation remains unimplemented;
future rendering must not make viewport visibility become its trigger.

## Reference execution and limits

`capture_reference.lua` starts the original game and overrides the round
index during initialization. These are diagnostic round-entry fixtures,
not proof that the preceding rounds were completed or that every inventory
flag is legitimate. It records 720 frames, a screenshot, RAM and scroll/palette
metadata for each round. Tap handles are retained so Lua GC cannot remove them.

Every foreground ring column matches a decoded source column in those captures.
Every visible background/foreground column matches too. A few offscreen
background columns contain stale initialization data; the validator reports
them separately rather than hiding the mismatch. This validates the captured
starting views, not every later event or every cell encountered in a playthrough.

The CUS30 has eight wavetable channels controlled by MCU code and shared RAM.
There is no separate ready-made “music ROM.” MCU programs are exported for
analysis; melody/event translation is pending. The current DAC music comes
from a locally recorded original MAME execution, not arbitrary bytes labelled
as music and not an unrelated replacement track.
