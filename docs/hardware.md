# Native cartridge design and measured limits

Native 68000 C links against the SGDK runtime supplied by romdev. Standard
cartridge ROM is 3.875 MiB, within the ordinary 4 MiB address window. There is
no 6809/MCU emulator in the cartridge, no extra processor and no special mapper.
The generated header declares actual ROM bounds, JUE regions and a joypad;
it declares battery-backed SRAM (odd bytes, 200001h to 2001FFh) for settings and high scores. Reset vectors and checksum are checked in the tests.

H40 is explicitly selected at 320×224. Both planes are 64×32. Foreground is
VDP plane A; background is B. Per-tile horizontal scroll fixes the three HUD
rows and bottom status row. Entering columns populate a circular buffer in
each direction. Full map painting occurs at layout load with display disabled.
Source map rows remain 8 pixels high; scenery is never stretched to 320.

Background patterns start at VRAM tile 128, leaving low SGDK system/font space
available under either font-placement configuration. Animated player patterns
stream to tile 1400 and occupy 16 or 20 tiles. Corrected 36×32 graphics use a
32×32 plus 8×32 hardware metasprite. The loaded SGDK font is at the top of the
pattern region under its legacy-font configuration, so player patterns stop
before tile 1440. Name tables/SAT/scroll table use SGDK's 64×32 VRAM layout.

Three palette lines cover scenery; the fourth covers player/UI. Palettes are
per round. The converter retains original colours exactly where its 15-colour
bank packing permits and otherwise chooses weighted nearest CRAM colours.
Tiles are deduplicated by packed pattern data. Converted records preserve
flip and ordinary foreground-priority bits. Special source per-pixel masks
and lantern effects are pending.

The player update is independent of rendering. An integer phase scheduler
targets source 2000/33 Hz from nominal NTSC or PAL refresh, occasionally
executing two simulation ticks per displayed frame. Both 3-button control
modes are implemented. PAL cadence is implemented but was not emulator-tested.

In the executed 120-frame movement sample, DMA averaged **507 bytes/frame**
and peaked at **768 bytes/frame**. This includes player pattern changes, SAT
and scroll uploads. CPU data-port column writes are not included in that
counter. The build contains at most two player hardware sprites per line;
that does not establish performance once enemies and large vehicles exist.
No real-hardware timing or crowded-scene performance claim is made.

The linked RAM sections end at mirrored address E0FF0A9C (2716 bytes from
RAM base), before runtime heap and stack usage. This is not a measured
high-water mark. Remaining runtime memory, worst-case 68000 frame execution,
audio DMA contention under a complete enemy load and physical flash-cart
compatibility remain unmeasured.
