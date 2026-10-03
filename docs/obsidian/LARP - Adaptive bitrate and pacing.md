---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-03
---

# Adaptive bitrate and pacing

Adaptive mode closes a loop: the sender asks how delivery is going, the receiver returns cumulative measurements, and the sender adjusts the software encoder's bitrate. Packet pacing controls when those encoded bytes leave the process.

This implementation is recorded in commit `a8fd3be`. Local impairment and capacity tests and two-machine Tailscale acceptance are complete. The remote controlled-loss tests decoded and presented all 340 surviving frames in both Release and the sanitizer run. A separate live PipeWire transfer delivered and presented all 120 frames with zero loss or corruption.

## Feedback travels separately from media

The media wire format stays at version 1. Control messages use magic `LAFB`, version 1, and a kind field. A probe is 32 bytes. A report is 96 bytes.

A probe contains a random nonzero session ID, sequence number, and sender timestamp. A report echoes those fields and adds receiver time and seven cumulative counters: expected packets, missing packets, completed frames, dropped frames, skipped frames, corrupt frames, and received media bytes.

The sender probes every 250 ms. H.264 receivers respond only after media has pinned the sender endpoint, and no more often than once per 100 ms. Raw and synthetic-byte receivers do not provide feedback. Recognized control packets bypass media reassembly and statistics.

Cumulative counters survive a missing report. The next accepted report still includes the events since the previous accepted report. The first report establishes a baseline rather than guessing an interval loss rate.

## Validate the reply before acting

`AdaptiveSender` accepts packets only from the configured destination. `RateControl` requires an exact match to one of eight outstanding probes and rejects duplicate sequences, replies over one second old, timestamp inconsistencies, counter regressions, and impossible counter deltas.

These checks reduce accidental mixing and replay within session accounting. In plaintext mode they are not authentication against someone who can observe the probes. Secure mode wraps probes and replies in the same authenticated encryption and replay checks as video.

In plaintext mode a receiver restart with smaller counters requires a new sender session. In secure mode `AdaptiveSender::synchronize` detects a new authenticated generation and reconstructs `RateControl` at the current bitrate with a new probe session and fresh counter baseline. Counter regressions remain invalid within one generation. See [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md). An older client that decodes media but never replies causes bitrate to fall toward the minimum.

## Measure without synchronized clocks

Application RTT is sender reply-processing time minus sender probe-creation time. Both readings use the sender's clock. Receiver throughput uses differences between receiver report times, both on the receiver's clock.

The sender never subtracts receiver time from sender time. RTT includes application scheduling, decoding, and work between network operations. It is not pure link latency.

## The control law

`RateControl` has no sockets, codec calls, sleeps, or clock reads. Its caller supplies time. This makes exact timing boundaries testable without real delays.

| Condition | Action |
| --- | --- |
| Packet loss at least 2%, frame loss at least 5%, increasing corruption, or RTT over session minimum by more than 50 ms | Reduce target by 25%, at least 500 ms between reductions |
| Four consecutive clean reports with completed frames, at least one second since the last change | Increase by 64 kbps |
| No accepted feedback for one second | Reduce by 25%, at most once per second |
| Any change | Clamp to 128–8,000 decimal kbps |

Internally bitrate is in bits per second. The CLI accepts kbps and converts it. Empty intervals cannot produce an increase.

This is additive increase and multiplicative decrease. For example, congestion can reduce 2,000 kbps to 1,500, then to 1,125. Clean intervals recover in smaller 64 kbps steps. The thresholds are implementation settings; no external standard or universally optimal calibration is established by the repository evidence.

`AdaptiveSender::wait_until` runs the feedback loop between video frames. It processes at most 32 received control datagrams per iteration, then checks the video deadline. That bound prevents an incoming control flood from keeping one iteration indefinitely inside receipt.

## Why bitrate control also needs burst control

A target average bitrate does not prevent a single picture from creating a packet burst. The local constrained-link benchmark initially observed startup loss even after adding packet spacing.

The final adaptive path combines two measures. The encoder's VBV buffer holds roughly one frame's target bits instead of half a second's. The sender spaces packets across a window of three quarters of a frame period, capped at 25 ms.

`send_frame` divides the requested window by packet count. The first packet sends immediately, so the ideal last-packet time is slightly shorter than the full window. Each later deadline starts from the preceding actual send, preventing catch-up bursts when scheduling slips.

The 25 ms cap matters at one FPS. Without it, a nominal three-quarter-period window would exceed the receiver's default 100 ms expiry deadline. Custom shorter deadlines must still leave time for spacing and network delay.

Pacing adds intentional intra-frame delay. It improves measured burst handling, but does not itself prove lower display latency. Fixed-quality and raw streams remain unpaced.

## What the local capacity test proves

`benchmarks/bandwidth.py` forwards real H.264 over a modeled FIFO with an 8,192-byte default queue. Capacity changes from 4,000 to 800 and back to 4,000 kbps. Wire accounting includes 28 bytes of IPv4 and UDP headers per packet, but excludes Tailscale and link-layer overhead.

Recorded tests check startup loss, queue bounds, settled delivery, bitrate reduction, recovery, and decoding. They permit a brief additive capacity probe while checking sustained behavior. This is evidence of the tested control loop, not proof of Internet fairness or arbitrary-path congestion safety.

Sources: [feedback format](../../protocol/feedback.hpp), [pure controller](../../transport/rate_control.hpp), [socket integration](../../transport/adaptive_sender.hpp), [packet spacing](../../transport/send_frame.hpp), [adaptive guide](../adaptive.md), [burst evidence](../bandwidth.md).
