# Bandwidth changes and packet pacing

The local bandwidth benchmark sends real software H.264 through a constrained
UDP proxy. It tests how the sender responds when capacity changes, including
startup bursts that a packet-loss-only test cannot expose.

```sh
python3 benchmarks/bandwidth.py build-resume --output /tmp/larp-bandwidth.json
```

The receiver runs without a window. The default scenario lasts about 21 seconds,
including time to drain and report after the 480-frame sender finishes. It uses
these forward-link capacities:

| Stream time | Capacity |
| --- | ---: |
| 0–3 seconds | 4,000 kbps |
| 3–10 seconds | 800 kbps |
| 10–16 seconds | 4,000 kbps |

The sender starts at 2,000 kbps and 30 FPS. Media and probes share the same FIFO.
Replies use an unconstrained reverse path. The default FIFO holds 8,192 remaining
wire bytes and drops arriving packets when full. `--queue-bytes` changes that
limit. All limits count 28 IPv4 and UDP header bytes per datagram in addition
to the UDP payload. Tailscale and link-layer overhead are not modeled.

The proxy serializes packets using elapsed monotonic time and the capacity
active during each time segment. A capacity change also affects packets already
queued. Idle time creates no burst credit. Sampling and forwarding run in one
Python process, so operating-system scheduling can delay actual delivery beyond
the modeled service time. Deterministic model tests cover partial packets,
FIFO order, tail drop, capacity changes, and idle time without wall-clock sleeps.

The benchmark records per-second target bitrate, delivered wire bitrate, queue
drops, and cumulative decoded frames. `--output` saves those samples and both
process logs as JSON. It checks zero startup drops, a bounded queue, adaptation
near the reduced capacity, low loss after settling, and rising targets with
continued decoding after recovery. The delivered-rate check allows one packet
of interval-boundary accounting and scheduling variation. Target bitrate is an
encoder setting, so it need not equal delivered wire bitrate.

The controller deliberately probes available capacity with additive increases.
Targets can briefly overshoot and fall again. The test therefore permits the average target over the three settling samples
to reach 10% above reduced capacity. It still checks each delivered-rate sample
and the total settled packet loss. A brief target probe is not sustained
overshoot. Recovery is measured from the target at the capacity
transition, not from an earlier probing peak.

## Sender changes

Adaptive H.264 now spreads a frame's packets over three quarters of a frame
period, capped at 25 ms. A single-packet frame sends immediately. Each subsequent
packet waits one spacing interval after the preceding send. If scheduling runs
late, the sender does not send a catch-up burst. No packet queue or worker thread
was added. Raw and fixed-quality streams retain their existing unpaced behavior.

The cap also applies at low FPS so the sender does not spread one frame across
most of a second. The default receiver timeout remains 100 ms. A shorter custom
timeout must leave room for packet spacing and network delay. Scheduling can
extend the actual send duration beyond the requested window.

The adaptive software encoder's VBV buffer now holds one frame's target bits,
calculated from bitrate divided by FPS, instead of half a second of target bits.
The same calculation applies when bitrate changes. This limits encoder bursts
before packet pacing. It does not guarantee an exact per-frame byte count.
Tighter buffering can reduce image quality on complex frames to fit the budget.
The target rate limits and the fixed-quality encoder path are unchanged.

Pacing adds up to about 25 ms of intentional intra-frame send time at 30 FPS.
That is a measured reliability tradeoff for a small queue, not a claim of lower
glass-to-glass latency. Feedback is processed between frames, so paced sending
also contributes to the reported application RTT.

## Reproduce the checks

```sh
ctest --test-dir build-resume -R '^(bandwidth-model|pacing|bandwidth)$' --output-on-failure
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-resume-sanitize --output-on-failure
```

The `pacing` test uses real UDP sockets and verifies send duration, packet order,
and complete frame reconstruction. The existing codec test still changes bitrate
within one encoder and decodes every frame. The adaptive impairment test continues
to exercise lost feedback, delayed replies, and replay rejection independently
of the bandwidth scenario.

The separate [two-machine adaptive checks](adaptive.md#two-machine-tailscale-acceptance-on-2026-10-03)
verify Tailscale delivery, feedback, and recovery under controlled impairments.
This bandwidth model does not establish Internet fairness, congestion safety
across arbitrary paths, or display latency.

## Measurements on 2026-09-24

The table compares the same 4,000 → 800 → 4,000 kbps scenario on the Intel
laptop. Startup drops count the first two one-second samples, before the
capacity change. Total drops count proxy tail drops, including any probes.

| Sender configuration | Startup packet drops | Total proxy drops | Decoded frames out of 480 |
| --- | ---: | ---: | ---: |
| Unpaced, half-second VBV | 100 | 228 | 419 |
| Paced, half-second VBV | 67 | 235 | 424 |
| Paced, one-frame VBV, Release | 0 | 96 | 436 |
| Paced, one-frame VBV, sanitized | 0 | 104 | 432 |

The [unpaced measurements](../benchmarks/results/milestone-seven-bandwidth-before.json)
and [pacing-only measurements](../benchmarks/results/milestone-seven-bandwidth-paced.json)
retain the intermediate configurations. Pacing alone did not remove the
encoder's startup burst, which motivated the smaller VBV buffer. Differences
in controller timing also affect total loss, so these runs do not isolate a
universal percentage improvement attributable to pacing alone.

In the final [Release run](../benchmarks/results/milestone-seven-bandwidth-release.log),
the low-capacity settling samples reported targets of 737, 801, and 865 kbps
with no packet drops in those intervals. At the recovery transition the target
was 648 kbps; it rose to 968 kbps by the 15-second sample. The
[sanitized run](../benchmarks/results/milestone-seven-bandwidth-sanitize.log)
also passed startup, capacity, loss, and recovery checks. Neither run reported
corrupt frames. The controller reduced bitrate after the capacity drop, but
transient loss during that adjustment remains visible in the table.

All 24 tests in the Release suite passed, followed by the added low-FPS
regression. All 25 tests passed together under ASan, LSan, and UBSan. The
low-FPS regression sends at one FPS and verifies both frames decode without
expiry, covering the pacing-window cap.


## Repeat capacity changes and measure memory

Run four capacity cycles, about 64 seconds of video, with a five-second receiver
shutdown margin:

```sh
python3 benchmarks/bandwidth.py build-resume --cycles 4 \
  --max-rss-growth-kib 8192 --output /tmp/larp-soak.json
python3 benchmarks/bandwidth.py build-resume --cycles 4 --cpu-load \
  --max-rss-growth-kib 8192 --output /tmp/larp-soak-cpu.json
```

`--cycles` accepts 1 through 30. The same settling, recovery, queue, and corruption
checks apply to every cycle. The sender must finish all requested frames before
the receiver exits. This catches a sender that falls behind enough to overrun
the test duration.

The benchmark samples Linux RSS once per second and excludes the first five
seconds from its memory summary. It records first, last, minimum, maximum, and
sample count for both sender and receiver. Samples from an exited sender are
omitted. `--max-rss-growth-kib` limits last-minus-first growth for each process;
it does not limit peak RSS or prove leak freedom. Use the reported range as well
as the endpoint difference when reviewing a run.

`--cpu-load` pins the sender and one busy Python worker to the same allowed
logical CPU. The worker uses niceness 10, below the sender's normal priority.
The receiver and proxy keep their existing CPU affinity. Only these benchmark
child processes are affected, and the worker is terminated on success or failure.
The benchmark samples worker CPU time and fails unless it averages at least
half a logical CPU. This tests competition with a background workload, not
starvation by equal-priority tasks or a fully saturated machine.

Sanitized runs can retain freed allocations in the sanitizer quarantine. Run
them without the Release RSS limit, then inspect the measurements and exit-time
leak diagnostics separately:

```sh
ASAN_OPTIONS=detect_leaks=1 python3 benchmarks/bandwidth.py \
  build-resume-sanitize --cycles 4 --cpu-load --output /tmp/larp-soak-sanitize.json
```

### Sustained measurements on 2026-09-24

Four cycles sent 1,920 frames over about 64 seconds. Every cycle recovered after
its capacity drop, with zero corrupt frames. The repeated run exposed a benchmark
issue: a brief 881 kbps target exceeded the old per-sample 880 kbps assertion
despite no settled drops and delivered traffic below capacity. Settling now uses
the three-sample average target. Wire-rate, packet-loss, and decoding checks
remain unchanged. Regression cases accept a transient probe but reject sustained
overshoot, excessive wire rate, and settled packet loss.

| Run | Decoded frames | Sender RSS growth, KiB | Receiver RSS growth, KiB |
| --- | ---: | ---: | ---: |
| Release, unloaded | 1,829 | 0 | 168 |
| Release, background CPU load | 1,829 | 0 | 168 |
| Default ASan, LSan, and UBSan, background CPU load | 1,837 | 9,852 | 228,848 |

Growth is last minus first after warmup. Deliberate bandwidth reductions still
cause transient frame loss. These runs do not claim lossless streaming across
capacity changes or general leak freedom.

The [unloaded samples](../benchmarks/results/milestone-seven-soak-release.json)
were rechecked with the corrected settling assertion. In the
[loaded Release run](../benchmarks/results/milestone-seven-soak-cpu.json), the
worker averaged 97.4% of one logical CPU. Sender RSS stayed at 25,960 KiB.
Receiver RSS ranged from 30,952 to 31,164 KiB after warmup. Both passed the
8,192 KiB net-growth limit.

The [default sanitizer run](../benchmarks/results/milestone-seven-soak-sanitize.json)
passed all four cycles and exited without sanitizer diagnostics. Its receiver
RSS ranged from 87,516 to 330,852 KiB and ended at 316,364 KiB. Instrumented RSS
was not flat; it must not be reported as the Release memory baseline.

A separate two-cycle diagnostic retained leak detection but set
`ASAN_OPTIONS=detect_leaks=1:quarantine_size_mb=16`. At approximately 32 seconds,
receiver RSS was 70,924 KiB, compared with 322,084 KiB at the same elapsed time
in the default-quarantine run. The smaller-quarantine run ranged from 67,376
to 72,680 KiB after warmup and ended below its first warm sample. It passed
both capacity cycles and exited without sanitizer diagnostics. This comparison
shows that quarantine retention materially affects the instrumented RSS numbers;
it does not turn these finite runs into a general memory-bound proof.

The [diagnostic samples](../benchmarks/results/milestone-seven-soak-quarantine16.json)
record the smaller quarantine setting. The regular sanitizer checks and default
run above still use the normal quarantine. No project sanitizer defaults changed.
The benchmark now records `ASAN_OPTIONS` in future JSON outputs.

The updated model and memory tests contain ten deterministic cases. Both those
cases and the default one-cycle streaming check passed after the benchmark
changes. These additions change verification tooling only; this sustained pass
did not require a production streaming change.
