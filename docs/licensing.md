# Licensing

This page describes what applies to which parts of the project. It is not
legal advice, and nothing here grants rights to Namco's game.

## This repository

The project's original code, tools and documentation are under the
[MIT licence](../LICENSE). The SGDK headers under `include/` keep their own
licence ([LICENSE-SGDK](../LICENSE-SGDK), [COPYING-SGDK-RUNTIME](../COPYING-SGDK-RUNTIME)).

The repository contains no arcade ROM data, decoded graphics, music, palette
or level tables, translated arcade program code, generated cartridge, or
private font. Those are produced locally by the ROM maker from files you
supply, and they are not covered by the MIT licence. Supplying your own ROM
set does not by itself give you permission to use it; please use ROM data you
are entitled to use.

`tools/rom_layout.json` lists the expected file names, sizes and checksums of
the supported MAME set so the maker can validate your input. It contains no
ROM content. `tools/rom-maker/access_hints.json` records how many bytes the
original program reads or writes at particular addresses; it is analysis data,
not program code.

## Build tools

Neither the Windows app nor this repository contains third-party build tools. On first use, the app (or `setup.sh` / `Setup.bat`) downloads pinned versions of Python (python-build-standalone), Node.js, the Zig compiler, Pillow, NumPy and the `romdevtools` npm package with its locked dependencies, directly from their official sources onto the user's own computer. Each download is checked against the SHA256 recorded in `tools/rom-maker/portable-runtimes.json` or `package-lock.json`. Those tools remain under their own licences: they include GPL compilers and emulator cores, two of which (Genesis Plus GX and Snes9x) are under non-commercial licences. The maker uses the Mega Drive compiler toolchain from that set; compiled output is not encumbered by the compilers' licences.

The Windows app itself (`tools/rom-maker/app/Maker.cs`) is original project code under the MIT licence. It embeds the project's own maker source files and nothing else.
