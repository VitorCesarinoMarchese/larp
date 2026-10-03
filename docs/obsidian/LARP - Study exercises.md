---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-03
---

# Study exercises

Use these exercises to connect the explanations to observable behavior. Commands run from `../..`. The expected results describe the tests and protocol; they are not new benchmark measurements performed while writing these notes.

## Build an isolated study copy of the binaries

This configuration needs a C++23 compiler, CMake 3.25 or newer, FFmpeg development libraries, libx264 support, and Python for subprocess tests. It omits desktop capture, live rendering, and GPU tests so you can study transport and software media without opening a chooser or window.

```sh
cmake -S . -B /tmp/larp-study-build -DCMAKE_BUILD_TYPE=Release -DLARP_CAPTURE=OFF -DLARP_RENDER=OFF -DLARP_TEST_NVIDIA=OFF
cmake --build /tmp/larp-study-build -j2
ctest --test-dir /tmp/larp-study-build --output-on-failure
```

A successful suite reports passing configured tests. The number depends on build options and the checkout. FFmpeg remains required even when capture and rendering are disabled.

## Predict and inspect a synthetic transfer

Start a fresh receiver in one terminal:

```sh
/tmp/larp-study-build/larp-client 127.0.0.1 5000 12
```

After it prints `Listening`, start the sender in another terminal:

```sh
/tmp/larp-study-build/larp-host 127.0.0.1 5000 600 16384 60
```

Each frame needs `ceil(16384 / 1166) = 15` packets. The sender therefore reports 9,000 packets for 600 frames. On an unloaded loopback connection, expect `Validated: 600` and `Corrupt: 0`. Delivery can still depend on scheduling and socket pressure.

Explain why final interval FPS may be zero even after all frames arrive. Then find the code that generates `(frame_id + offset) mod 256` and the code that checks it.

Read [LARP - UDP protocol and reassembly](LARP%20-%20UDP%20protocol%20and%20reassembly.md) if the packet count or counters are unclear.

## Decode video without desktop capture

Use a fresh receiver:

```sh
/tmp/larp-study-build/larp-client --h264 127.0.0.1 5000 12 /tmp/larp-study.ppm
```

Start this host after `Listening`:

```sh
/tmp/larp-study-build/larp-host --h264-synthetic 127.0.0.1 5000 300 30
```

The receiver saves the first valid decoded picture as a 320 by 180 P6 PPM snapshot. It continues decoding and counting later frames without replacing that file.

Follow one picture through `H264Encoder::encode`, `send_frame`, `Reassembler::accept`, and `H264Decoder::decode`. At each boundary, identify the buffer owner and when the bytes may be overwritten.

## Read recovery tests as executable scenarios

Run the focused tests:

```sh
ctest --test-dir /tmp/larp-study-build -R '^(protocol|reassembly|loss|h264|h264-loss|h264-client)$' --output-on-failure
```

Open `tests/loss_test.cpp` and `tests/h264_loss.py`. Identify one example each of packet reorder, an omitted fragment, stale arrival, corrupted media, and subsequent recovery.

Predict which scenario increases `Invalid`, `Dropped`, `Skipped`, or `Corrupt`. Check your prediction against the assertions. Explain why transport completion and successful media decoding need separate counters.

## Observe adaptation

Start a fresh H.264 receiver with enough time for the stream:

```sh
/tmp/larp-study-build/larp-client --h264 127.0.0.1 5000 12 /tmp/larp-study-adaptive.ppm
```

Start an adaptive host:

```sh
/tmp/larp-study-build/larp-host --h264-synthetic 127.0.0.1 5000 300 30 --adaptive 2000
```

Look for sender reports containing `Feedback`, `RTT us`, `Target kbps`, and feedback age. Then run the controlled capacity benchmark:

```sh
python3 benchmarks/bandwidth.py /tmp/larp-study-build --output /tmp/larp-study-bandwidth.json
```

Inspect the JSON samples and process logs. Find the capacity reduction and recovery, then compare target bitrate with delivered wire bitrate. They need not match because target controls encoded video while wire rate includes headers, probes, and actual encoded output.

The benchmark should pass its startup, settling, recovery, queue, and decoding checks. Its default scenario sends 480 frames through a modeled link. It does not test a physical Tailscale route.

Read [LARP - Adaptive bitrate and pacing](LARP%20-%20Adaptive%20bitrate%20and%20pacing.md) alongside `tests/rate_control_test.cpp`. Locate how tests supply synthetic time instead of sleeping.

## Verify authenticated recovery without unlocking the screen

The study build above has rendering and capture disabled. Run the production session check:

```sh
ctest --test-dir /tmp/larp-study-build -R secure-sessions --output-on-failure
```

It uses decoded snapshots in this build. Rendering-enabled builds use SDL dummy displays. The fixture checks tampering, duplicate and retired-session replay, wrong keys and peers, key-file validation, sender and receiver restarts, and a complete bidirectional outage. It also checks that the synthetic-byte sender services heartbeats between one-second frame intervals.

Read `transport/session.cpp`, `tests/secure_sessions.py`, and [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md). Locate why the replay window advances only after authentication, and why fresh feedback counters are accepted after a session change but rejected within the previous generation.

## Questions to answer without the notes

- Why can a packet for an expired frame never revive it?
- Why does a whole missing frame contribute to frame loss but not observed packet loss?
- Why must every H.264 frame carry parameter sets even though the decoder flushes state?
- Why does CRC32 not authenticate a source?
- Why does the renderer copy a borrowed pixel span before returning?
- Why can a 4 MiB transport buffer coexist with much larger capture-process RSS?
- Why does disabling capture still leave FFmpeg as a dependency?
- Why is application RTT different from one-way network delay and display latency?
- Why can flat Release RSS coexist with failing NVIDIA lifecycle leak tests?
- Why would uncapped pacing at one FPS exceed the default frame expiry?
- Why is authenticated replay rejection different from media duplicate detection?
- Why must restart recovery clear frame-ID and decoder state?
- Why does a compromised shared key expose recorded sessions?

For a deeper exercise, propose a two-frame reassembly window. Identify the additional storage, replacement rules, deadlines, and returned-span lifetimes before considering implementation.

## Inspect the original decision record

```sh
git log --oneline --reverse
git show cf8cd28 -- capture/linux/context.hpp
git show ec33113 -- benchmarks/codec.cpp
git show 09fe19e -- render/window_sdl.cpp docs/rendering.md
git status --short
```

Inspect implementation commit `a8fd3be` and compare its adaptive and session changes with the earlier history. Explain which behavior was verified locally, which required a second machine, and which milestones remain blocked.

Sources: [CMake targets](../../CMakeLists.txt), [loss tests](../../tests/loss_test.cpp), [H.264 recovery scenarios](../../tests/h264_loss.py), [controller tests](../../tests/rate_control_test.cpp), [bandwidth benchmark](../../benchmarks/bandwidth.py).

Return to [LARP - Study map](LARP%20-%20Study%20map.md).
