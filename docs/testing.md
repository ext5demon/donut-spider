# Host regression tests

These tests use synthetic input and existing public sample projects. They do not
require DELTARUNE files, commercial-game downloads, a PS3 SDK, or an emulator.
The PS3 draw tests record RSX API calls; they do not execute physical GPU shaders.

On Debian/Ubuntu Linux:

```bash
sudo apt-get install cmake ninja-build gcc pkg-config libsdl2-dev libglfw3-dev libbz2-dev xvfb xauth
bash tools/test-host.sh
BUILD_DIR=build-sanitize SANITIZE=ON bash tools/test-host.sh
```

There are 26 CTest tests, covering controller handoff/reconnection and stick
thresholds, cropped pixel transfers, bounded DMA command submission, surfaces
and lifetime reuse, chapter scanning, picker animation/input/draw state, texture
bundle validation with synthetic fixtures, audio streaming/pitch/start-stop
tails, GML VM behavior, garbage collection and vertex buffers.

The real picker is exercised through production PS3GL with a host RSX recorder
at 480p, 720p and 1080p. Native OpenGL checks cover visible atlas pixels, cursor,
separators, aspect ratio and transitions. The audio tests use SDL's dummy device.

Run a specific test with `ctest --test-dir build-tests -R picker --output-on-failure`.
No real-console performance or complete chapter playthrough is implied by a
passing host test. Chapter statuses in the README come from project playtesters.
