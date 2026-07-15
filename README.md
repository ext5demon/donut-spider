# Donut Spider

![Donut Spider logo](assets/donut-spider-logo.png)

**PA_001 — Pre-alpha**

Donut Spider is an experimental GameMaker bytecode runner with a PlayStation 3 backend. The current PS3 build focuses on WAD 17 games and keeping memory use inside the console's limits.

This is test software. It is playable in places, but it is not accurate or stable enough to call finished. Back up your saves.

## Before you install it

The repository and release package do not include games, WADs, saves, music, video, or extracted commercial assets. You need to supply files from your own copy of a game.

A PS3 game bundle currently looks like this:

```text
my-game/
  data.win
  TEXTURES.BIN
  mus/
  ...other audio and video files used by the game...
```

`TEXTURES.BIN` is the PS3 texture-streaming bundle. Keep it beside `data.win`; lowercase `textures.bin` is also accepted. Donut Spider will refuse to open a WAD when the texture bundle is missing.

Donut Spider is unofficial and is not affiliated with Toby Fox, 8-4, GameMaker, or YoYo Games.

## Installing PA_001 on PS3

1. Download `Donut-Spider-PA_001.pkg` from the GitHub release.
2. Install it through Package Manager on a CFW or HEN-enabled PS3.
3. Copy your private game bundle to the internal drive or a USB device.
4. Launch **Donut Spider**.
5. Use **SELECT WAD** to browse to the `.win` file and press Cross.

The public package installs as title ID `DONS00001`. The separate developer package uses `DONSD0001`, so both builds can be installed at once.

Every launch creates a persistent session log in the app's `USRDIR/logs/` directory on the PS3 HDD. Logs record the selected WAD, detected game and WAD versions, room transitions, ten-second runtime/memory heartbeats, fatal load errors, and the session exit reason. Each entry is flushed immediately, so the log remains useful when a game or emulator crashes before a clean shutdown.

### WAD picker controls

- D-pad: move
- Cross: open directory or select WAD
- Circle: go back
- Square: refresh storage

### In-game controls

- D-pad or left stick: movement
- Cross: Z / confirm
- Square: X / cancel
- Triangle or Start: C / menu
- L1 and R1: Page Down and Page Up
- L2: F10

## Known PA_001 problems

- PS3 surface captures can lose their alpha channel. The Chapter 4 prophecy sequence is visibly broken and some captured sprites have black rectangles.
- Heavy battles and effects can dip below their intended frame rate.
- WAD 17 support does not mean every game or every room is supported. Missing GML functions, shaders, codecs, and platform behavior can still stop a run.
- This build has had hands-on RPCS3 testing, but it has not completed enough hardware testing for a stable release.

Please include the version, game version, room name, and the last visible action when reporting a bug.

## Building the PS3 version

You need:

- a working [PSL1GHT/ps3dev](https://github.com/ps3dev/PSL1GHT) toolchain
- PSL1GHT's PPU SDL2 portlib
- CMake 3.21 or newer
- Unix Makefiles or another CMake generator supported by your toolchain
- PowerShell for the packaging script

Set `PS3DEV` and `PSL1GHT` to the toolchain root. From the repository root, configure the public build:

```powershell
$env:PS3DEV = 'C:\ps3dev'
$env:PSL1GHT = $env:PS3DEV

cmake -S . -B build-ps3-public -G 'Unix Makefiles' `
  -DPLATFORM=ps3 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:PS3DEV\portlibs\ppu" `
  -DENABLE_WAD14=OFF `
  -DENABLE_WAD16=OFF `
  -DENABLE_WAD17=ON `
  -DDONUT_SPIDER_VERSION=PA_001 `
  -DDONUT_SPIDER_DEV_BUILD=OFF `
  -DDONUT_SPIDER_DEV_PRELOAD_WAD=OFF

cmake --build build-ps3-public --parallel
pwsh tools/build-ps3-package.ps1 -Variant Public -BuildDirectory build-ps3-public
```

The package and SHA-256 checksum are written to `dist/`.

For the diagnostic build, change `DONUT_SPIDER_DEV_BUILD` to `ON`, use a separate build directory, then package it with `-Variant Dev`:

```powershell
cmake -S . -B build-ps3-dev-release -G 'Unix Makefiles' `
  -DPLATFORM=ps3 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="$env:PS3DEV\portlibs\ppu" `
  -DENABLE_WAD14=OFF `
  -DENABLE_WAD16=OFF `
  -DENABLE_WAD17=ON `
  -DDONUT_SPIDER_VERSION=PA_001 `
  -DDONUT_SPIDER_DEV_BUILD=ON `
  -DDONUT_SPIDER_DEV_PRELOAD_WAD=OFF

cmake --build build-ps3-dev-release --parallel
pwsh tools/build-ps3-package.ps1 -Variant Dev -BuildDirectory build-ps3-dev-release
```

The developer build starts with its diagnostics visible. Select toggles the overlay while a game is running. `DONUT_SPIDER_DEV_PRELOAD_WAD` exists for private repeatable testing only; do not enable it for public packages.

## Desktop development build

Desktop builds are useful for checking parser and VM changes before waiting on the PS3 linker. Choose an installed backend and configure it normally. For example, with SDL2:

```powershell
cmake -S . -B build-windows-debug `
  -DPLATFORM=desktop `
  -DDESKTOP_BACKEND=sdl2 `
  -DCMAKE_BUILD_TYPE=Debug `
  -DENABLE_WAD17=ON

cmake --build build-windows-debug --parallel
```

## Project history and license

Donut Spider is derived from [Butterscotch4PS3](https://github.com/WinG4merBR/Butterscotch4PS3) and [Butterscotch](https://github.com/ButterscotchRunner/Butterscotch). Their work made this port possible.

Source code is available under the [Mozilla Public License 2.0](LICENSE). The logo in this repository is the project's supplied artwork; it is not extracted from a game.
