# Spider Donut v0.1 Beta

![Spider Donut logo](assets/donut-spider-logo.png)

Spider Donut is an experimental GameMaker bytecode runner for **PlayStation 3**,
using the native libgcm / RSX backend. This beta builds on dev7.2, with chapter
discovery, controller handoff, surface/readback, rendering and audio fixes
developed through PS3 playtesting.

[Download v0.1](https://github.com/ext5demon/donut-spider/releases/tag/v0.1)
· [Report a bug](https://github.com/ext5demon/donut-spider/issues)

## Chapter status

DELTARUNE compatibility reported by the project's playtesters for this beta:

| Chapter | Status |
| --- | --- |
| Chapter 1 | Fully playable |
| Chapter 2 | Playable with some minor bugs |
| Chapter 3 | Doesn't work |
| Chapter 4 | Bootable |
| Chapter 5 | Doesn't work |

**Bootable** means the chapter starts; it does not mean it can be completed.
Compatibility depends on the game version and matching chapter assets. This is
beta software, and slowdowns or unsupported effects can still occur.

## What you need

- A PS3 running homebrew through HEN or CFW, and a USB drive for installation.
- Your own legally obtained PC game files for each chapter you want to play.
- A PS3 `TEXTURES.BIN` generated from **that exact chapter's `data.win`**.

No DELTARUNE WADs, chapter texture bundles, gameplay music, videos or saves are
included in the release. The launcher restores the dev7.1 menu font, chapter
icons, SOUL cursor and menu sounds, with the same entrance and launch animations.
These DELTARUNE menu assets belong to their original creators; see the
[menu asset notice](assets/picker/NOTICE.txt). Spider Donut is independent of
Toby Fox, 8-4, GameMaker and YoYo Games.

## Install and play

1. Download `spider-donut-v0.1.pkg` from the [v0.1 release](https://github.com/ext5demon/donut-spider/releases/tag/v0.1).
2. Copy the PKG to a FAT32 USB drive, then install it with the PS3's Package Manager.
3. Prepare your chapter folders as described below and copy them to USB or internal storage.
4. Launch **Spider Donut v0.1 Beta** from the XMB. It searches mounted storage for chapters.
5. Select a discovered chapter with **Cross**, then select **Play**. Use **Browse**
   to select a `data.win` manually, or **Refresh** after connecting storage.

Keep your existing chapter folders and saves when upgrading a dev6/dev7 build;
install over the app rather than deleting it. The app uses title ID **DONSD0001**
and PS3 installer revision **00.13**, independently of the public version `v0.1`.

## Build TEXTURES.BIN

Use the upstream [Butterscotch Preprocessor](https://github.com/ButterscotchRunner/ButterscotchPreprocessor).
**Always pass `--target ps3`; the converter defaults to PS2.** Renaming a PS2
bundle to `TEXTURES.BIN` does not convert it.

These commands are for Linux, macOS or Windows through WSL, with **JDK 21** and
Git installed. This pinned revision has the PS3 converter and CLI options used
in these instructions:

```bash
git clone https://github.com/ButterscotchRunner/ButterscotchPreprocessor.git
cd ButterscotchPreprocessor
git checkout 32664f834127c229d19411e6d3488220d8062b63
chmod +x gradlew
./gradlew :processor-cli:installDist
```

Run the generated launcher once for **each chapter**, with a separate output
folder. Replace the two example paths with your own:

```bash
./processor-cli/build/install/processor-cli/bin/processor-cli \
  --target ps3 \
  --output "/path/to/prepared/chapter2_windows" \
  "/path/to/DELTARUNE/chapter2_windows/data.win"
```

At this converter revision the output filename is lowercase `textures.bin`;
Spider Donut accepts both `textures.bin` and `TEXTURES.BIN`. Keep one copy beside
the matching `data.win`.

Copy the chapter's **original, unchanged** `data.win`, external audio files,
audio-group files and other required loose assets into the prepared chapter
folder. Preserve subfolders and filenames. The PS3 converter generates textures;
it does not copy the rest of the game for you. Keep any shared music/assets from
the PC installation alongside the chapter folders, as shown below.

- Repeat with Chapter 1's `data.win` for Chapter 1, Chapter 2's for Chapter 2, etc.
- Do not mix chapters' bundles or bundles from different game updates.
- Do not use the PS2 atlas/ISO conversion workflow for PS3.
- If you already have working matching PS3 bundles from dev7.2, reuse them.

### Prepare cutscene videos

Chapters that play external MP4 videos also need BSV video and Ogg audio files.
The PS3 runner opens `<original-video-filename>.bsv` and its matching `.bsv.ogg`
audio track. Copying an original MP4 alone does not prepare it for this player.

With Python 3 and FFmpeg (including `ffprobe`) installed, run the source converter
for each chapter video. For Chapter 3's English and Japanese Tenna introductions:

```bash
python3 tools/transcode_video.py "/path/to/DELTARUNE/chapter3_windows/vid/tennaIntroF1_compressed_28.mp4"
python3 tools/transcode_video.py "/path/to/DELTARUNE/chapter3_windows/vid/tennaIntroJPf1_compressed_28.mp4"
```

Each command writes `.mp4.bsv` and `.mp4.bsv.ogg` files beside the source MP4.
Copy both into the PS3 chapter's `vid/` folder with the full filenames intact.
Generate these from the same game version as your chapter files. A log containing
`Video: streamed sidecar not found` means the required converted video is missing.

### USB / internal-storage layout

Example USB root after copying your own files:

```text
USB_ROOT/
  spider-donut-v0.1.pkg
  DELTARUNE/
    mus/                         # shared music, if present in your PC copy
    chapter1_windows/
      data.win
      TEXTURES.BIN
      ...chapter assets...
    chapter2_windows/
      data.win
      TEXTURES.BIN
      ...chapter assets...
    chapter4_windows/
      data.win
      TEXTURES.BIN
      ...chapter assets...
```

The PS3 sees this as `/dev_usb000/DELTARUNE/` (the USB number can vary). The same
`DELTARUNE` folder can be placed on internal storage, for example
`/dev_hdd0/DELTARUNE/`. Other mounted storage and manual browsing are supported.
The status table still applies when an unsupported chapter is discovered.

## Controls

| PS3 button | Chapter picker | In game |
| --- | --- | --- |
| D-Pad / left stick | Navigate | Move |
| Cross | Select / confirm | Confirm / interact (`Z`) |
| Circle | Back / cancel | Cancel / run (`X`) |
| Square | Search again | Cancel / run (`X`) |
| Triangle | — | Menu (`C`) |
| Start | — | Toggle diagnostic overlay |
| L1 / R1 | — | Page Down / Page Up debug keys |
| L2 | — | F10 debug key |

## Saves, logs and bug reports

Saves are under `/dev_hdd0/game/DONSD0001/USRDIR/saves/`, separated by game ID.
Back them up before upgrading. Logs are under:

```text
/dev_hdd0/game/DONSD0001/USRDIR/logs/session-*.log
```

For a bug report, include the chapter, game version, room/scene, reproduction
steps and newest session log. Screenshots or video help with visual issues.
Check logs for personal information before posting them. Do not upload
`data.win`, texture bundles, music or saves to a public issue.

Known limits include demanding scenes dropping frames or stuttering, incomplete
shader/GML coverage, and the chapter support shown above. Enemy-dialogue clipping
and the Lancer Key Item highlight discrepancy remain open PS3 QA items. Host tests
do not establish full real-console compatibility.

## Build the PS3 package

Use Linux (or Linux under WSL) with a working
[ps3dev / PSL1GHT toolchain](https://github.com/ps3dev/ps3dev), the PPU SDL2 portlib,
CMake 3.21+, Ninja, Python 3 and the ps3dev packaging tools (`make_self_npdrm`,
`sfo`, `pkg`, `package_finalize`). No Sony SDK is required.

```bash
git clone https://github.com/ext5demon/donut-spider.git
cd donut-spider
git checkout v0.1
export PS3DEV=/path/to/ps3dev
export PSL1GHT="$PS3DEV"
bash build.sh
```

The script builds and finalizes `dist/spider-donut-v0.1.pkg` and a SHA-256 checksum.
Set `JOBS`, `BUILD_DIR` or `OUTPUT_DIR` to override the defaults. The dev7.1 menu
assets and embedded header are checked in; ordinary builds need no game copy,
font download or image-generation step. To rebuild the header from those assets:

```bash
python3 -m pip install Pillow
python3 tools/embed_picker_assets.py
```

For host regression tests, see [docs/testing.md](docs/testing.md). They cover
controllers, surfaces, bounded DMA submission, VM behavior, audio and the picker's
PS3 draw-state submission at 480p, 720p and 1080p, without commercial-game downloads.

## History and licensing

Spider Donut (formerly Donut Spider) is derived from
[Butterscotch4PS3](https://github.com/WinG4merBR/Butterscotch4PS3) and
[Butterscotch](https://github.com/ButterscotchRunner/Butterscotch).
Runner source is under [MPL-2.0](LICENSE). DELTARUNE menu artwork, fonts and sounds
are excluded from that license; see the [menu asset notice](assets/picker/NOTICE.txt).
Third-party libraries retain their own notices. DELTARUNE and its assets belong
to their respective creators.
