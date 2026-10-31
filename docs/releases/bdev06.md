# Spider Donut bdev06

This development build follows the Chapter 4 report and the bdev05 session ending in `room_dw_churchc_titanclimb1_post`.

## Pulse animation

Animation-curve evaluation previously treated Bezier channels as straight lines between knots. The game's Sanctuary pulses use Bezier handles to expand quickly and then slow down. The evaluator now uses both X and Y handles and the channel's sampling resolution, following [GameMaker's sampled-cubic behavior](https://github.com/YoYoGames/GameMaker-HTML5/blob/develop/scripts/yyAnimCurve.js). This restores the authored radius and its associated fade timing instead of increasing pulse size or brightness by an arbitrary multiplier.

On PS3, pulses now use a dedicated fragment program with no texture sampling or palette/hue/tone dispatch. Other colour effects return to their smaller pre-bdev05 program. This reduces the work in the pulse path; a physical PS3 frame-rate improvement has not been measured.

## Crystal tower

The tower script moves its crystal sprites onto layers created at runtime. The renderer previously required a matching parsed room layer before drawing sprite elements, so those moved sprites disappeared. Runtime sprites now draw on new layers too, preserving layer offsets, sprite transforms, tint, alpha and visibility. Moving an element to its current layer or an invalid destination no longer risks invalidating its storage.

## Performance reports requiring retesting

The supplied log shows progressively longer GPU waits followed by a backend fence timeout. The isolated native Spawn appearance check completes without reproducing that progressive slowdown. **This build does not establish that the Titan Spawn console crash is fixed.** The Wicabel/Organikk encounter and the black-flame tower chase also need console retesting.

Slow swaps now record `GPU FRAME` with wait duration, draw count and vertex count, at most once per second. A GPU panic also records `RSX_WORK`. These distinguish growing submitted geometry from slow execution of a bounded workload. Existing `SLOW PHASE` and stall-watchdog records remain available. The watchdog timeout is unchanged; encounters and effects are not skipped.

## Validation and installation

- 30 normal host regressions and the same suite with AddressSanitizer/UndefinedBehaviorSanitizer.
- New regression covers sampled Bezier handles and sprite moves/drawing on runtime-created layers.
- Owned Chapter 4 native fixtures compare the old and corrected pulse animation and moved-layer drawing, and exercise the Spawn appearance. These are isolated tests, not a PS3 emulator or a complete playthrough.
- Dedicated PS3 shader compilation, PS3 cross-build, package/metadata and private-file/secret audit.

Install `spider-donut-bdev06.pkg` over the current installation. Title ID is `DONSD0001`; installer revision is `00.21`. The supplied development logo remains the XMB icon and background. Preloader menu files match bdev05/dev7.1; this build does not claim to diagnose the separately reported old-picker screen. Keep existing chapter data, texture bundles, videos and saves. No chapter data or texture bundles are included or regenerated. GitHub stays on v0.2.

Retest pulses in both Sanctuaries, the crystal tower, the Wicabel/Organikk fight, the black-flame chase and the Spawn appearance/fight. Logs are in `/dev_hdd0/game/DONSD0001/USRDIR/logs/`.
