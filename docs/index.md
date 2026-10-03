---
aliases:
  - LARP
  - L.A.R.P.
tags:
  - larp
  - project
updated: 2026-10-03
---

# L.A.R.P. documentation

L.A.R.P. streams synthetic frames and Linux PipeWire screen previews over
Tailscale. It supports software H.264, live rendering, adaptive bitrate, and
optional encrypted sessions with process restart and network recovery.

## Milestone status

Checked milestones have implementation and acceptance evidence for their
documented Linux workloads. NVIDIA acceptance remains blocked, and Windows
work is deferred.

- [x] 1. Synthetic frame transport over UDP. [[measurements|Transfer measurements]]
- [x] 2. Packet loss and stale-frame handling. [[milestone-two|Loss and recovery]]
- [x] 3. Linux PipeWire screen capture. [[capture|Capture guide]]
- [x] 4. Software H.264 encoding and decoding. [[software-h264|Software codec guide]]
- [ ] 5. NVENC and NVDEC acceleration. Implemented, but blocked by driver leaks. [[nvidia-leak-investigation|Leak investigation]]
- [x] 6. Low-latency Linux rendering. [[rendering|Rendering guide]]
- [x] 7. Network telemetry and adaptive bitrate. [[adaptive|Adaptive streaming]]
- [ ] 8. Windows capture abstraction. Unimplemented and deferred.
- [ ] 9. Windows hardware encoding and decoding. Unimplemented and deferred.
- [ ] 10. Windows rendering and Winsock transport. Unimplemented and deferred.
- [ ] 11. Linux and Windows interoperability. Unimplemented and deferred.
- [x] 12. Tailscale session security and network recovery. [[sessions|Authenticated sessions]]

Milestone 12 keeps Tailscale. Custom NAT traversal and a separate relay are
outside its scope. Windows milestones require a Windows test machine, which
is currently unavailable.

## Latest verification

The October 3 checks passed all 26 local Release tests and all 26 local
sanitizer tests. The second Linux machine passed 24 Release tests and 21
non-NVIDIA sanitizer tests. Final session checks passed after the key-file
validation and low-FPS heartbeat refinements.

Encrypted Tailscale checks passed in both directions with sender and receiver
restarts. After receiver restart, the new receiver decoded and presented 156
frames. Across sender restart, the remote receiver decoded and presented all
120 frames. Neither run reported corruption; the sanitizer run reported no
sanitizer errors. See [[session-verification|Session acceptance and evidence]].

The session checks use synthetic video and dummy displays or snapshots. They
can run with the screen locked. Earlier milestone 6 and 7 live-rendering and
PipeWire checks were completed with an unlocked desktop.

## Run and understand the project

- [Read the Obsidian study notes](obsidian/README.md)
- [[sessions|Set up keys, encrypted streaming, and restart recovery]]
- [[capture|Capture a Linux screen through PipeWire]]
- [[software-h264|Encode and decode software H.264]]
- [[rendering|Present received video in a window]]
- [[adaptive|Configure receiver feedback and adaptive bitrate]]
- [[bandwidth|Measure packet pacing and bandwidth behavior]]
- [[protocol|Look up packet formats, session envelopes, and counters]]
- [[design|Read transport design decisions]]
- [[measurements|Read the initial transfer measurements]]

## Remaining work

Milestone 5 needs a driver and library combination that passes leak checks.
The current NVIDIA 615.71.09 installation fails all five hardware sanitizer
tests. A standalone CUDA initialization probe reproduces a 183-byte leak
outside L.A.R.P.'s codec implementation. Driver profiles and sanitizer checks
were left unchanged. See [[nvidia|NVIDIA implementation]],
[[nvidia-leak-investigation|Isolated reproductions]], and
[[nvidia-bug-report|Unsubmitted NVIDIA report draft]].

Windows milestones 8 through 11 require implementation and Windows acceptance.
Linux results do not establish Windows compatibility.

The shared-key session protocol has no forward secrecy or independent security
audit. Its limits are recorded in [[protocol#Authenticated session envelope|The session protocol reference]].
