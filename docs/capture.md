# PipeWire screen capture

Milestone three adds a Linux Wayland capture implementation and a raw preview path. Live capture and two-machine preview delivery over Tailscale passed. The capture host also passed a live sanitizer run after a reproduced context-lifecycle leak was fixed.

## Build

Capture requires the PipeWire and GIO Unix development libraries, discovered through `pkg-config` as `libpipewire-0.3` and `gio-unix-2.0`. PipeWire supplies the video stream. GIO handles portal D-Bus requests, signals, and Unix file-descriptor transfer. Neither library appears in the transport or media interfaces.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure
```

To build only the transport and receiver, disable capture:

```sh
cmake -S . -B build-transport -DCMAKE_BUILD_TYPE=Release -DLARP_CAPTURE=OFF
cmake --build build-transport -j2
```

## Capture a monitor

Run inside the graphical Wayland session:

```sh
./build-release/larp-capture-probe 5 /tmp/larp-capture.ppm
```

Select a monitor and confirm sharing in the desktop portal's chooser. Each run starts a new session. Selection times out after 60 seconds. No permission or restore token is persisted. Cancelling, denying access, or closing the session produces an error and releases the application's resources.

The probe counts captured previews for five seconds after selection and saves the first preview when an output path is supplied. Changed counts differences between successive pixel fingerprints. A run that captures no frames fails. Screen content can be static, so a frame count alone is not proof of changing pixels.

## Send a raw preview over Tailscale

On the receiving machine, start a client with its Tailscale address:

```sh
./build-release/larp-client --raw 100.118.75.118 5000 120 /tmp/larp-preview.ppm
```

On the capture machine, start the host and select a monitor:

```sh
./build-release/larp-host --capture 100.118.75.118 5000 30 5
```

The host runs for 30 seconds after selection and sends at most five previews per second. The client validates the raw-frame header and CRC, then saves the first valid preview as a binary PPM image. Open that image to inspect the captured content. The client does not render a live video window.

The client lifetime includes time spent waiting for host selection. Allow enough time for that selection plus the transfer. Use a fresh client for each host session because endpoint pinning still applies.

Capture host syntax is `larp-host --capture IPv4 PORT SECONDS FPS`. Seconds accepts 1 through 3,600 and FPS accepts 1 through 30. The raw client accepts an optional frame timeout after the output path, from 1 through 1,000 ms. Synthetic host and client commands remain available.

## Capture ownership and limits

`ScreenCapture::next` accepts caller-owned RGB storage and a bounded wait, then returns frame dimensions and a local receipt timestamp. The buffer must have at least 172,800 bytes. Capture does not retain the storage after `next` returns, including on failure. Callers must use each capture instance on one thread.

The implementation drives PipeWire callbacks on the calling thread. It drains available buffers, discards older ones, converts the newest one directly into the caller's storage, and requeues the PipeWire buffer before returning. There is no worker thread, mutex, mailbox, or application frame queue.

The host keeps one 172,820-byte raw-frame buffer and one 1,200-byte packet buffer. The receiver retains its existing 4 MiB reassembly buffer. Previews fit within 320×180, preserve aspect ratio up to integer rounding, and never upscale. Conversion uses nearest-neighbor sampling and produces packed RGB24 without a native-resolution intermediate copy.

PipeWire negotiates two through four CPU-mappable source buffers. Sources must be packed RGBx, RGBA, BGRx, or BGRA, no larger than 4,096×2,160. The adapter rejects more than four buffers or an individual buffer larger than 64 MiB. Thus source mappings have a separate upper bound of 256 MiB. Actual mapping size depends on the selected source and compositor; GIO and PipeWire also have internal bookkeeping. Application buffer sizes are not a claim about total process RSS.

The adapter validates positive stride, chunk offsets, available byte counts, corruption flags, and crop bounds before reading pixels. Unsupported layouts and malformed buffers cannot write outside the preview buffer. DMA-BUF-only and modifier layouts are unsupported in this milestone. Alpha and cursor metadata are not transmitted; the portal's default hidden cursor mode is used.

The timestamp records when the application handles the source buffer, before conversion. It is not a compositor presentation timestamp or a glass-to-glass latency measurement. Slow sending can leave capture buffers waiting until the next pull; older available buffers are discarded then.

## Raw-frame envelope

The existing version-one UDP packet header is unchanged. Its reassembled payload in explicit raw mode begins with this big-endian envelope:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic `0x4c524742`, ASCII `LRGB` |
| 4 | 2 | Raw-frame version, 1 |
| 6 | 2 | Pixel format, 1 for packed RGB24 |
| 8 | 4 | Width |
| 12 | 4 | Height |
| 16 | 4 | CRC-32 |
| 20 | width × height × 3 | Pixels, row-major RGB bytes |

CRC-32 uses reflected polynomial `0xedb88320`, initial value `0xffffffff`, and final XOR `0xffffffff`. It covers header bytes 0 through 15 followed by all pixels; the checksum field itself is excluded. This detects accidental corruption, not malicious modification.

The decoder accepts nonzero dimensions at most 4,096×2,160 and requires the exact pixel byte count. The enclosing transport still enforces its 4 MiB limit. No resolution-dependent allocation occurs when parsing. Raw mode is selected explicitly; the receiver does not guess the content type from synthetic bytes.

## Design comparison

The architect review compared a pull session with leased buffers against a callback thread with a three-slot mailbox. The pull design was the base. Its single-threaded event loop matches the existing synchronous host and avoids shutdown synchronization. The callback design contributed explicit buffer validation, newest-frame selection, and session-closure handling.

The final interface uses caller-owned preview storage instead of either proposed buffer pool. This removes lease lifetime rules and native-frame copies while preserving a portable capture boundary. The accepted cost is low-resolution CPU conversion and a pause in event processing while the host sends. Hardware-native capture and encoding remain later milestones.

## Verification

Pixel and raw-envelope tests preceded their implementations and initially failed because their headers did not exist. The raw-client test initially failed on the unsupported `--raw` command. The capture-startup test initially failed because the host lacked capture mode. All now pass.

The tests cover padded rows, channel order, downscaling, short buffers, invalid geometry, header validation, an independently computed Python CRC, exact PPM bytes, corrupt pixels, and startup with an unavailable session bus. All ten tests passed in Release and with address, leak, and undefined-behavior sanitizers. A capture-disabled build passed its eight tests locally and on `notept`.

The real portal handshake was verified separately. Initial probes timed out while the user was away. After monitor selection, a five-second probe produced 211 previews with no malformed buffers. Saved previews from separate runs were inspected and showed the selected desktop with different content.

A subsequent live sanitizer run exposed a 3,677-byte shutdown leak. The `pipewire-context` regression reproduced it without the portal or any video frames: five context create/destroy cycles leaked 18,385 bytes in 170 allocations. Disabling the optional real-time scheduling module for this caller-thread capture context made the same test pass. The fix sets the per-context `module.rt=false` property; it does not change system PipeWire configuration or suppress LeakSanitizer. This milestone does not request real-time thread scheduling.

On September 13, 2026, the sanitized capture host sent 100 raw previews at a target of five FPS over 20 seconds to `notept` at `100.118.75.118`. The remote Release client validated all 100 frames and received all 14,900 packets, with zero dropped frames or corruption. The saved 320×180 PPM image was retrieved and visually inspected. Media traveled over Tailscale. No direct LAN test was used.

The host emitted no sanitizer diagnostics. Host RSS after a two-second capture warmup ranged from 68,396 to 68,400 KiB across 90 samples at 200 ms intervals. First RSS was 68,396 KiB and last RSS was 68,400 KiB. This one-page variation is a measurement of an instrumented run, not a Release-memory baseline or a general leak-freedom claim. Receiver RSS was not sampled in this run.

The receiver ran the capture-disabled build of `a105aa3`. The host included the context fix committed as `cf8cd28`. See [the capture-transfer log](../benchmarks/results/milestone-three-capture.log) for counters and memory observations. Snapshot files remain outside the repository because they contain desktop content.

Milestone three is complete. Software video encoding begins in milestone four.

API references: [ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html) and [PipeWire video input example](https://docs.pipewire.org/video-play_8c-example.html).
