# Software H.264

Milestone 4 adds FFmpeg software encoding and decoding to the existing preview transport. Live screen capture, Tailscale transfer to `notept`, and decoded snapshot output are verified.

## Run the preview

Build with the `libavcodec`, `libavutil`, and `libswscale` development libraries. The host needs an FFmpeg build with the `libx264` encoder. The client uses FFmpeg's native software `h264` decoder. These libraries provide codec and pixel conversion support without introducing a media transport framework.

On the receiving Tailscale machine:

```sh
./build-release/larp-client --h264 100.118.75.118 5000 40 /tmp/decoded.ppm
```

On the development machine:

```sh
./build-release/larp-host --h264 100.118.75.118 5000 20 10
```

Select a monitor and confirm sharing in the desktop portal. The host's duration starts after selection. Allow enough time in the client command for selection, or restart the receiver if its duration expires.

The receiver saves its first decoded image as a P6 PPM file. Subsequent images contribute to statistics. This milestone does not present a live window. Rendering remains milestone 6.

To test without a desktop chooser:

```sh
./build-release/larp-host --h264-synthetic 100.118.75.118 5000 900 30
```

The first synthetic image is uniform gray. Later images contain a changing deterministic pattern. The frame count is 1 through 1,000,000,000, and FPS is 1 through 30. The capture command accepts 1 through 3,600 seconds and 1 through 30 FPS. The client retains the existing optional frame timeout argument after the output path.

Restart the receiver before each host session. The source endpoint remains pinned to the first valid transport packet.

## Codec decisions

`codec/h264.hpp` exposes caller-owned RGB and encoded buffers. FFmpeg objects stay inside the implementation and use RAII deleters. The UDP layer still treats media as opaque bytes.

The encoder uses libx264, YUV420P, CRF 23, the ultrafast preset, and the zerolatency tune. It has one thread, no B-frames, and a GOP of one. Every access unit contains SPS, PPS, and one IDR picture. Each complete frame can decode independently, including after a lost frame or a late join. This uses more bandwidth than inter-frame compression. It avoids a keyframe wait and gives us a recovery baseline before hardware encoding.

RGB input remains limited to the existing 320 by 180 capture preview. Dimensions must be at least two pixels. Odd dimensions scale down to the nearest even dimensions for YUV420P. Conversion uses libswscale with its default BT.601 coefficients and limited-range YUV. H.264 is lossy, so decoded RGB is not byte-identical to its source.

Encoding must produce exactly one packet immediately. Decoding must produce exactly one image. Unexpected delayed or extra output is an error. This deliberately supports the milestone's stream profile rather than arbitrary H.264 files. The implementation follows FFmpeg's [send and receive API](https://ffmpeg.org/doxygen/trunk/group__lavc__encdec.html).

The receiver flushes codec state before each access unit. It rejects frames lacking their own parameter sets, inter-frame pictures, multiple IDR NAL units, more than eight NAL units, dimension mismatches, non-YUV420P output, and decoder-reported corruption. No decoder retry queue exists.

## Media envelope

The 1,200-byte UDP datagram limit and version-one transport header remain unchanged. Each reassembled H.264 frame starts with this 20-byte envelope, followed by one Annex B access unit:

| Offset | Bytes | Value |
| --- | --- | --- |
| 0 | 4 | Magic `0x4c483236`, ASCII `LH26` |
| 4 | 2 | Version 1 |
| 6 | 2 | Profile 1, independent YUV420P H.264 preview |
| 8 | 4 | Decoded width, even, 2 through 320 |
| 12 | 4 | Decoded height, even, 2 through 180 |
| 16 | 4 | CRC32 |
| 20 | Remaining | Annex B bytes with SPS, PPS, and IDR |

Integers use big-endian byte order. CRC32 uses reflected polynomial `0xedb88320`, initial and final XOR `0xffffffff`. It covers the first 16 bytes followed by compressed data, excluding the checksum field. The receiver checks it before invoking FFmpeg. This detects accidental corruption and is not authentication.

The media envelope and access unit together are limited to 256 KiB. The transport's existing 4 MiB reassembly allocation remains fixed. The decoder owns one padded 256 KiB compressed input allocation and the application owns one 172,800-byte decoded RGB buffer. FFmpeg's coded-pixel limit is 61,440, allowing a 320 by 180 picture padded to 192 coded rows. Output must still match the smaller envelope dimensions before conversion.

Codec contexts, frames, and conversion contexts persist across frames. A capture size change replaces the encoder. FFmpeg owns additional bounded codec buffers, so application buffer sizes are not a total RSS guarantee. RSS measurements and sanitizers cover these allocations as well. There are no application frame queues.

## Verification

The initial codec test failed to compile before the API existed. The client test failed with the old usage message before `--h264` was implemented. A later regression test demonstrated that FFmpeg reused parameter sets from an earlier frame. Requiring SPS and PPS in each access unit made that test pass.

Tests cover 1,000 encode cycles, skipped frames, late joining, malformed compressed data, checksum errors, dimension limits, odd dimensions, and insufficient buffers. The subprocess checks verify decoded pixels and exercise packet reordering, an omitted fragment, corruption, and subsequent recovery. When the `ffmpeg` executable is installed, the loss test also decodes an emitted access unit through its command-line interface.

Run the RSS sampler around either executable:

```sh
python3 benchmarks/rss.py ./build-release/larp-host --h264-synthetic 100.118.75.118 5000 900 30
```

It reports minimum, maximum, first, and last RSS every 200 ms after a two-second warmup, retaining only aggregate values. Encoder and decoder statistics report mean local processing time in microseconds. These values include color conversion and media validation work, exclude codec initialization, and do not measure glass-to-glass latency.

All 13 tests pass in Release and with AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer. All 11 capture-disabled tests also pass on `notept`. Standalone FFmpeg successfully decoded an emitted access unit. A decoded remote desktop snapshot was retrieved and visually inspected. Screen images remain outside the repository.

The initial 900-frame Tailscale run at 30 FPS delivered 898 frames. Two expired; subsequent frames decoded with no corruption. Host RSS remained 25,980 KiB and receiver RSS remained 30,608 KiB after warmup. Mean encode time was 1,855 microseconds; mean decode time was 3,017 microseconds. This run preceded the stricter SPS/PPS validation.

The final capture-only run sent 300 frames at 10 FPS. The receiver decoded 292; eight expired with 49 fragments counted missing at expiry. Later packets arrived stale. There were no corrupt images, malformed captures, or capture timeouts. Mean encode time was 1,355 microseconds and mean decode time was 2,616 microseconds. Payload bitrate was approximately 1.39 Mbps. This is a lossy Tailscale result, not a claim of lossless delivery.

A separate sanitizer capture run sent 200 frames and exited without sanitizer diagnostics. It overlapped a 1,800-frame synthetic stream, increasing packet loss. The receiver decoded 149 capture frames and 1,583 synthetic frames, with no corrupt images. These overlap results are retained as loss-recovery evidence, not isolated throughput measurements.

## Memory investigation

The 1,800-frame sanitizer host grew from 49,996 to 107,544 KiB RSS. The shorter capture run also grew. Investigation compared identical 15,000-frame encode/decode workloads with normal Release allocation, default ASan quarantine, and ASan quarantine disabled. Both sanitizer runs retained leak detection and completed without diagnostics.

```sh
python3 benchmarks/rss.py ./build-release/larp-codec-bench 15000
ASAN_OPTIONS=detect_leaks=1 python3 benchmarks/rss.py ./build-sanitize/larp-codec-bench 15000
ASAN_OPTIONS=detect_leaks=1:quarantine_size_mb=0:thread_local_quarantine_size_kb=0 python3 benchmarks/rss.py ./build-sanitize/larp-codec-bench 15000
```

| Build | Minimum RSS, KiB | Maximum RSS, KiB | Final RSS, KiB |
| --- | ---: | ---: | ---: |
| Release | 30,144 | 30,356 | 30,356 |
| ASan, default quarantine | 146,336 | 329,360 | 311,772 |
| ASan, quarantine disabled | 48,032 | 50,360 | 48,032 |

Each run encoded and decoded 414,368,376 bytes of changing compressed image data. Default ASan RSS leveled off rather than following total bytes processed. With quarantine disabled, RSS remained within about 2.3 MiB. The evidence attributes the large sanitizer growth to retained freed allocations, not live frame accumulation. Disabling quarantine is a diagnostic comparison only; normal sanitizer tests retain it.

FFmpeg allocates encoded packet storage per frame. L.A.R.P. releases each packet before processing the next frame. Removing those library allocations would require a different ownership path and is not justified by the Release measurements. Application image buffers remain reusable and fixed in size.

Capture-process RSS sampling includes chooser time and codec initialization, so its first-to-last values do not represent steady-state growth. The capture-only host peaked at 61,880 KiB and its receiver at 36,404 KiB. The codec comparison above isolates the new component from portal and PipeWire initialization.

The unpaced Release codec run averaged 797 microseconds encoding and 802 microseconds decoding. Other verification jobs ran concurrently, so these measurements are a baseline rather than an isolated CPU comparison. No allocation or codec optimization was made in response to sanitizer quarantine growth.

See [the recorded test summaries](../benchmarks/results/milestone-four-h264.log) for commands and final counters.
