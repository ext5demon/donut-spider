# Spider Donut Alpha 1 (A1)

![Donut Spider logo](assets/donut-spider-logo.png)

**Spider Donut Alpha 1 (A1)**

Spider Donut (formerly Donut Spider) is an experimental GameMaker bytecode runner with a native PlayStation 3 (libgcm / RSX) backend. This release focuses on WAD 17 games (such as Deltarune Chapter 1 & 2) and optimizing performance and memory usage for the PS3 console architecture.

This is alpha software. It is playable across major game rooms, but it is still under active development. Always back up your save files.

## Before You Install

The repository and release packages do not include game assets, WADs, saves, audio, or video files. You must supply your own `data.win` and texture assets from your legally owned copy of the game.

A PS3 game directory bundle should be structured as follows:

```text
my-game/
  data.win
  TEXTURES.BIN
  mus/
  ...other game assets...
```

`TEXTURES.BIN` is the PS3 texture streaming bundle. Keep it in the same directory as `data.win` (lowercase `textures.bin` is also supported).

Spider Donut is an independent open-source project and is not affiliated with Toby Fox, 8-4, GameMaker, or YoYo Games.

## Installation

1. Download `spider-donut-A1.pkg` from the release repository.
2. Install the PKG on a HEN or CFW-enabled PlayStation 3 system using Package Manager.
3. Copy your game bundle to the PS3 internal storage (`/dev_hdd0/`) or an external USB drive.
4. Launch **Spider Donut Alpha 1** from the XMB.
5. Use the built-in WAD picker to navigate to your `data.win` file and press **Cross** to boot.

Log files are stored in `USRDIR/logs/` on the PS3 hard drive. Logs record runtime statistics, frame rates, room transitions, and diagnostic heartbeats.

## Controls

### WAD Picker Controls

| Button | Action |
|---|---|
| D-Pad Up / Down | Navigate entries |
| Cross | Select WAD or open directory |
| Circle | Go to parent directory |
| Square | Refresh storage list |

### In-Game Controls

| PS3 Button | Game Input / Mapping |
|---|---|
| D-Pad / Left Analog | Movement |
| Cross (`X`) | Confirm / Interact (`Z`) |
| Circle (`O`) / Square (`[]`) | Cancel / Run / Back (`X`) |
| Triangle (`/\`) | Menu (`C`) |
| Start / Options | Toggle Developer Overlay |
| L1 / R1 | Page Down / Page Up |
| L2 | F10 |

## Known Bugs & Limitations in Alpha 1

- **Surface Capture Alpha Channels:** Framebuffer surface readbacks used in specific cutscene prophecy effects may drop alpha channels, rendering behind dark rectangles.
- **Dynamic Asset Stalls:** Uncached texture assets or large room transitions can cause temporary frame drops during loading.
- **Coverage:** Some specialized GML built-in functions or uncommon audio codecs may produce fallback behavior.
- **Hardware Testing:** Testing is ongoing on native PS3 hardware and RPCS3 emulator.

## Building from Source

### Prerequisites

- A working [PSL1GHT/ps3dev](https://github.com/ps3dev/PSL1GHT) toolchain
- PPU SDL2 portlib
- CMake 3.21 or newer
- Make or Ninja build tools

### Build Instructions

Set your `PS3DEV` and `PSL1GHT` environment variables to your local toolchain path, then run:

```bash
export PS3DEV=/path/to/ps3dev
export PSL1GHT=$PS3DEV

cmake -S . -B build-ps3-release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/ppu.cmake \
  -DPLATFORM=ps3 \
  -DCMAKE_BUILD_TYPE=Release \
  -DDONUT_SPIDER_VERSION=A1 \
  -DDONUT_SPIDER_DEV_BUILD=ON

cmake --build build-ps3-release --parallel
```

To create the final PKG installer:

```bash
./build.sh
```

The resulting package `spider-donut-A1.pkg` will be generated in the `dist/` directory.

## Project History & License

Spider Donut is derived from [Butterscotch4PS3](https://github.com/WinG4merBR/Butterscotch4PS3) and [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch).

Source code is released under the [Mozilla Public License 2.0](LICENSE).
