# Initial measurements

Measured on September 12, 2026, using GCC 16.2.1, CMake 4.4.3, and a Release build on the development Linux machine.

| Path | Frames | Frame bytes | Target FPS | Validated | Missing | Corrupt |
| --- | --- | --- | --- | --- | --- | --- |
| Localhost, 10 seconds | 600 | 16384 | 60 | 600 | 0 | 0 |
| Own LAN address, 5 seconds | 300 | 16384 | 60 | 300 | 0 | 0 |

The localhost run reported about 60 FPS and 8.1 Mbps while the host sent data. RSS samples after warmup were constant at 3,956 KiB for the host and 8,364 KiB for the client. The own-address run was constant at 3,952 KiB and 8,348 KiB respectively. These short samples do not prove that all workloads are leak-free.

The own-address run used `192.168.0.119`. Traffic to the machine's own address does not prove physical LAN transport. A transfer between two separate Linux machines remains unverified.

The protocol, reassembly, UDP socket, and subprocess integration tests passed with AddressSanitizer and UndefinedBehaviorSanitizer. The tests include 10,000 repeated frame assemblies and 10,000 random malformed datagrams. The integration test verifies content corruption detection, packet loss, expiry, reordered packets, duplicates, oversized datagrams, and endpoint isolation.

Tests preceded each major implementation. Initial builds failed on missing protocol, reassembly, and socket headers. The application test failed because `larp-client` did not exist. After implementation, those checks passed. These initial failures were compile or startup failures, not runtime failures in stub implementations.

The benchmark does not measure glass-to-glass latency, one-way network latency, CPU utilization, or saturation throughput. It measures application datagram throughput and process RSS at the selected workload. No optimization claim follows from this baseline.
