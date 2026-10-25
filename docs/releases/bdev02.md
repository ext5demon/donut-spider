# Spider Donut bdev02

Chapter 3 compatibility fixes following the first full beta QA report. Includes
the bdev01 desert-pit fix. This is a development build for another console test,
not confirmation that every reported issue has been resolved.

- Implement the immediate primitive API, including triangle fans and textured,
  coloured vertices. Chapter 3 uses these for its clock wipe, skewed sprites,
  circle-width helpers and some Knight attacks.
- Render all four colours in `draw_sprite_general`. Clip partial sprites in
  their original coordinate space before converting to downscaled atlas texels.
- Add the brightness-to-transparency shader used by Tenna's laughing-star attack,
  the photo/sand grayscale shader and tone uniforms to the legacy/PS3 renderer.
- Preserve sound and stream resource gain/pitch defaults. Keep each voice's
  volume separate from its asset, group and emitter volume. Support the gain
  emitters used for rhythm-room crowd audio.
- Report paused audio correctly, preserve paused sample voices under slot
  pressure, and resume only paused tracks instead of restarting playing ones.
- Batch each circle into one renderer submission, use logarithmic lookup for
  long projectile paths, and free strings allocated by shader uniform lookups.

## QA follow-up

The following table distinguishes implemented engine changes from console
confirmation. There is no new PS3 session log or screenshot attached to this
report. Host checks cannot establish the final PS3 frame rate, sound balance,
or every scene's appearance.

| Report | Follow-up in this build |
| --- | --- |
| 1. Pink stage does not fade | Still under investigation; need to compare the reported image/sequence. |
| 2. Shadowguy trumpet slowdown/quiet audio | Faster path sampling and renderer/audio changes; exact encounter performance and mix remain unconfirmed. |
| 3. Missing clock transition | Required primitive API implemented; fan drawing covered by tests. Console retest needed. |
| 4. Noelle voice on vending-machine Buy | Cause not reproduced; remains open. No sound has been suppressed or substituted. |
| 5. Glitched Tenna antenna sound | Persistent pitch defaults and resume handling corrected; exact sound still needs a console comparison. |
| 6. Missing photograph effect | Required grayscale/sand shader implemented and uniform routing tested. Console retest needed. |
| 7. Island physical-combat slowdown | General drawing/path improvements included; real-console performance remains open. |
| 8. Rock Show shadows are horizontal strips | Skewed/textured primitives and atlas clipping corrected; exact scene still needs comparison. |
| 9. Music stays silent after being caught | Pause queries, resume and sound gain handling corrected; retry the stealth sequence. |
| 10. TV/quiz room slowdown | Shader-uniform string leak fixed and primitive/circle draws batched; console timing remains unconfirmed. |
| 11. White circular lines during laughing stars | Required brightness-to-transparency shader implemented; host mask pixels checked. Console retest needed. |
| 12. Rhythm notes lack feedback/guitar | Audio routing and independent layer gains corrected; note feedback is not confirmed fixed. |
| 13. Invisible Knight projectiles | Missing primitive drawing implemented, including the Knight cone. Other projectiles still need retesting. |
| 14. Invisible dog during Knight chase | Cause not reproduced; remains open. |

## Install and build

Install `spider-donut-bdev02.pkg` over the existing Spider Donut application.
Title ID DONSD0001 is unchanged; installer revision is 00.15. Keep all chapter
folders, video sidecars and saves. The preloader artwork, sounds, layout and
animations are unchanged from v0.1/dev7.1. The public v0.1 release is unchanged.

Build using `PS3DEV=/path/to/ps3dev bash tools/build-bdev02.sh`. The checked-in
RSX effect binary is generated from `src/ps3/color_effect.fcg` with PSL1GHT
`cgcomp` and NVIDIA Cg 3.1. Game files and extracted game shaders are not included.

Host regressions include 28 normal and sanitizer tests. See `docs/testing.md`
for the reproducible test commands and their limitations.
