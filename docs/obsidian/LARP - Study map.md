---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-03
---

# LARP study map

L.A.R.P. means Low-latency Adaptive Remote Protocol. It is a C++23 Linux prototype that sends synthetic frames or desktop previews over UDP. The receiver reconstructs frames, validates or decodes them, and saves a snapshot or displays a live window.

These notes explain the files in `../..` as inspected on 2026-10-03. The committed baseline is `09fe19ec9c4f908926393bc0e49b751d7af99502`. Implementation commit `a8fd3be` adds adaptive bitrate, packet pacing, bandwidth benchmarks, rendering recovery, and authenticated sessions on top of that baseline. These notes include its saved acceptance evidence and distinguish completed Linux checks from NVIDIA and Windows blockers.

## Read in this order

1. [LARP - Architecture and frame flow](LARP%20-%20Architecture%20and%20frame%20flow.md) explains the components and follows one frame.
2. [LARP - UDP protocol and reassembly](LARP%20-%20UDP%20protocol%20and%20reassembly.md) explains packet boundaries, state transitions, and loss accounting.
3. [LARP - Screen capture](LARP%20-%20Screen%20capture.md) explains desktop permission, PipeWire buffers, and pixel conversion.
4. [LARP - H264 codec and recovery](LARP%20-%20H264%20codec%20and%20recovery.md) explains compression, media validation, and independent frames.
5. [LARP - Rendering and ownership](LARP%20-%20Rendering%20and%20ownership.md) explains the window and buffer lifetimes.
6. [LARP - Adaptive bitrate and pacing](LARP%20-%20Adaptive%20bitrate%20and%20pacing.md) explains the feedback loop and burst control.
7. [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md) explains authentication, encryption, replay rejection, and restart recovery.
8. [LARP - Decisions and evidence](LARP%20-%20Decisions%20and%20evidence.md) explains documented tradeoffs, history, and unresolved questions.
9. [LARP - Study exercises](LARP%20-%20Study%20exercises.md) provides local experiments and questions to test your understanding.

## Implementation and acceptance are different

| Milestone                            | Status recorded by this checkout                                           |
| ------------------------------------ | -------------------------------------------------------------------------- |
| 1. Synthetic UDP transport           | Complete                                                                   |
| 2. Loss and stale-frame handling     | Complete                                                                   |
| 3. PipeWire capture                  | Complete                                                                   |
| 4. Software H.264                    | Complete                                                                   |
| 5. NVIDIA encode and decode          | Functional, acceptance blocked by driver leaks                             |
| 6. Linux live rendering              | Complete, including two-machine Tailscale rendering acceptance |
| 7. Feedback and adaptive bitrate     | Complete, including loss, outage, adaptive, and live-capture acceptance               |
| 8–11. Windows capture, codec, rendering, and interoperability | Unimplemented and deferred; no Windows test machine available |
| 12. Tailscale session security and network recovery | Complete, including encrypted two-machine restart checks |

The optional shared-key session layer now provides negotiation, authenticated encryption, replay rejection, and recovery after process restarts or network interruptions. Tailscale continues to supply connectivity. Custom NAT traversal, a separate relay, input forwarding, and arbitrary-resolution H.264 streaming are outside the current implementation.

The October 3 local suites passed 26 tests in Release and 26 with ASan, LSan, and UBSan. The second machine passed 24 Release tests and 21 non-NVIDIA sanitizer tests. Final session checks passed after the last boundary and heartbeat refinements. All five NVIDIA sanitizer tests still fail with driver leak reports. See [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md) for the acceptance results and their limits.

## How to use the sources

Each note ends with local source links and symbols to search. The notes are a study snapshot. The repository remains the authority when its code changes.

Documented rationale is marked as such. A consequence of code is not automatically the author's motivation. Numerical test results are historical measurements with stated limits, not guarantees for every machine.

The decision investigation used local source, repository documentation, Git history, tests, and saved benchmark results. External issues, reviews, chats, and operational telemetry were not examined. No origin story is inferred from their absence.

Sources: [README](../../README.md), [milestone-one design](../design.md), [adaptive status](../adaptive.md).
