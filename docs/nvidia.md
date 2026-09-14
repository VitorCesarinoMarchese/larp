# NVIDIA H.264 backends

Milestone 5 implements NVENC encoding and NVDEC decoding. Functional tests and Tailscale transfers pass, including a captured desktop preview. Acceptance remains blocked by NVIDIA driver leaks reproduced outside L.A.R.P. The README checklist stays unchecked to preserve the project's no-leaks requirement.

## Build and select a codec

The normal build still needs only the FFmpeg development libraries. NVIDIA operation additionally requires a compatible GPU, driver, and an FFmpeg build with NVENC and CUDA hardware decoding. L.A.R.P. does not require CUDA SDK headers or link directly to CUDA. FFmpeg loads the driver libraries at runtime.

Software remains the default. Append `--codec nvidia` to an H.264 host or client command to request hardware explicitly. Initialization errors stop the requested operation rather than silently selecting software. Use `--codec software` for the software fallback.

On the Intel receiver, `notept`:

```sh
./build-release/larp-client --h264 100.118.75.118 5000 60 /tmp/nvenc-preview.ppm --codec software
```

On the NVIDIA development machine:

```sh
./build-release/larp-host --h264 100.118.75.118 5000 30 10 --codec nvidia
```

Confirm that the receiver prints `Listening` before starting capture. Select a monitor in the desktop portal. The host prints `Encoder: h264_nvenc` after initialization. To avoid the chooser, use `--h264-synthetic` with a frame count in place of capture duration.

To test NVDEC, reverse the direction. Start this client on the development machine:

```sh
./build-release/larp-client --h264 100.127.119.2 5000 20 /tmp/nvdec-preview.ppm --codec nvidia
```

Then run this host on `notept`:

```sh
./build-release/larp-host --h264-synthetic 100.127.119.2 5000 300 30 --codec software
```

The client prints `Decoder: h264+nvdec`. Its decoder requires CUDA output frames before downloading them to RGB. Restart the receiver between host sessions. The optional frame timeout still precedes the final `--codec` option. Raw and byte-pattern modes do not accept codec selection.

## Ownership and latency decisions

`CodecBackend` selects software or NVIDIA inside the existing H.264 classes. FFmpeg objects remain private. Packetization, reassembly, timestamps, and the media envelope are unchanged. Both encoders emit independent Annex B access units with SPS, PPS, and one IDR picture.

NVENC uses `h264_nvenc`, preset `p1`, tuning `ull`, baseline profile, constant QP 23, GOP 1, no B-frames, no lookahead, two surfaces, and zero output delay. Encoding must produce one packet immediately, within the existing 256 KiB limit. FFmpeg exposes these controls through its [NVIDIA integration](https://docs.nvidia.com/video-technologies/video-codec-sdk/13.1/ffmpeg-with-nvidia-gpu/index.html).

NVDEC uses the native FFmpeg H.264 parser and CUDA hardware acceleration. A per-instance format callback accepts only CUDA output with NV12 software storage. It checks coded dimensions before initializing an eight-surface frame pool. Streams requiring a larger pool or another pixel format are rejected. Device and pool references belong to the RAII codec context. The callback follows FFmpeg's [hardware decoding API](https://github.com/FFmpeg/FFmpeg/blob/master/doc/examples/hw_decode.c).

Capture still produces a CPU RGB preview of at most 320 by 180 pixels. NVENC therefore includes an upload. NVDEC downloads into one reusable NV12 frame for RGB conversion and snapshot output. There is no GPU rendering or zero-copy path yet. Transfer costs are included in the processing measurements. Hardware-supported minimum dimensions remain driver-dependent; 320 by 180 and 318 by 178 are tested.

The client resets decoder state after a rejected compressed frame. `avcodec_flush_buffers` alone did not recover reliably after an unsupported H.264 pixel format. The reset replaces the codec context while retaining its CUDA device reference, then accepts the next independent frame. There is no retry queue or software substitution.

## Verification

Enable hardware tests explicitly on an NVIDIA machine:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DLARP_TEST_NVIDIA=ON
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure
```

All 19 Release tests pass on the GTX 1660 Ti. All 12 capture-disabled tests pass on the Intel machine. Explicit NVIDIA requests there exit with a missing `libcuda.so.1` error; software remains usable.

The codec-selection test first failed because the backend API did not exist. The subprocess test then failed because the old CLI rejected `--codec`. The unsupported-profile regression first terminated the client, then exposed a poisoned decoder state. It now rejects the frame and verifies recovery on the next valid image.

Tests cover NVENC to software, software to NVDEC, and NVENC to NVDEC. They include 1,000 encode cycles, skipped frames, late joining, color values, odd dimensions, malformed envelopes, unsupported pixel formats, packet reordering, loss, and corruption. The standalone `ffmpeg` executable also decodes a generated NVENC access unit when available.

Verified Tailscale transfers:

| Transfer | Sent | Decoded | Corrupt | Mean encode, us | Mean decode, us |
| --- | ---: | ---: | ---: | ---: | ---: |
| NVENC synthetic to software on `notept` | 300 | 300 | 0 | 1,203 | 4,694 |
| Software on `notept` to NVDEC | 300 | 300 | 0 | 2,764 | 1,411 |
| NVENC screen capture to software on `notept` | 298 | 298 | 0 | 820 | 2,389 |

The capture run lasted 30 seconds at a requested 10 FPS. It sent 3,576 packets with no missing or stale packets, malformed captures, or capture timeouts. The decoded desktop snapshot was retrieved and visually inspected. Screen images remain outside the repository. An earlier capture attempt had no listening receiver because SSH authentication blocked startup; it is not counted as transfer evidence.

## Sustained memory measurements

```sh
python3 benchmarks/rss.py ./build-release/larp-codec-bench 15000 software software
python3 benchmarks/rss.py --gpu ./build-release/larp-codec-bench 15000 nvidia nvidia
```

`--gpu` samples per-process GPU memory through `nvidia-smi` once per second. RSS is sampled every 200 ms after two seconds. Both samplers retain aggregates rather than a growing sample list.

| 15,000-frame run | RSS minimum, KiB | RSS maximum, KiB | GPU memory, MiB | Mean encode, us | Mean decode, us |
| --- | ---: | ---: | ---: | ---: | ---: |
| Software | 28,196 | 28,408 | Not used | 843 | 829 |
| NVIDIA | 371,348 | 371,348 | 159 | 694 | 1,090 |

Hardware memory stayed flat after warmup, and the benchmark process disappeared from GPU memory accounting after exit. This does not establish leak-free encoder lifecycle behavior. The separate leak probes below fail.

These encoder settings are not matched for visual quality or bitrate. NVENC emitted 925,798,931 bytes, including envelopes, compared with 414,368,376 bytes from libx264 for the same inputs. Hardware is not faster across the complete CPU-preview round trip in this measurement. The small resolution, different compression settings, upload, and download limit the comparison. No performance tuning was applied to hide those costs.

## Driver leak blocks acceptance

The [follow-up investigation](nvidia-leak-investigation.md) isolates the CUDA
baseline to an application-profile name collision and the additional growth
to encoder-library load/unload cycles. It includes reproducible comparisons.

The installed NVIDIA driver is 615.71.09. Unmodified ASan settings prevent CUDA initialization with `CUDA_ERROR_OUT_OF_MEMORY`, despite available VRAM. Disabling protection of ASan's address-space gap allows initialization. This CUDA compatibility setting is described in the [sanitizer maintainers' issue](https://github.com/google/sanitizers/issues/629).

All 14 software tests pass with normal ASan, LeakSanitizer, and UBSan settings:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DLARP_SANITIZE=ON -DLARP_TEST_NVIDIA=ON
cmake --build build-sanitize -j2
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitize -LE nvidia --output-on-failure
```

This hardware run still fails all five tests on LeakSanitizer reports. No address or undefined-behavior error was reported in the run:

```sh
ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0 ctest --test-dir build-sanitize -L nvidia --output-on-failure
```

Two diagnostic executables isolate dependency allocations:

```sh
ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0 ./build-sanitize/larp-cuda-init
ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0 ./build-sanitize/larp-nvenc-sessions 1
ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0 ./build-sanitize/larp-nvenc-sessions 10
```

`larp-cuda-init` links no FFmpeg or L.A.R.P. codec code. It loads `libcuda.so.1`, calls `cuInit`, and closes the library through RAII. LeakSanitizer reports 183 bytes in four driver allocations.

`larp-nvenc-sessions` opens and frees FFmpeg NVENC contexts without allocating application frames, sending packets, or using L.A.R.P.'s codec implementation. One empty session reports 279 bytes in six allocations. Ten report 1,143 bytes in 24 allocations. The difference is 96 bytes per empty encoder lifecycle, beyond the CUDA initialization baseline. Stacks include the driver's condition-variable path. NVIDIA's forum records [similar encoder-session leaks](https://forums.developer.nvidia.com/t/memory-leak-in-nvencopenencodesessionex/254738), but the local probes are the evidence for this installed driver.

An independent FFmpeg CLI NVDEC run, with the ASan runtime preloaded, also reports 231 bytes in five allocations. Reproduction commands are in [the measurement log](../benchmarks/results/milestone-five-nvidia.log).

No leak suppression, disabled leak detection, or driver-unload workaround was added. Sustained streaming has stable measured memory, but repeated encoder-library load/unload cycles still violate the no-leaks requirement. Completing milestone 5 requires a driver and library combination that passes these probes and the hardware sanitizer suite. The system driver has not been changed, and milestone 6 has not started.
