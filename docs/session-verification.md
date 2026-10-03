# Session acceptance on 2026-10-03

The session implementation was built and tested on two Linux machines over
Tailscale. All new tests use synthetic inputs and either SDL dummy displays or
decoded snapshots. They work while the screen is locked and do not request a
capture portal. The second machine's existing checkout was left untouched.

The [source manifest](../benchmarks/results/milestone-twelve-source.log)
records the archived implementation, build options, crypto library, and GPU.

## Software suites

| Build | Result | Evidence |
| --- | --- | --- |
| Local Release, capture and rendering enabled | 26 of 26 passed | [Full suite](../benchmarks/results/milestone-twelve-release-tests.log) |
| Local Debug, ASan, LSan, and UBSan | 26 of 26 passed | [Full suite](../benchmarks/results/milestone-twelve-sanitize-tests.log) |
| Remote Release, capture disabled, rendering enabled | 24 of 24 passed | [Full suite](../benchmarks/results/milestone-twelve-remote-release-tests.log) |
| Remote sanitized Debug, capture and rendering disabled | 21 of 21 non-NVIDIA tests passed | [Full suite](../benchmarks/results/milestone-twelve-remote-sanitize-tests.log) |

After the full local suites, key-file validation was hardened against named
pipes and symlinks, and rejection of a forged high media counter was checked.
After the full remote suites, the synthetic-byte sender was changed to service
heartbeats between frames. The final session check passes in
[local Release](../benchmarks/results/milestone-twelve-release-security.log),
[local sanitizers](../benchmarks/results/milestone-twelve-sanitize-security.log),
and [remote sanitizers](../benchmarks/results/milestone-twelve-remote-sanitize-security.log).
The unchanged plaintext synthetic-byte path also passes its
[integration regression](../benchmarks/results/milestone-twelve-raw-regression.log).

The session fixture exercises production sockets, encryption, packetization,
reassembly, H.264 decoding, and adaptive feedback. Sender restart delivers
exactly 40 decoded frames across two authenticated generations despite injected
tampering, duplicates, and fragment reordering. Receiver restart establishes
a fresh feedback baseline. A 1.5-second complete bidirectional blackhole
requires a new session, discarded media attempts, and decoded recovery.
Wrong keys, unexpected source IPs, plaintext envelopes, malformed key files,
and retired-session replay are rejected. The low-FPS synthetic-byte fixture
delivers all four frames and observes at least eight encrypted heartbeat
requests between its one-second frame intervals.

These tests cover the implemented behavior and negative cases. They do not
replace an independent cryptographic security audit.

## Two-machine encrypted streaming

Both directions use production synthetic H.264, adaptive feedback, and the
same source build. One direction keeps a remote sender running for 300 frames
at 30 FPS while the local receiver exits and restarts on the same port. The
other keeps a remote receiver running while two local senders each transmit
60 frames at 20 FPS with a fresh session.

| Local build | Before receiver restart | After receiver restart | Across sender restart | Result |
| --- | --- | --- | --- | --- |
| Release | 113 decoded and presented | 156 decoded and presented | 120 decoded and presented | [Passed](../benchmarks/results/milestone-twelve-tailscale-release.log) |
| ASan, LSan, and UBSan | 112 decoded and presented | 156 decoded and presented | 120 decoded and presented | [Passed](../benchmarks/results/milestone-twelve-tailscale-sanitize.log) |

Every receiver reports zero corruption. The continuously running sender
establishes two sessions and prints a fresh `Feedback: 1` baseline after
recovery. The continuously running receiver establishes two sessions and
decodes all 120 frames across the sender restart. The sanitizer run emits no
sanitizer diagnostics.

The restart interruption intentionally loses video. During receiver restart,
the sender continues producing frames until heartbeat processing triggers
renegotiation. The new receiver rejects encrypted traffic from the retired
session. The Release sender reports six withheld application datagrams and
the sanitizer run reports five. The sanitizer receivers also record one
incomplete frame each at the shutdown or session transition. The recovery
check does not require zero loss across an intentional restart.

The benchmark removes its temporary shared keys on both machines. No private
key bytes or desktop content are present in the saved logs.

## Remaining milestones

Milestones 1 through 4, 6, and 7 already have Linux software acceptance evidence.
Milestone 12 adds the session checks above and the two-machine restart checks.

Milestone 5 remains blocked. All five NVIDIA sanitizer tests fail on the second
machine with the existing driver leak reports. The standalone CUDA probe also
fails outside L.A.R.P.'s codec code. See
[the locked-screen NVIDIA recheck](nvidia-leak-investigation.md#locked-screen-recheck-on-2026-10-03).
Leak detection was retained, and no driver or profile settings changed.

Windows milestones 8 through 11 remain unimplemented and deferred because no
Windows test machine is available. Linux tests do not establish Windows
capture, codec, rendering, socket, or interoperability acceptance.
