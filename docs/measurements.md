# Initial measurements

Measured on September 12, 2026, using GCC 16.2.1, CMake 4.4.3, and a Release build on the development Linux machine.

| Path | Frames | Frame bytes | Target FPS | Validated | Missing | Corrupt |
| --- | --- | --- | --- | --- | --- | --- |
| Localhost, 10 seconds | 600 | 16384 | 60 | 600 | 0 | 0 |
| Own LAN address, 5 seconds | 300 | 16384 | 60 | 300 | 0 | 0 |

The localhost run reported about 60 FPS and 8.1 Mbps while the host sent data. RSS samples after warmup were constant at 3,956 KiB for the host and 8,364 KiB for the client. The own-address run was constant at 3,952 KiB and 8,348 KiB respectively. These short samples do not prove that all workloads are leak-free.

The own-address run used `198.51.100.10`. Traffic to the machine's own address does not prove physical LAN transport. See the two-machine results below for the subsequent physical-network test.

The protocol, reassembly, UDP socket, and subprocess integration tests passed with AddressSanitizer and UndefinedBehaviorSanitizer. The tests include 10,000 repeated frame assemblies and 10,000 random malformed datagrams. The integration test verifies content corruption detection, packet loss, expiry, reordered packets, duplicates, oversized datagrams, and endpoint isolation.

Tests preceded each major implementation. Initial builds failed on missing protocol, reassembly, and socket headers. The application test failed because `larp-client` did not exist. After implementation, those checks passed. These initial failures were compile or startup failures, not runtime failures in stub implementations.

The benchmark does not measure glass-to-glass latency, one-way network latency, CPU utilization, or saturation throughput. It measures application datagram throughput and process RSS at the selected workload. No optimization claim follows from this baseline.

## Two-machine verification

Both machines ran commit `f66a7ac`, built in Release mode with GCC 16.2.1. The remote machine, `receiver-host`, passed all four CTest tests before the transfer.

Tailscale SSH controlled the remote process. Media packets traveled from `receiver-host` at `203.0.113.20` over Wi-Fi to the development machine at `198.51.100.10`. The remote route used `wlan0` through `203.0.113.1`. This tests a routed local network between two machines, rather than a same-subnet connection or Tailscale media transport.

The direction matters. The development machine routes outbound traffic to `203.0.113.20` through `proton0`, and its ping received no replies. The reverse LAN ping succeeded. No routes or VPN settings were changed.

The first 10-second transfer sent 600 frames and 9,000 packets. The receiver validated 598 frames and received 8,970 packets, with two skipped frames and no corrupt frames. Since every frame contained 15 packets, the totals show 30 lost packets, or 0.33%. The client's displayed packet loss remained zero because no packets from either skipped frame arrived. This demonstrates why the skipped-frame count and sender totals matter.

An initial 30-second attempt used a client deadline that did not allow enough time for SSH startup. The client stopped before the host finished. That attempt is excluded from loss measurements. Its sampled receiver RSS stayed at 8,396 KiB. The repeat waits for the remote shell to report readiness before starting the client and releasing the host.

The synchronized 30-second transfer sent 1,800 frames and 27,000 packets. The receiver validated 1,795 frames and received 26,925 packets. It reported five skipped frames, zero incomplete frames, and zero corruption. The sender and receiver totals show 75 lost packets, or 0.28%. Throughput during transmission was about 8.1 Mbps at about 60 FPS.

Receiver RSS remained at 8,392 KiB across 309 samples taken every 100 ms after a two-second warmup. This is a bounded-workload observation, not a general leak-freedom claim. Remote host RSS was not sampled.

Raw receiver reports are preserved in [the 10-second log](../benchmarks/results/lan-10s.log) and [the synchronized 30-second log](../benchmarks/results/lan-30s.log). Both runs used 16,384-byte frames at 60 FPS. The synchronized run used an ephemeral client port and a 33-second client lifetime. The remote shell waited for the client port on standard input before starting the host.

The two-machine transport check passed with observable whole-frame loss. It does not establish the cause of that loss, reverse-direction connectivity, saturation throughput, or end-to-end latency. No transport implementation changes or optimizations were made for this test.

## Reverse-direction follow-up

At the follow-up check, the development machine routed `203.0.113.20` through `198.51.100.1` on `enp8s0`, rather than through Proton VPN. No network configuration changes were made by the test. Ping still received no replies.

The remote client bound `203.0.113.20` and reported readiness before the local host started. The host sent 1,800 frames and 27,000 packets over 30 seconds. The remote client received zero packets during its 33-second lifetime. Successful UDP sends do not establish delivery. The cause of this directional connectivity failure remains undiagnosed.

The [reverse LAN log](../benchmarks/results/lan-reverse-30s.log) preserves the receiver output. This run does not change the successful forward LAN result.

The reverse transfer over Tailscale succeeded. The remote client bound `192.0.2.10`, and the local host sent 600 frames and 9,000 packets over 10 seconds. All 600 frames validated, with zero missing packets, skipped frames, or corruption. The [reverse Tailscale log](../benchmarks/results/tailscale-reverse-10s.log) preserves the result. This verifies the remote receiver on the overlay path; it does not prove reverse physical LAN connectivity or identify which network setting blocks that path.
