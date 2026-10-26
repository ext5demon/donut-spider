# Spider Donut bdev04

This development build reduces repeated work in the PS3 palette drawing path. Palette-row and fog constants belong to private fragment programs and survive switching programs. The renderer now uploads them only when their values change. This removes redundant command-buffer transfers in projectile and afterimage draws. Program binding, hardware draw-state submission and matrix uploads remain unchanged, including the startup/surface-transition behavior restored in dev7.1.

This is a performance change for retesting the Roaring Knight's full-screen explosive-spike attack. A regression requests the same palette/fog state 1,000 times, verifies that only two uniform calls are needed, and checks the rendered pixels before and after changing values and programs. This does not establish a physical PS3 frame-rate improvement or guarantee a steady 30 FPS in that attack.

The reported disappearance after beating the Knight is **still under investigation**. Native checks through the game's own post-battle checkpoints display the replacement sprites. A separate forced normal-enemy cleanup reproduces an off-screen cutscene actor: the scene reads the removed battle actor's position as zero. That establishes a failure path, but does not prove what caused the tester's console to enter it. This build adds one-second `KNIGHT STATE` reports and `KNIGHT DESTROY` records, including the calling game script, to the existing session log. It does not change battle outcomes, skip the cutscene or hide the symptom by forcing a sprite.

The dev7.1/v0.1 preloader artwork, layout, animations and controls are unchanged. Existing bdev03 alpha-mask fixes remain included.

Validation:

- Normal host regression suite and AddressSanitizer/UndefinedBehaviorSanitizer suite.
- Real GL uniform/pixel checks for repeated state, value changes and program changes.
- Native checks using privately owned Chapter 3 data and isolated fixtures; these are not a full playthrough or a PS3 emulator test.
- PS3 cross-build, package metadata/footer, exact source archive, private-file and secret audit.

Install `spider-donut-bdev04.pkg` over the existing installation. Title ID stays `DONSD0001`; installer revision is `00.17`. Keep the existing chapter data, texture bundles, videos and saves. No chapter data or texture bundles are included or regenerated. Development changes stay local; the public GitHub release is unchanged.

Retest the full-screen spike attack, then the Knight's defeat and ball/injured animations. The log is in `/dev_hdd0/game/DONSD0001/USRDIR/logs/`.
