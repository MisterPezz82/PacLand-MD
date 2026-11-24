# Portable desktop ROM Maker

The desktop application uses the existing `maker.py` pipeline. It does not change game code or the cartridge's features. The original C# Windows launcher is retained for developers who need the old source-only, download-on-first-use approach; the new release workflow builds the shared Python/PySide6 launcher instead.

## Downloads

| Platform | Package | Installation |
| --- | --- | --- |
| Windows x64 | `PacLand-ROM-Maker-Windows-x64.exe` | Run directly |
| Linux x64 | `PacLand-ROM-Maker-Linux-x64.AppImage` | Mark executable, then run |
| Apple Silicon | `PacLand-ROM-Maker-macOS-Apple-Silicon.dmg` | Mount and open the app inside |

The Linux launcher is built on Ubuntu 22.04, targeting glibc 2.35 or newer. Desktop/graphics libraries and normal AppImage execution support are still required. If FUSE is unavailable, `APPIMAGE_EXTRACT_AND_RUN=1 ./PacLand-ROM-Maker-Linux-x64.AppImage` uses the AppImage runtime's extraction mode. Apple Silicon packages target macOS 14 or newer. Windows packages target Windows 10/11 x64.

All packages include checksum-pinned build runtimes and locked npm dependencies. They are substantially larger than the original Windows launcher because normal operation never downloads tools. The app uses the OS temporary directory, keeps tool caches inside that workspace and removes the workspace when the job finishes. A forced process termination or power loss may leave a `pacland-md-*` temporary directory, which can be deleted when the app is closed.

Choose the ROM ZIPs or folder, select an output folder and press **Build my ROM**. Adjacent split sets, merged sets and Japanese artwork discovery are handled by the original backend. The app supports multiple ZIP selections and drag-and-drop. The worker-count control limits concurrent palette searches; fewer workers reduce memory use. Progress reports stages rather than a time estimate. Cancellation stops the process tree before cleanup.

Only `Pac-Land-MD.bin`, `Pac-Land-MD.bin.sha256` and `build-manifest.json` are exported after a successful build. Existing output prompts for replacement. On failure or cancellation, existing output is retained. Use **Save build log** before closing the app if diagnostics are needed. Input ROMs are never modified or uploaded.

## GitHub Actions

`.github/workflows/build.yaml` builds on pushes to `main`, pull requests targeting `main` and manual runs. It uses native Windows x64, Ubuntu x64 and macOS ARM64 runners. The three jobs build and test independently. Native architecture checks prevent an Intel macOS runner from silently producing the wrong package.

The workflow verifies the backend's existing ROM-independent tests and desktop process/environment tests. Each packaged app then checks its bundled Python libraries, compiles and executes a native C helper, runs the backend tests, assembles the original Z80 sound player and compiles a small 68000 routine using the WASM toolchain. macOS also runs the self-test directly from a mounted read-only DMG. These checks need no arcade ROMs and do not establish full cartridge correctness; validate a complete cartridge locally with your supported ROM set before announcing a release.

Successful trusted builds on `main` update the rolling `Pre-release` tag and release with the three direct downloads plus `SHA256SUMS.txt`. Pull requests upload test artifacts without publishing releases. GitHub may wrap Actions artifact downloads in ZIP containers; the actual release assets are the direct EXE, AppImage and DMG files.

The workflow grants write access only to the release job. Enable GitHub Actions for the fork and permit workflow release creation. There is no need for Companion Cloud or ScreenScraper credentials.

## macOS packaging

The workflow does not import certificates, perform Developer ID signing, submit notarization requests or require Apple secrets. It packages the application directly into a read-only DMG. PyInstaller handles the internal ad-hoc signatures required for native Apple Silicon executables; these do not identify a developer or notarize the app.

The icon is generated from the supplied reference and stored in `tools/rom-maker/app/assets/icon.png`; Windows ICO and macOS ICNS variants are derived during packaging.

## Local packaging

Run on the matching native OS and architecture, with Python 3.13 and an internet connection for assembling the package:

```sh
python -m pip install -r tools/rom-maker/app/requirements.txt
python tools/rom-maker/app/package.py --platform linux-x64
```

Use `windows-x64` or `macos-arm64` on the respective machines. `--reuse-payload` reuses an already assembled `build/desktop/payload` while rebuilding the launcher. Windows output is under `dist/desktop`; macOS produces a `.app` there. Linux produces `build/desktop/AppDir`, which the workflow converts into an AppImage. DMG creation is performed by the workflow.

For source development, first assemble the payload with `portable.py`, install the desktop requirements and run:

```sh
python tools/rom-maker/app/main.py
python tools/rom-maker/app/main.py --self-test --report self-test.json
python -m unittest discover -s tools/rom-maker -p 'test_*.py'
```

Runtime download URLs and SHA256 values are in `tools/rom-maker/portable-runtimes.json`. Apple Silicon uses native ARM64 Python, Node and Zig, and ARM64 Pillow/NumPy wheels. The native helper compiler selects the host target instead of hard-coding Linux x64. Game compilation continues to use the existing WASM toolchain.

Third-party tools retain their original licences and notices in each payload, including `THIRD-PARTY-NOTICES.txt`. The app does not bundle arcade ROMs, generated game assets or completed cartridges. The source licence does not cover original arcade content.
