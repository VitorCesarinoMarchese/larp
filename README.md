# L.A.R.P.

Low-latency Adaptive Remote Protocol. Transports synthetic frames and PipeWire screen previews over UDP on Linux, encodes and decodes H.264 in software, presents live video, and adapts bitrate using receiver feedback.

## Milestone status

- [x] Milestone 1: synthetic frame transport over UDP
- [x] Milestone 2: packet loss and stale-frame handling
- [x] Milestone 3: Linux PipeWire screen capture
- [x] Milestone 4: H.264 software encoding and decoding
- [ ] Milestone 5: NVENC and NVDEC acceleration
- [x] Milestone 6: low-latency Linux rendering
- [x] Milestone 7: network telemetry and adaptive bitrate
- [ ] Milestone 8: Windows capture abstraction
- [ ] Milestone 9: Windows hardware encoding and decoding
- [ ] Milestone 10: Windows rendering and Winsock transport
- [ ] Milestone 11: Linux and Windows interoperability
- [x] Milestone 12: Tailscale session security and network recovery

Milestone 5's NVIDIA backends are implemented and pass functional tests. Acceptance remains blocked by driver leaks reproduced outside L.A.R.P. See [the NVIDIA guide](docs/nvidia.md) for evidence and reproduction commands.

Milestone 6's live Linux preview passes local and two-machine Tailscale
verification with the software codec. See
[the rendering guide](docs/rendering.md) for commands and measurements.

Milestone 7's software adaptive bitrate and receiver feedback pass local and
two-machine Tailscale loss, delay, outage, and live-capture checks. See
[the adaptive streaming guide](docs/adaptive.md).

Windows milestones 8 through 11 remain unimplemented and are deferred until a
Windows test machine is available. Milestone 12 keeps Tailscale for internet
transport and adds authenticated sessions and recovery. See
[the session guide](docs/sessions.md). Direct NAT traversal and a separate relay
are outside its current scope.

Milestone 12 passes local negative and recovery tests, sanitizer checks on both
Linux nodes, and encrypted Tailscale streaming with sender and receiver
restarts in both directions. See [the acceptance results](docs/session-verification.md).

Each completed milestone has implementation notes and measurements in [`docs/`](docs/).
The [Obsidian study notes](docs/obsidian/README.md) explain the architecture,
protocol, recovery, and acceptance evidence in reading order.

## Build and test

Use a C++23 compiler and CMake 3.25 or newer. Python 3 enables the subprocess integration tests and RSS benchmarks. All builds require libsodium 1.0.18 or newer and the FFmpeg `libavcodec`, `libavutil`, and `libswscale` development libraries. Software encoding requires FFmpeg with `libx264`. Capture-enabled builds also require the PipeWire and GIO Unix development libraries. Use `-DLARP_CAPTURE=OFF` to build the transport, software codec, and receiver without desktop capture.

Live preview builds require SDL3 3.2 or newer. Use `-DLARP_RENDER=OFF` to omit
SDL3 and the window. Live windows use X11 or Xwayland; snapshot modes remain
usable without a display.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j2
ctest --test-dir build-release --output-on-failure
```

Run the same tests with AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DLARP_SANITIZE=ON
cmake --build build-sanitize -j2
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-sanitize --output-on-failure
```

## Run a transfer

Machine names and non-loopback IP addresses in documentation and saved logs are
placeholders. Replace `receiver-host` with your own SSH alias and example IPs
with the intended machine's address. The examples use reserved documentation
address ranges, including where they describe Tailscale tests.

Start the client in one terminal:

```sh
./build-release/larp-client 127.0.0.1 5000 12
```

Start the host in another terminal:

```sh
./build-release/larp-host 127.0.0.1 5000 600 16384 60
```

The host sends 600 frames of 16,384 bytes at 60 FPS. The client runs for 12 seconds. Its final line should include `Validated: 600` and `Corrupt: 0` on an unloaded localhost connection. The final FPS may be zero because the host has already stopped.

For two Linux machines, use Tailscale. Build on both machines, bind the client to its Tailscale IPv4 address, and give the host that address. Use the same frame count, byte count, and FPS as above, then compare the host's sent count with the client's final validated count. Network acceptance tests currently use only Tailscale. Local automated tests use loopback.

These commands use the original plaintext mode, which pins the first valid
source IP and UDP port and requires a new client for each host session.
For authenticated encryption and automatic restart recovery, use
[`--key-file` and `--peer`](docs/sessions.md).

## Measure a transfer

```sh
python3 benchmarks/transfer.py build-release
python3 benchmarks/transfer.py build-release 127.0.0.1 30 65536 60
```

The script prints receiver statistics and samples each process's RSS every 100 ms after a one-second warmup. It keeps at most one hour of samples. See [measurement results](docs/measurements.md) for the initial baseline and its limits.

See the [protocol reference](docs/protocol.md) for command limits and statistic definitions, and [design decisions](docs/design.md) for buffering and memory tradeoffs.

## Check loss and stale-frame recovery

To use a 50 ms expiry deadline, pass the optional fifth client argument:

```sh
./build-release/larp-client 192.0.2.10 5000 12 50
```

Run the controlled impairment check against an existing remote build:

```sh
python3 tests/tailscale_impairment.py receiver-host /tmp/larp-m2.JcrIW0/build 192.0.2.10
```

The check sends reordered and duplicate packets, omits a packet and a whole frame, waits for expiry, and then sends a late packet followed by a usable frame. It expects three validated frames, one expiry, one replacement, one skipped frame, 20% observed packet loss, and 50% frame loss. Additional network loss can fail this acceptance check. Unit tests verify exact deadlines without wall-clock sleeps.

Observed packet loss excludes entirely unseen frames because their packet counts are unknown. Read Frame loss and Skipped alongside it. See [milestone-two evidence](docs/milestone-two.md) for the tests and memory measurements.

## PipeWire screen capture

The milestone-three capture and raw-preview commands are described in [the capture guide](docs/capture.md). Use `-DLARP_CAPTURE=OFF` to omit desktop capture. Live capture and Tailscale preview delivery are verified; the guide records the tests, the leak fix, and memory measurements.

## Software H.264 preview

The `--h264` host and client modes encode and decode screen previews with FFmpeg. The client saves a decoded snapshot and reports processing times. See [the software H.264 guide](docs/software-h264.md) for commands, memory limits, recovery behavior, and verified Tailscale results.

## Adaptive software streaming

Add `--adaptive 2000` to an H.264 host command to start at 2,000 kbps. The
sender reports receiver loss, throughput, application RTT, and bitrate changes.
The existing H.264 clients reply automatically. Adaptive mode currently requires
the software encoder. See [usage and local verification](docs/adaptive.md), and
[bandwidth changes and packet pacing](docs/bandwidth.md).
