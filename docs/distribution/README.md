# Pac-Land MD ROM maker: downloads

The ROM maker builds the Mega Drive conversion on your own computer from your own Pac-Land arcade ROM set. It contains no game content.

## Windows

Download `Pac-Land MD ROM Maker.exe` from the Releases page and run it. Drag your `pacland.zip` onto its window and press **Build my ROM**. On first use it downloads its own pinned, checksum-verified build tools (Python, Node.js, the Zig compiler and libraries, about 500 MB) into `AppData\Local\PacLandMD`. It ignores any other copies of those tools on your PC, needs no administrator rights and saves the cartridge to `Documents\Pac-Land MD`.

The app is not code-signed, so Windows SmartScreen and some antivirus products may warn about a new file. On the SmartScreen screen choose *More info*, then *Run anyway*.

## Linux

Use this repository: download or clone it, run `sh setup.sh` once, then `sh make-rom.sh /path/to/pacland.zip`. Setup downloads the same pinned tools into the repository folder. The cartridge appears in `build/rom-maker-output`.

## ROM sets

Merged, split and non-merged MAME `pacland` sets all work; files are found by checksum. A merged set includes the Japanese artwork; with a split set keep `paclandj.zip` beside `pacland.zip`. With the Japanese artwork available, the result matches the reference cartridge (SHA256 `68404d45d2db59210f9c46141d74c25df461c307f4f09600a213a28af57e6c80`, 4,063,232 bytes).

## What is not included

No arcade ROM, game graphics or music, translated arcade program or ready-made cartridge. The build tools are downloaded by you from their official sources and keep their own licences. The project's MIT licence covers only its own code and documentation and grants no rights to the original game.
