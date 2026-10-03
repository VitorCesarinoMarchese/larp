# Live Linux preview

Milestone 6 adds a resizable window for received raw RGB and H.264 video.
Local and two-machine Tailscale verification are complete with the software
codec. Milestone 5's NVIDIA leak blocker remains separate.

## Run a preview

The default build requires SDL3 3.2 or newer and an X11 display. On Wayland,
run through Xwayland. This CPU presentation path does not support native
Wayland without Xwayland. Use `-DLARP_RENDER=OFF` for a build without SDL3 or
live-window support. Capture has its own `LARP_CAPTURE` option.

Start the receiver:

```sh
./build-release/larp-client --view-h264 127.0.0.1 5000 60
```

After it prints `Listening`, start a software sender in another terminal:

```sh
./build-release/larp-host --h264-synthetic 127.0.0.1 5000 900 30
```

For a remote transfer, replace the loopback address in both commands with the
receiver's Tailscale IPv4 address. Use the existing host `--h264` capture mode
to send screen previews. The live receiver does not request a snapshot path.
Existing `--raw` and `--h264` receiver commands still write snapshots.

Raw RGB uses the same window:

```sh
./build-release/larp-client --view-raw 127.0.0.1 5000 60
```

Both live modes accept a final frame timeout from 1 to 1,000 milliseconds.
`--view-h264` also accepts `--codec software|nvidia` after the timeout, when
present. Software is the default. The renderer remains CPU-based with either
decoder. NVIDIA decoding retains its known driver-leak limitation.

Resize the window to fit the desktop. Black bars preserve the frame's aspect
ratio. Close the window or press Escape to stop before the duration expires.
The last valid frame remains visible if the sender stops. Invalid frames do
not replace it.

## Ownership and latency

`client/main.cpp` receives UDP packets, reassembles one frame, validates or
decodes it, and calls `VideoWindow::present(RawFrameView)`. The renderer copies
RGB bytes before returning because the client reuses the source storage.
`VideoWindow::poll()` handles close, Escape, exposure, and resize events on the
same thread. Both the idle path and the packet path poll events before each
receive. The network wait is at most 10 ms.

`render/window_sdl.cpp` owns one RGB surface and one SDL window. It replaces the
RGB surface only when frame dimensions change, respects SDL's row pitch, and
reacquires the window surface for each redraw. SDL invalidates that surface
after resize, as described by [SDL_GetWindowSurface](https://wiki.libsdl.org/SDL3/SDL_GetWindowSurface).
RAII releases the RGB surface, window, and video subsystem in that order.

Presentation uses a CPU scaled blit followed by a window-surface update. The
application selects X11 by default, while honoring explicit SDL video-driver
selection. It disables framebuffer acceleration inside this process. It does
not change the desktop compositor, installed driver, or system settings.
Surface vsync keeps SDL's [default disabled setting](https://wiki.libsdl.org/SDL3/SDL_SetWindowSurfaceVSync).
The compositor still controls when pixels reach the display.

The design comparison selected a synchronous window over a receiver worker
with a latest-frame mailbox. It keeps borrowed-frame ownership and shutdown
in one thread. The independent design review agreed with that choice. Direct
Wayland shared-memory buffers were also considered, but would add configure,
buffer-release, and resize handling that SDL already supplies through X11.
CPU window surfaces replaced the proposed SDL renderer to avoid loading a GPU
renderer for the existing small CPU previews.

There is no application display queue. The retained RGB frame is bounded to
4096 by 2160, and raw transport's 4 MiB cap is smaller. Current H.264 frames
remain bounded to 320 by 180. Window buffers scale with window pixel size.
Presentation blocks receive while the blit and update run. If future workloads
outpace this path, a latest-frame mailbox needs measured justification.

`Presented` counts successful updates for received frames, excluding idle
redraws. `Present mean us` and `Present max us` include the RGB copy, scale,
and SDL update call. They are CPU-call durations, not display scanout or
end-to-end latency measurements.

## Verification

```sh
ctest --test-dir build-release --output-on-failure
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitize -LE nvidia --output-on-failure
python3 benchmarks/render.py build-release
ASAN_OPTIONS=detect_leaks=1 python3 benchmarks/render.py build-sanitize --frames 300
```

On 2026-09-21, all 21 Release tests and all 16 non-NVIDIA sanitizer tests passed.
A build with capture and rendering disabled passed all 12 tests and rejected
live-window requests with an explicit build-option error.

The new render test checks actual surface pixels, RGB order, padded rows,
frame replacement, dimension changes, aspect ratio, black bars, repaint after
source-buffer reuse, malformed input, close, Escape, and repeated lifecycles.
The client test sends both raw and software H.264 frames, checks presentation
counts, rejects corrupt frames, and exercises unavailable-display errors.
These automated tests use SDL's dummy display.

Separate desktop tests used SDL 3.4.16 on Xwayland under Hyprland. A cropped
window screenshot verified red, green, blue, white, and black-bar pixels after
resize. Both close-after-frame and close-without-sender exited cleanly under
ASan, LSan, and UBSan, in 66 ms and 54 ms including compositor command and
process teardown. The inspected CPU preview process mapped none of
`libnvidia`, `libcuda`, `libEGL`, or `libGLX`. Screenshots remain outside Git.

| Live software transfer | Presented | Corrupt | Mean decode, us | Mean present, us | Maximum present, us |
| --- | ---: | ---: | ---: | ---: | ---: |
| Release, 900 frames at 30 FPS | 900 | 0 | 1,685 | 8,361 | 15,977 |
| Sanitized, 300 frames at 30 FPS | 300 | 0 | 4,771 | 7,355 | 15,882 |

Both loopback runs reported zero loss. Release receiver RSS stayed at 51,892
KiB across 155 samples after warmup. Sanitizer RSS ranged from 98,188 to
175,640 KiB; it did not stay flat, and the process exited without leak reports.
These measurements include the software decoder and desktop presentation.

Results are in the [Release log](../benchmarks/results/milestone-six-render.log),
[sanitizer log](../benchmarks/results/milestone-six-render-sanitize.log), and
[desktop check log](../benchmarks/results/milestone-six-desktop.log).

The benchmark can run an existing sender build on a second machine:

```sh
python3 benchmarks/render.py build-release --frames 900 --fps 30 \
  --bind 192.0.2.20 --remote receiver-host --remote-build /path/to/remote/build
```

Replace the documentation placeholders with the actual receiver Tailscale IP,
sender SSH alias, and sender build path. The benchmark waits for receiver
readiness and requires every requested frame to be presented without corruption.

## Intel laptop recheck on 2026-09-24

Resumed from `09fe19e` on an Intel Iris Xe laptop. All 16 software tests passed
in Release and under ASan, LSan, and UBSan. The five optional NVIDIA tests
failed because this machine has no CUDA driver or NVIDIA GPU; this run does
not recheck the earlier NVIDIA leak findings. Keep `LARP_TEST_NVIDIA=OFF` on
this machine.

A 300-frame, 30 FPS live loopback run overlapped with the test suites and
presented 294 frames with zero corrupt frames. It reported five incomplete
frames and one entirely skipped frame. Its maximum presentation call took
57,063 microseconds. The benchmark correctly failed its all-frames check.

Repeating the same command after the suites finished presented all 300 frames
with zero loss or corruption. Mean decode time was 2,528 microseconds; mean
presentation time was 9,195 microseconds, with a maximum of 18,454 microseconds.
Receiver RSS ranged from 41,048 to 45,524 KiB over 55 samples. This short run
does not establish constant memory use or performance under contention.

```sh
cmake -S . -B build-resume -DCMAKE_BUILD_TYPE=Release -DLARP_TEST_NVIDIA=OFF
cmake --build build-resume -j2
ctest --test-dir build-resume --output-on-failure
python3 benchmarks/render.py build-resume --frames 300 --fps 30
```

Both the [concurrent-test run](../benchmarks/results/milestone-six-intel-loaded.log)
and [isolated run](../benchmarks/results/milestone-six-intel-isolated.log) are
preserved. At the time of these September measurements, no second machine was
available and network acceptance was pending. The October 3 two-machine checks
below complete milestone 6's acceptance.

## Verify recovery on one machine

The `view-recovery` CTest case runs the production software encoder, then
replays its UDP fragments into the live receiver with controlled impairments.
It uses SDL's dummy display by default. To run the same case in a desktop
window, use:

```sh
python3 tests/view_recovery.py build-resume --desktop
ASAN_OPTIONS=detect_leaks=1 python3 tests/view_recovery.py build-resume-sanitize --desktop
```

The sequence reorders and duplicates fragments, withholds one fragment past
the receiver's 50 ms deadline, loses a whole frame, corrupts encoded video,
sends a newer frame from an unrelated endpoint, and resumes after a sender
pause. Exact final counters must show four validated and presented frames,
one corrupt frame, one expiry, one missing fragment, one skipped frame, one
duplicate, one stale fragment, and one foreign packet. This also checks that
those failures do not prevent later independent H.264 frames from appearing.

On 2026-09-24, the desktop case passed under ASan, LSan, and UBSan with no
sanitizer diagnostics. The [desktop recovery log](../benchmarks/results/milestone-six-recovery-sanitize.log)
records the counters. This test verifies application recovery over loopback;
it does not simulate a bandwidth bottleneck, measure display scanout, or
replace the separate two-machine Tailscale test recorded below.

## Two-machine Tailscale acceptance on 2026-10-03

A second Linux machine sent 900 software H.264 frames at 30 FPS over Tailscale
to the local desktop receiver. All 900 frames decoded and were presented,
with zero loss, corruption, or invalid packets. Mean decode time was 2,571 us,
mean presentation time was 7,585 us, and maximum presentation time was
10,776 us. Receiver RSS remained at 40,984 KiB across 190 samples after warmup.
The [Release log](../benchmarks/results/milestone-six-tailscale-release.log)
contains the full counters and RSS measurements.

All 25 local tests also passed under ASan, LSan, and UBSan with leak detection
enabled; see the [sanitizer test log](../benchmarks/results/tailscale-local-sanitize-tests.log).
Two additional 300-frame, 30 FPS Tailscale runs with a sanitized receiver
failed the benchmark's all-frames check. The
[first run](../benchmarks/results/milestone-six-tailscale-sanitize-before.log)
presented 277 frames, with one incomplete frame and missing traffic before
the first received frame. The
[repeat](../benchmarks/results/milestone-six-tailscale-sanitize.log)
presented 294 frames and reported six entirely skipped frames. Both receivers
exited successfully, with zero corruption and no sanitizer diagnostics.
The cause of these network losses is not isolated. The successful Release
acceptance does not guarantee lossless delivery under instrumentation or
changing network conditions.

```sh
python3 benchmarks/render.py build-release --frames 900 --fps 30 \
  --bind RECEIVER_TAILSCALE_IP --remote user@SENDER_TAILSCALE_IP \
  --remote-build /path/to/sender/build-release
```

The reverse direction also passed with live PipeWire capture and adaptive
software H.264. The second machine displayed all 120 captured frames without
loss or corruption. See the [capture log](../benchmarks/results/milestone-seven-tailscale-capture.log).
These checks complete milestone 6's network acceptance. CPU presentation
durations remain distinct from display scanout and glass-to-glass latency.
