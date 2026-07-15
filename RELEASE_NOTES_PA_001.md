# Donut Spider PA_001

This is the first public Pre-alpha build of Donut Spider for PlayStation 3.

The release includes the installable PS3 package. It does not contain a WAD, game assets, saves, or any other commercial game data. On first boot, choose **SELECT WAD** and browse to your own game bundle.

## What works

- WAD 17 loading on PS3
- controller-driven storage and WAD picker
- streamed PS3 texture bundle support through `TEXTURES.BIN`
- sound effects, voices, streamed audio, and BSV video playback
- mark-and-sweep collection for cyclic GML heap values
- persistent per-session HDD logs with WAD details, room transitions, and runtime/memory heartbeats
- local build paths and debug symbols stripped from the release binary

## Known problems

- Surface captures can lose alpha on PS3. The Chapter 4 prophecy sequence is visibly broken and some captured sprites have black boxes.
- Some heavy battles and effects can run below their intended frame rate.
- Compatibility is incomplete. Back up saves before testing.

This build is meant for testing, bug reports, and development. It is not a stable release.
