# Spider Donut bdev03

This development build fixes a reproduced cause of invisible Roaring Knight attack effects. The legacy renderer inherited OpenGL's `ALWAYS` alpha comparison. When the game enabled alpha testing without setting a reference, transparent sprite pixels were written into destination-alpha masks and erased the attack layer. Startup now initializes the comparison to `GREATER` with a zero reference, while leaving alpha testing disabled until requested by the game. This applies to the PS3 renderer as well as the desktop legacy renderer.

The controlled native Chapter 3 encounter previously displayed the Knight's pointing animation and heart while hiding the stars and beam. Running the same game scripts and frame with the correction displays the masked stars and beam. This is an isolated encounter fixture, not a complete playthrough or physical PS3 confirmation. The tester should retry the reported hand-opening burst, other invisible attacks, and the floor/background animation. The background's game code and animation run in the native fixture; the reported PS3 floor problem is not yet independently confirmed fixed.

The dev7.1/v0.1 preloader artwork, animation, layout and controls are unchanged. This includes the bdev02 drawing and audio changes; the tester already confirmed Tenna's reported fight effect was fixed in bdev02.

Validation:

- Normal host tests and AddressSanitizer/UndefinedBehaviorSanitizer host tests.
- A regression through the real renderer initialization, followed by a destination-alpha mask test that enables alpha testing without setting its reference. Transparent texels preserve the mask; nontransparent texels still blend.
- Before/after native captures from the owned Chapter 3 scripts. Private game data and capture fixtures are excluded from the package and source archive.
- PS3 cross-build, package metadata/footer and source/private-file audit.

Install `spider-donut-bdev03.pkg` over the existing Spider Donut installation. Title ID stays `DONSD0001`; installer revision is `00.16`. Keep the existing chapter files, texture bundles, videos and saves. This update does not include or regenerate chapter data or texture bundles. Development source is kept local; the public v0.1 release is unchanged.
