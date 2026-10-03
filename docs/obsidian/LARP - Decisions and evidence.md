---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-03
---

# Decisions and evidence

LARP's recorded decisions favor bounded storage, explicit ownership, and independently testable stages. The sources below explain actual tradeoffs. They do not establish that every constant is optimal or every alternative was evaluated.

## One active frame trades completeness for bounded state

`docs/design.md` explicitly chooses one 4 MiB reassembly buffer. Memory does not grow with traffic volume, frame IDs, or the number of incomplete frames a sender attempts to introduce.

The cost is cross-frame reordering loss. A newer frame replaces an incomplete older one. A bounded multi-frame window might preserve more frames, but the document requires evidence that extra memory and retained age help the stream before adding it.

The same document postpones scatter-gather transmission. Removing the host's per-packet copy was not justified by the initial measured rate. This is a documented deferred optimization, not an assertion that copying is always faster.

`docs/design.md` is a milestone-one record. Its original statements that codecs and rendering were absent, and all frames sent in bursts, are historical. The note now distinguishes that baseline from current capture, codecs, rendering, adaptive pacing, and authenticated sessions.

Sources: [original design](../design.md), commits `308e10f`, `cadc27f`, and `dc3c15c`.

## Independent H.264 trades bandwidth for recovery

The software codec guide explicitly chooses SPS, PPS, and an IDR picture in every frame. This supports late joining and immediate recovery after a lost frame. Inter-frame encoding could compress better, but introduces reference dependencies and a keyframe wait.

The strict one-packet, one-image API makes unexpected buffered output an error. The protocol deliberately supports one preview profile rather than arbitrary video files.

This is the foundation for [LARP - H264 codec and recovery](LARP%20-%20H264%20codec%20and%20recovery.md). Adaptive mode changes rate control but preserves independent pictures.

Sources: [software codec decisions](../software-h264.md), implementation commit `1ca24c4`, acceptance record `e5e2178`.

## Synchronous rendering trades throughput for simpler ownership

The rendering guide records a comparison with a worker and latest-frame mailbox, direct Wayland shared-memory buffers, and an SDL renderer. The selected CPU window keeps borrowed-frame consumption and shutdown on one thread and avoids GPU renderer initialization for small CPU images.

The cost is that rendering blocks receipt. A mailbox could become useful if measurements show this path falling behind. The existing decision does not prove that synchronous rendering will scale to larger streams.

Sources: [rendering design comparison](../rendering.md), commit `09fe19e`.

## A local PipeWire fix has a narrower scope than a driver workaround

The capture investigation reproduced a leak using only repeated PipeWire context creation. Capture does not request realtime scheduling, so setting `module.rt=false` on its context removed an unused module and the recorded leak.

This fix changed the application's own context construction and has a dedicated regression. It did not suppress leak reports or alter system PipeWire settings.

The NVIDIA investigation is different. A standalone `cuInit(0)` process, without FFmpeg or LARP codec linkage, reproduced 183 leaked bytes in four allocations. A hundred initialization calls did not multiply that baseline.

Private vendor-profile experiments made the leak appear or disappear with the `CudaNoStablePerfLimit` profile name. The documented hypothesis is that replacing a built-in profile loses an earlier allocation. That remains an inference because the driver source is unavailable.

Separate FFmpeg-only NVENC lifecycle probes recorded 279 leaked bytes for one normal session and 1,143 bytes for ten. After subtracting the 183-byte CUDA baseline, the normal lifecycle results correspond to 96 bytes per session. Retaining the library across ten sessions and then explicitly unloading it left one 96-byte remainder beyond the baseline.

Keeping a library loaded until process exit can make allocations reachable and hide leak reports. The investigation therefore does not treat preloading as a cleanup fix. Flat sustained RSS also does not erase repeated lifecycle leak reports.

NVIDIA backends pass functional checks, but milestone-five acceptance stays blocked. The October 3 locked-screen recheck on the second machine still fails all five NVIDIA sanitizer tests. Its standalone CUDA probe again reports 183 bytes in four allocations. No driver or profile settings changed. The bug-report file is a draft, not evidence of an external submission or vendor resolution.

Sources: [capture leak reproduction](../capture.md), fix `cf8cd28`; [NVIDIA investigation](../nvidia-leak-investigation.md), [report draft](../nvidia-bug-report.md), [recheck evidence](../../benchmarks/results/nvidia-recheck-2026-09-21.log), commits `f9415a2`, `3fd4e0f`, and `09fe19e`.

## High sanitizer RSS does not by itself identify a leak

ASan retains freed allocations in a quarantine to detect use-after-free. The software codec investigation compared identical 15,000-frame workloads with Release, default ASan, and ASan with quarantine disabled. Leak detection remained enabled in both sanitizer runs.

| Configuration | Recorded RSS range in KiB |
| --- | ---: |
| Release | 30,144–30,356 |
| Default ASan | 146,336–329,360 |
| ASan with quarantine disabled | 48,032–50,360 |

The comparison supports quarantine retention as the source of much of that instrumented growth. It does not prove all future workloads leak-free. The recorded decision kept FFmpeg packet ownership because Release measurements did not justify changing it. Normal sanitizer checks retain their usual quarantine.

The later adaptive soak makes the same distinction. Default sanitizer RSS grows substantially, while a smaller-quarantine diagnostic reduces it. Describe Release memory and sanitizer diagnostics separately.

Sources: [software memory comparison](../software-h264.md), benchmark commit `ec33113`; [adaptive soak evidence](../bandwidth.md).

## Pacing was refined using encoded traffic

The constrained-link measurements recorded 100 startup drops without pacing and 67 with pacing alone. Adding a one-frame VBV buffer to pacing produced zero startup drops in the recorded Release and sanitizer scenarios.

Pacing alone did not solve the encoder's startup burst. The evidence motivated limiting burst size before transmission as well as spacing packets afterward. The runs do not isolate a universal percentage benefit from either change.

A repeated benchmark also exposed an overly strict assertion: an 881 kbps transient target exceeded an 880 kbps limit despite delivered traffic remaining within capacity. Settling now checks the average target over three samples. Delivered-rate, loss, and decoding checks still apply. This recognizes capacity probing rather than relaxing every acceptance criterion.

Source: [bandwidth experiments and benchmark correction](../bandwidth.md). These changes are now recorded in implementation commit `a8fd3be`.

## Tailscale connectivity and authenticated sessions have separate roles

Milestone 12 retains Tailscale for routing and adds authentication and recovery within LARP. The session layer uses libsodium rather than implementing a cryptographic algorithm. It derives separate directional keys for each handshake, rejects reused application counters, and starts a fresh generation after heartbeat failure.

Frames produced during reconnection are discarded rather than retained for later delivery. This preserves bounded storage and avoids delivering a backlog after recovery. The shared-key design has no forward secrecy: a compromised key exposes recorded sessions and allows impersonation. The automated checks establish tested behavior, not an independent security audit.

Local Release and sanitizer suites passed 26 tests each. Remote Release passed 24 tests, and remote non-NVIDIA sanitizers passed 21. Final targeted session checks passed after the last refinements. The encrypted two-machine runs recovered after both sender and receiver restarts, with zero corruption and no sanitizer diagnostics.

Sources: [session implementation](../../transport/session.cpp), [acceptance report](../session-verification.md), [source manifest](../../benchmarks/results/milestone-twelve-source.log). See [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md).

## History anchors

| Date | Change | Git anchor |
| --- | --- | --- |
| 2026-09-12 | Bounded transport, socket ownership, synthetic sender | `308e10f`, `cadc27f`, `34e89e5` |
| 2026-09-12 | Loss reasons and accounting | `f5cde69`, `5badeb1` |
| 2026-09-12–13 | Portal capture and context leak fix | `a105aa3`, `cf8cd28`, `ef2a23f` |
| 2026-09-13 | Software codec and memory comparison | `1ca24c4`, `ec33113`, `e5e2178` |
| 2026-09-14 | NVIDIA support and isolated leak probes | `05a53c0`, `f9415a2`, `3fd4e0f` |
| 2026-09-21 | Live window and NVIDIA leak recheck | `09fe19e` |
| 2026-09-24 | Adaptive, pacing, bandwidth, soak, and rendering addenda | Initially working-tree changes; included in `a8fd3be` |
| 2026-10-03 | Two-machine rendering and adaptive acceptance; authenticated sessions, replay rejection, and restart recovery | `a8fd3be` and saved acceptance logs |

## Questions the evidence leaves open

The local record does not establish the optimal control thresholds, arbitrary-path congestion fairness, end-to-end display latency, the exact closed-driver ownership defect, or an independent security audit of the shared-key session protocol. Remote acceptance for milestones six, seven, and twelve is now recorded. Windows acceptance remains unavailable.

The reviewed history and documents also do not provide a detailed comparative rationale for choosing C++ over another language or this custom media transport over every existing protocol. The code shows the chosen implementation. It cannot establish those broader motivations on its own.

Return to [LARP - Study map](LARP%20-%20Study%20map.md) or continue with [LARP - Study exercises](LARP%20-%20Study%20exercises.md).
