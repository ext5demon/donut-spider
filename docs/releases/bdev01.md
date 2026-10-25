# Spider Donut bdev01

First development build after v0.1 Beta, focused on the Chapter 3 desert tutorial
pit interaction in `room_board_intro`.

`mp_grid_path` now replaces the destination path's closed/curved settings with an
open, straight route. New paths default to closed, and preserving that setting
made the companions walk back toward the start instead of stopping at the goal.
The pit sequence waits for both companions to arrive, so that incorrect endpoint
can leave the sequence waiting indefinitely.

A regression test checks the two relative companion destinations, reuse of a
previously closed/curved path, cardinal/diagonal movement and same-cell routes.
The installation guide now explains converting external videos into matching
BSV/Ogg files. Converted game videos remain user-supplied assets.

Install `spider-donut-bdev01.pkg` over the existing app. Title ID DONSD0001 is
unchanged; installer revision is 00.14. Keep your chapter files, video sidecars
and saves. The preloader artwork, sounds, animations and layout are unchanged.

Build from source with `PS3DEV=/path/to/ps3dev bash tools/build-bdev01.sh`.
Development builds use the `bdevXX` version convention; v0.1 remains the public
beta release.

The log also reports unsupported texture-filter, primitive-drawing and date APIs.
Those are separate compatibility items and are not fixed by this pathfinding
change. This development package still needs confirmation at the reported scene
on physical PS3 hardware.
