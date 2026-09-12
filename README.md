# L.A.R.P.

Low-latency Adaptive Remote Protocol. The first implementation transports synthetic frames over UDP on Linux and validates their contents at the receiver.

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

For two Linux machines, build on both machines. Bind the client to its LAN address or `0.0.0.0`, and give the host the client's LAN IPv4 address. Allow inbound UDP port 5000 on the client. Use the same frame count, byte count, and FPS as above, then compare the host's sent count with the client's final validated count.

Restart the client before each host session. The client pins the first valid source IP and UDP port. This prototype has no session negotiation.

## Measure a transfer

```sh
python3 benchmarks/transfer.py build-release
python3 benchmarks/transfer.py build-release 127.0.0.1 30 65536 60
```

The script prints receiver statistics and samples each process's RSS every 100 ms after a one-second warmup. It keeps at most one hour of samples. See [measurement results](docs/measurements.md) for the initial baseline and its limits.

See the [protocol reference](docs/protocol.md) for command limits and statistic definitions, and [design decisions](docs/design.md) for buffering and memory tradeoffs.
