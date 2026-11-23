# Bring your own ROM maker

The ROM maker rebuilds the current adaptive-presentation Mega Drive development cartridge from your own supported arcade ROMs. It includes no arcade ROM, extracted graphics, captured music, translated arcade program, game cartridge or saved state. It is a rebuild, not a patch containing a transformed copy of the game. All game assets and translated code are generated locally. No ROMs are uploaded and no paid services or API keys are used.

The end-user distributions are separate **Windows x64 ZIP** and **Linux x64 tar.gz** packages. Both bundle Python, Pillow, NumPy, Node, Zig/Clang/libc, the cartridge compilers and locked build libraries. Extract the whole package into a writable folder; run `Make-ROM.bat` on Windows or `sh make-rom.sh` on Linux. No setup, downloads, administrator access or separate development software is required for a normal build. Windows requires 64-bit Windows 10/11; Linux requires x86_64 with glibc 2.28 or newer. User build processes allow the local compiler server and block external Python/Node connections.

The setup instructions below apply to the optional developer **source edition**, not the bundled end-user packages. Maintainers assemble the pinned portable runtimes with `tools/rom-maker/portable.py`; original download hashes are in `portable-runtimes.json`. Bundled notices and licences remain in each package. Generated cartridges and the private build directory must not be added to a shareable archive.

The result remains an incomplete development build with the project's existing gameplay/performance limitations. The source licence covers original project work; it does not grant rights to the original game or to locally generated cartridges.

The mainstream build includes adaptive response timing and a Start-toggled Namco pause window over dimmed, frozen gameplay, with Resume and Exit to Title. Its reference SHA256 is `68404d45d2db59210f9c46141d74c25df461c307f4f09600a213a28af57e6c80` when the Japanese artwork is available (a merged set, or paclandj.zip beside the parent). A parent-only set without that artwork intentionally differs.

## Source-edition requirements and setup

Nothing needs installing beforehand. Run `Setup.bat` on Windows or `sh setup.sh` on Linux once, with an internet connection. It downloads pinned copies of Python, Node.js, the Zig C compiler, Pillow, NumPy and the lockfile-pinned `romdevtools` npm toolchain (about 500 MB in total) from their official sources: python-build-standalone on GitHub, nodejs.org, ziglang.org, PyPI and npm. Every file is checked against the SHA256 in `tools/rom-maker/portable-runtimes.json` or `package-lock.json`. Everything goes into the maker folder itself (`runtime/` and `node_modules/`); nothing is installed system-wide and no administrator rights are needed. After that, building needs no internet access. If setup is interrupted, run it again; finished downloads are reused. The download cache is deleted when setup completes.

The npm package contains the Mega Drive and Z80 compiler tooling; a separate SGDK installation is unnecessary. Keep `LICENSE`, `LICENSE-SGDK`, and third-party package notices. Developers who prefer their own tools can still run `tools/rom-maker/maker.py` directly with Python 3.12+, Node.js 24+ and GCC or Zig on PATH (`--node`, `--gcc`).

The optional `--reprofile` diagnostic mode additionally needs MAME, Xvfb and Python 3 on Linux (or inside WSL on Windows). For Ubuntu/Debian install `python3 mame xvfb xauth`; `--mame` selects its executable, default `/usr/games/mame`. Ordinary builds use canonical source-owned access hints and need no saved states or reference emulator.

## Building your own cartridge

On Windows drag your `pacland.zip` (or the folder containing it) onto `Make-ROM.bat`, or double-click the BAT and enter/drop the ZIP path into its prompt. On Linux run:

```sh
sh make-rom.sh /path/to/pacland.zip
```

The supported input is the MAME **pacland** set (Namco, World), described by `tools/rom_layout.json`, in any of MAME's layouts: merged, split or non-merged. The maker finds each required file by size and checksum, so ZIP names, member names and folders inside the ZIP do not matter. It searches the ZIP you give it plus any other `pacland*.zip` in the same folder, or every ZIP in a folder if you give it a folder. A merged `pacland.zip` already contains the Japanese artwork; with a split set, keep `paclandj.zip` beside `pacland.zip`. A Japanese or Midway clone ZIP on its own is not enough, because those revisions use different program ROMs. The maker reports what it found, which ZIP each part came from and, if anything is missing, the exact files and checksums it still needs. Every file is checked by CRC32 and SHA1 before use; other revisions and damaged files are rejected. 7-Zip archives are not supported. No downloading occurs. `--validate-only` runs this check without the build dependencies and writes nothing.

Optional inputs:

- `--japan /path/to/paclandj.zip` is only needed if the Japanese artwork is somewhere else. It is found automatically in a merged set or beside the parent ZIP. Without it, Japanese regional selections use the parent artwork; the menu item is still present.
- `--output /path/to/private-output` changes the destination (default `build/rom-maker-output`).
- `--node /path/to/node` and `--gcc /path/to/gcc` select installed executables. `ROM_MAKER_NODE` also selects Node.
- `--jobs 4` limits parallel palette searches (default up to eight); use `--jobs 1` to reduce CPU use.

For example, from a Windows terminal in the development tree:

```powershell
python tools/rom-maker/maker.py C:\MyROMs\pacland.zip
```

The launcher prints progress through validation, extraction, canonical access-hint loading, parallel scenery packing, local recompilation, sprite preparation, HUD/title generation, sound MCU simulation, and the final cartridge build. A cold rebuild can take many minutes, particularly palette packing and Python emulation. During a long step, its detailed log continues under the printed private working folder. Each run creates a fresh `build/rom-maker/run-…` source workspace, so development assets cannot silently leak into the result. It starts and stops its own local headless build server on an available loopback port and leaves an existing development server alone.

The output folder contains `Pac-Land-MD.bin`, its `.sha256` file, and `build-manifest.json` with required input-member hashes, source hashes, optional-input configuration, tool versions, the Mega Drive header checksum and final SHA256. Local logs and private source/generated work are retained under `build/rom-maker` for diagnosis. Treat those directories and the cartridge as private. Delete obsolete run directories yourself when you no longer need them; the maker does not remove development files.

## Reproducibility and known limits

The current private reference cartridge has SHA256 `68404d45d2db59210f9c46141d74c25df461c307f4f09600a213a28af57e6c80`. That configuration includes the Japanese graphics. Output without them is expected to differ. Use `--expect-sha256 HASH` to require a known complete output before the maker copies it into the requested output folder.

The original scalar FM synthesizer configuration is included as source model parameters (algorithm, feedback, operator multiplier/level, and gain trim). Captured waveform bytes, spectral measurement arrays and music streams are excluded. The music streams themselves are freshly captured from the supplied sound MCU ROMs. The synth parameters are authored conversion settings; the public kit does not need to repeat the historical emulator-guided calibration experiment.

`tools/rom-maker/access_hints.json` is canonical code-generation configuration: numeric access-site IDs map to bit flags identifying RAM, ROM or I/O access. It contains no arcade instruction bytes, graphics, memory snapshots, or translated routines. It preserves the existing compiler specialisation for the strictly validated parent revision. The translated C is always regenerated from the supplied ROM. `--reprofile` measures a new diagnostic profile instead; historical measurements are not necessarily reproduced by the current profiling script, so that option intentionally changes the cartridge hash.

Native compiler behaviour can affect clean-build results across environments. The maker records actual results and checksums rather than promising untested cross-platform byte identity. Unlike the legacy three-second sprite-coverage sampling, the maker instruments a separate host-only translated source with a fixed two-million-instruction budget per frame. Arcade waits that spin forever end after the last completed snapshot, independently of computer speed. A separate 120-second process timeout fails the build instead of silently accepting partial coverage. Primary palette runs must complete their full frame count. Cartridge source is not instrumented. Linux has the same native pipeline and launchers; see the local validation report for which operating systems were actually tested.

## Making the shareable kit

From the development checkout run:

```sh
python tools/rom-maker/maker.py --package build/rom-maker-kit/Pac-Land-MD-ROM-Maker.zip
```

Packaging uses an explicit source allowlist and fixed ZIP timestamps; it does not recursively copy the checkout. It excludes `reference_roms`, `res/generated`, `dist`, `build`, `node_modules`, fonts, captures, private paths in reference documentation, and `src/recomp_objects.c`. It includes only required original source/tooling, synthesizer settings, launcher/setup scripts, licence notices, this guide, and an integrity manifest. Root `Make-ROM.bat`/`make-rom.sh` and `Setup.bat`/`setup.sh` are generated into the kit. The resulting source ZIP and its checksum file are the intended distribution artifacts. Never ZIP the private working directory as a substitute.

## Historical source-kit validation from earlier on 21 November 2026

Before the return-camera correction, a source ZIP was extracted into an empty temporary folder and invoked through its actual Windows BAT launcher, with all game resources generated afresh and no reference snapshots present. With the declared private Japanese artwork and logo-font inputs, it completed successfully and matched the 4,194,304-byte reference cartridge exactly: SHA256 `373fa2e606024482a38a05fad79674e4282d1ce1574d15308b079caa529b7ed5`, Mega Drive checksum `D8E1`. The deterministic coverage harness reported ten stalled arcade waits; the generated sprite data matched the existing reference. Installed dependencies were reused for this test; a fresh-machine dependency installation was not tested.

The parent-only/plain-font branches were also recompiled using the freshly generated common parent resources. This produced a 4,063,232-byte cartridge with SHA256 `905cb16905bcf70ee1d6e61981229d5ce2f3803751bd96c97aae80f9a702db36`. The no-Japan atlas contains zero Japanese artwork keys. This cartridge passed ten emulator smoke checks, including all 32 scenery loads, and 24 USA composed-sprite pixel comparisons against the reference, with no cases drawing fewer sprites. The generator also reproduced the original Japanese-enabled atlas byte-for-byte, checking that the optional-input fix preserves the reference configuration. The plain-font generator separately completed in an empty workspace without a supplied font.

Three automated validation/packaging tests passed on Windows and Linux/WSL. Linux shell syntax and the shell launcher's strict validation of all 19 parent ROM members passed. At that historical checkpoint, full Linux cartridge compilation had not yet been exercised. The tests cover malformed/missing/duplicate/wrong-revision members, safe nested archive names, deterministic source ZIP bytes, excluded private/binary assets, source checksums, scalar-only conversion hints and the mainstream build switches.

The subsequent return-camera correction changes the current reference ROM to
SHA256 `45d216dce03675c410ca49ea7525c0bd219838dfe9287eb93e96bf6d3d368bff`.
Recompiling the corrected renderer against the previously clean-generated
maker resources matches the new mainstream build exactly. Asset generation
was unchanged; the complete cold pipeline was not repeated for this camera-only
revision. The updated parent-only/plain-font variant is SHA256
`4c2975f37049215036dbb1887d04e1cb67677fa6f82fec007e19b8be631bcb88`.
Both variants pass the dedicated camera checks; the parent-only variant also
passes the gameplay smoke suite. The source ZIP includes this correction.

## Prior F075 portable validation — 21 November 2026

The ordinary default development build, a cold source-kit Windows BAT build,
and fresh bundled Windows and Linux builds all reproduce the current mainstream
SHA256 `f075799097c1d7d848429493312a2f4b6fb94ad4873dd7f60d84ffc7e4f74962`
with the same optional Japanese-art and logo-font inputs. The Windows portable
test ran from the extracted ZIP in a folder containing spaces, with only Windows
OS directories on PATH. The Linux portable test ran under WSL with only `sh`
and `dirname` available on PATH; all language runtimes, native compilation and
cartridge compilers came from the bundle. WSL is a test host, not a package
requirement. The bundled Zig census matches GCC byte-for-byte on both platforms.

Portable runtimes are Python3.13.18, Pillow12.2.0, NumPy2.5.2, Node24.22.3 and
Zig0.14.1. Six source/package/offline tests pass on Windows and Linux, including a regression check that POSIX launchers contain no carriage returns. Python
and Node smoke tests allow loopback compiler connections and reject external
connections. Archive integrity and release details are recorded alongside the
distribution downloads. The lightweight source edition remains available
separately; it requires installed development software and initial setup.

## Prior title-DMA and waka revision — 21 November 2026

The current reference is SHA256 `80ff565612970f7e2521545e757e7f03e04e63902c85f39ff441147501dd7e90`. It combines the user's selected authored waka1 with completed VRAM-fill waits at title-frame wrap. The user confirmed the minimal DMA-wait correction stable on their real console; this verifies the reported attract-repeat fault in that test, not universal hardware qualification. Twelve attract cycles and pause checks pass for the combined build.

Both previously successful Windows/Linux cold-maker stages were recompiled with only title.c and prepare_boot.py changed, using bundled tools and freshly regenerated boot data. Both reproduce this reference exactly; common generated game resources were unchanged. Waka PCM is exactly the selected 3072 bytes. The expensive common cold-generation steps were not repeated for these two source changes. Private hardware diagnostic hooks are excluded from public build sources. All six source/package/offline tests pass on Windows and Linux.

## Prior CD85 Trip selection and Namco menus — 22 November 2026

Reference SHA256 at that checkpoint: `cd85adc371480b20e399267eb7d5e4735752ee52d6b5b95c376a746ebfa8ec9b`. On the title screen, Start opens Trip1–5 selection. The current arcade-style controls are described in the forest batch section below. Options-screen Start opens the same selector. The last selected trip is saved. Trips begin at rounds1/5/9/13/17. When trip shuffle is enabled, the selected actual trip comes first and the remaining trip order is seeded.

The boot notice, title prompts, options pages/subpages, sound test and trip selector use the Namco menu font; the existing Namco pause window remains unchanged. Menu font data is generated locally by prepare_hud.py from the supplied arcade characters. No generated game font is bundled.

Both previously successful Windows/Linux cold-maker stages were recompiled using bundled tools with the nine changed source files, fresh HUD menu-font data and fresh boot data. Both match this reference exactly; other common generated resources were preserved rather than repeating a full cold generation. Trip selection/saving/wrap/cancel, options Start, seeded-shuffle first trip, attract interruption and actual VRAM glyph checks pass. NTSC/PAL pause freeze/restore/exit checks and twelve attract cycles pass. The prior title-DMA correction was confirmed stable by the user on their console; this scoped result does not certify every console. The selected authored waka1 remains included.

## Prior03FF clean scene transitions — 22 November 2026

Reference SHA256 at that checkpoint: `03ff9fe2a0e07e19466e01a09f548b4baa59d21d5f6059e9af241c9027b00153`. Cold scene loads stay hidden until the initial scrolling, sprites, player and HUD are committed, then reveal at a physical vertical-interrupt boundary. The original trip card is retained. Title starts and wraps hide partial redraws, including when attract demos are disabled. Existing Trip1–5 selection, Namco menus, pause and selected waka1 are preserved.

Both bundled Windows/Linux maker stages recompiled only renderer.c/title.c against their verified CD85 resources and reproduce this reference exactly. No common asset generation was repeated. Actual opening fields, demos-disabled looping, Trip/Namco/save, NTSC/PAL pause, twelve attract cycles,64USA/Japan ending fixtures and both player designs' boots/bonus/pit/life-decrement/respawn checks pass. The new scene-transition change still awaits console play-testing. The earlier minimal title-DMA correction retains its scoped user hardware confirmation. A slower-filesystem compiler-server readiness allowance was also added to the builder; normal builds remain offline.

## Prior A2AE title-only attract playback — 22 November 2026

Reference SHA256 at that checkpoint: `a2ae752392a7e0182d455b1492324de7010d88d2e6d9fb856d77f3c7d7ad3169`. With attract demos disabled, playback now remains within the title/high-score sequence. The title generator stops at777frames before the captured demo scenery begins, instead of replaying a recorded level while the runtime demo flag is off. PAL0 pen15 is reserved white so the Start/B prompt remains visible. The generated resource contains124tiles and a23908-byte stream with no recorded sprites. Existing scene-reveal, Trip/Namco, pause and waka features remain unchanged.

Each bundled stage regenerated this title resource from its own staged user-ROM ZIP using the updated prepare_title.py, then recompiled. Windows/Linux both match the current reference and their title data matches the frozen root data with normalised line endings. All other generated assets were preserved; no full cold generation was repeated. The final revision passes2400consecutive attract-OFF fields with no level flash, visible title/high-score screens and Start/B prompt,12attract-ON cycles with stable pixels/CRAM/heap, plus Trip/Namco/save/random and scene-opening checks. Console play-testing of the recent scene/title changes remains pending; the earlier minimal title-DMA fix retains its scoped user confirmation.

## Prior BF79 forest visibility batch — 22 November 2026

Reference SHA256 at that checkpoint: `bf79fe637dd165a25ba79f8c96cb7eb7075d699fbd7a3371172e4e0b17c77d57`. This batch improves forest sprite visibility and palette handling, arcade Trip selection, new-game secret initialization and native demo timing. Each bundled stage freshly regenerated native/data.c and recomp_data.c from its own verified ROM data using the modified generators and bundled coverage host, retaining palette caches and other audio/title/HUD resources. Both generated resources match root data with normalised line endings; both ROMs reproduce this reference exactly. No private generated artwork was copied as a substitute.

Withdrawn evidence: the earlier synthetic 304-case placeholder checks did not validate captured real-ghost composites. They must not be used as correctness evidence. Their old synthetic cadence figures are not current release validation; use the captured-composite evidence below.

The external arcade cross-sprite cookie-cut/OR operation and round19 lighting are not fully emulated. New forest changes have no real-hardware qualification. These limitations remain open; this is not a claim of locked60 FPS or complete arcade rendering equivalence.

Current arcade Trip controls: Left/Right and A/C navigate; the configured Jump button (B by default) or Start confirms. Up returns. In any-button-jump mode, any A/B/C button confirms instead of navigating. The last selected trip is saved; shuffled trips begin with the selected actual trip, then the seeded remaining order.

## Rejected A834 white foreground experiment — 22 November 2026

Reference SHA256 at that checkpoint: `a834b5212200d211ed2268cee9c7edccbeca94f48b9095d0075b0346cdb42962`. White contours and source-eye pixels are applied only through opaque priority foreground foliage. Clear-area ghost pixels remain unchanged; background scenery does not trigger this effect. The ordinary low-priority body remains above background trees. The atlas is freshly regenerated from user-provided data; the native/recomp resources remain each stage's own verified BF79 generation, guarded by generator/runtime and resource hashes. Both bundled Windows/Linux recompiles match this reference.

Actual captured-composite validation passes 128 form/flip/art cases: retained original-engine states A0/A4/AC/B4/C0/C8/80 plus the D8 frightened state supplied by independent 6809 initializer 91B9. Body and both active child components are replayed. Another 54 captured live-Sue position steps verify eye/body alignment over real tree edges using displayed SAT positions. This is controlled debug-position playback, not uninterrupted gameplay. All changed pixels are white and foreground-covered; pixels outside opaque priority foreground remain unchanged. NTSC/PAL pause and twelve attract cycles pass with stable pixels/palette and heap. These replace the invalid synthetic 304-case claims.

Early natural-demo samples for routes R1–4 measure 59.85/59.81/59.54/59.47 FPS versus BF79 59.85/59.61/59.24/59.33. These are not crowded-forest benchmarks or proof of all-game locked 60 FPS. The RAM-neutral budget permits 16 new 128-byte masks per frame, with 32 cache cells and a 78-object-sprite cap reserving two player slots; hints were reduced from 1024 to 512 entries. Heavy scenes can defer overlays. Real-hardware validation remains pending, and the remaining external cross-sprite OR/round19-lighting limitations are not claimed solved.

## Prior60B total forest-transparency rollback and large queen fix — 22 November 2026

Reference SHA256 at that checkpoint: `60b369fe12c08418645fe188f38754bcaceef5c8e6cb33f39a8a14d0b490360b` (3,932,160 bytes). A834 transparency was rejected for forest performance. Overlay runtime, tagging, CPU clipping, DMA buffers, SAT insertion, atlas mask data and the 128-tile reservation are removed entirely; 1024-entry hints are restored. No active forest transparency or white-ghost feature is claimed. Earlier captured A834 visual tests do not establish acceptable gameplay performance.

In a matched controlled moving four-composite replay through actual round2 trees, A834 presented 13 of 120 fields (6.5 FPS), while this rollback presented 120 of 120 (60 FPS). Simulation was frozen to isolate rendering cost; this is not a full normal-gameplay benchmark or an all-game 60 FPS claim. NTSC/PAL pause and twelve attract cycles pass; title pixels/CRAM and heap remain stable.

The large queen/princess uses group32h cells1D4/1DC and a separate scene mapping preserving pink at spare PAL3 pen14. Both complete queen halves and both art designs pass actual VRAM/palette checks, including 570 displayed dress pixels per design. Existing player/little-fairy pens0–13 and15 are preserved across all32 layouts; the small-fairy bitmap is unchanged. Other queen shades use nearest mappings: full PROM-exact colour is not claimed.

Both bundled stages copied only objects.c, fast_draw.s, prepare_blast_atlas.py and prepare_fairy.py, reusing their own hash-verified BF79 native/recomp resources and freshly generating atlas/fairy data. Both match this reference. New rollback/queen console play-testing remains pending. Bundled-tool corresponding-source/licensing review also remains pending; packaging/content verification is not legal clearance. No publication is performed.

## Prior 9266 column DMA, spring launch and queen animation fix — 22 November 2026

Reference SHA256 at that checkpoint: `9266cd9869e0627c6dc06544b96eb39ced8ae00524dd66bc47bb0822ef0c3bac` (3,932,160 bytes). Strided column DMA explicitly splits at 128 KB ROM boundaries and advances the VRAM destination by words ×128, preserving column stride instead of the SDK fallback's contiguous advance. Forward/reverse Round5 VRAM map/pattern checks are clean. Controlled R13/R14/R15 scrolling checks are also clean, but the user's separate Trip4 castle/floor-block report still awaits an exact location; no specific resolution is claimed. Maps are unchanged.

Springboard state6 launch with boots off receives one extra upward +0x0080 (half a pixel/tick), a small intentional departure from arcade launch height. Ordinary jumps, maps and airborne tapping semantics are unchanged. In the reproduced final-pool trajectory, peak height rises from156 to177 and timed RIGHT taps cross the bank; simply holding RIGHT still fails. Scope checks cover440 compiled hook cases plus the actual cartridge trajectory.

Large queen groups32h and33h now both use custom cells/scene PAL3 mappings, fixing animation colour flicker. Both art designs and both phases pass actual VRAM/palette checks; sampled face colour stays stable and pink dress pixels are retained. Existing small-fairy art and palette rows remain unchanged. Other queen shades are nearest-colour mappings, not full PROM-exact output.

Both bundled stages recompiled the six frozen sources, reusing their own guarded BF79 native/recomp data and freshly generating atlas/fairy resources, and match this reference. NTSC/PAL pause and twelve attract cycles pass. The total forest-transparency rollback remains in force. New console validation and bundled-tool corresponding-source/licensing review remain pending; packaging verification is not legal clearance.

## Current: ghost outlines through front trees and flexible ROM sets — 23 November 2026

Reference SHA256: `68404d45d2db59210f9c46141d74c25df461c307f4f09600a213a28af57e6c80` (4,063,232 bytes), with the parent set plus the Japanese artwork. Ghosts behind front-layer trees keep their outer edge, dark line art and eyes in their own colours. A new build step, `tools/prepare_overlay_masks.py`, prepares the outline masks from your ROM set; they stay private in `build/` like the other generated data.

The maker now finds every required file by size and checksum in merged, split or non-merged sets, searches other `pacland*.zip` files beside the one given (or a whole folder), and reports exactly what it found and what is missing.

Validation: the source ZIP was extracted into an empty folder on Linux and every maker step was run from scratch with the same commands and environment as `maker.py` (palette search, translation, coverage, HUD, title, sound capture, cartridge compile). The result matched the reference SHA256 byte for byte. The maker's input search was tested with split, merged, parent-only, clone-only, renamed and damaged sets, and its unit tests pass. A full Windows run of `Make-ROM.bat` from the new ZIP has not yet been repeated.
