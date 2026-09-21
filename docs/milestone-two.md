# Milestone two: loss and stale-frame handling

Milestone two keeps the one-frame receiver and version-one wire format. It adds frame-loss reporting, finalized packet-loss accounting, drop reasons, and a configurable expiry deadline. There are no retransmissions or new packet queues.

## Behavior

Observed packet loss includes packet counts only after a frame completes or is discarded. An active frame can no longer dilute the reported loss percentage. Whole-frame gaps appear in Frame loss and Skipped because their packet counts are unknown. Before enough information exists, the percentage is N/A.

Dropped frames have exactly one reason: Expired, Superseded, or Shutdown. Repeated shutdown calls do not count a frame twice. Late packets cannot revive a completed, replaced, or expired frame. A duplicate cannot extend a frame's deadline.

The optional `FRAME_TIMEOUT_MS` client argument accepts 1 through 1,000 ms and defaults to 100 ms. The receive poll interval is at most the selected deadline and at most 10 ms. Operating-system scheduling can still delay expiry. All deadlines use receiver-local monotonic time.

The receiver still allocates one 4 MiB payload buffer at construction and uses the existing fixed packet bitmap. No receive-path allocation or additional frame buffer was introduced. Statistics use fixed-size counters.

## Failing-before evidence

The new `loss` test initially failed at `receiver.stats.expected == 0` after receiving the first packet of an incomplete frame. The previous implementation had already counted that frame's advertised packets. Moving packet accounting to completion and discard made the regression pass.

The client integration check then failed on the new timeout argument with the old usage message. After implementation, it passed with a 50 ms timeout, exact loss percentages, and the expected drop-reason counters.

The Tailscale impairment test failed against the milestone-one remote binary because `Expired: 1` was absent. It passed against milestone two with this final result:

```text
Packets: 10
Observed packet loss: 20.00%
Frame loss: 50.00%
Dropped: 2
Missing: 2
Expired: 1
Superseded: 1
Shutdown: 0
Skipped: 1
Validated: 3
Corrupt: 0
Duplicates: 1
Stale: 1
```

## Passing-after evidence

All five CTest tests passed in the local Release build and under AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer. All five also passed in a Release build on `receiver-host`. The deterministic tests cover exact deadline boundaries, duplicate packets, late-packet rejection, recovery after loss, drop-reason accounting, maximum-size reconstruction, and invalid timeouts.

The controlled Tailscale check uses `tests/tailscale_impairment.py`. It takes the SSH target, remote build directory, and receiver Tailscale IPv4 address. It uses one socket and a fixed packet sequence. Exact timer behavior belongs to unit tests; additional network loss can cause this acceptance check to fail.

A 30-second Tailscale transfer sent 1,800 frames of 16,384 bytes at 60 FPS from the development machine to `receiver-host` at `192.0.2.10`. The remote client ran commit `f5cde69`, compiled with GCC 16.2.1 in Release mode. SSH controlled startup; UDP media used the Tailscale address.

All 27,000 packets arrived and all 1,800 frames validated. There were no dropped, skipped, stale, or corrupt frames. Remote receiver RSS stayed at 8,384 KiB across 308 samples taken every 100 ms after a two-second warmup. The sender and receiver reports are in [the sustained-transfer log](../benchmarks/results/milestone-two-tailscale-30s.log).

The RSS result covers this workload and duration. It does not prove general leak freedom, measure remote host memory, or establish glass-to-glass latency. Screen capture remains milestone three.
