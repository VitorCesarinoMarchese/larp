# L.A.R.P.

Low-latency Adaptive Remote Protocol. Milestones 1 and 2 transport synthetic frames over UDP on Linux, validate their contents, and report loss and stale-frame drops.

## Build and test

Use a C++23 compiler and CMake 3.25 or newer. Python 3 enables the subprocess integration test and RSS benchmark. No media libraries are needed yet.

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

Restart the client before each host session. The client pins the first valid source IP and UDP port. This prototype has no session negotiation.

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
./build-release/larp-client 100.118.75.118 5000 12 50
```

Run the controlled impairment check against an existing remote build:

```sh
python3 tests/tailscale_impairment.py notept /tmp/larp-m2.JcrIW0/build 100.118.75.118
```

The check sends reordered and duplicate packets, omits a packet and a whole frame, waits for expiry, and then sends a late packet followed by a usable frame. It expects three validated frames, one expiry, one replacement, one skipped frame, 20% observed packet loss, and 50% frame loss. Additional network loss can fail this acceptance check. Unit tests verify exact deadlines without wall-clock sleeps.

Observed packet loss excludes entirely unseen frames because their packet counts are unknown. Read Frame loss and Skipped alongside it. See [milestone-two evidence](docs/milestone-two.md) for the tests and memory measurements.
