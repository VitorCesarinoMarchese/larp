# Adaptive software streaming

Milestone 7 adds receiver feedback and adaptive bitrate to the software H.264
sender. Both synthetic video and screen capture accept `--adaptive KBPS`.
Implementation, local verification, and two-machine Tailscale network
acceptance are complete with the software codec.

## Run it locally

Build the current branch, then start a receiver:

```sh
cmake -S . -B build-resume -DCMAKE_BUILD_TYPE=Release -DLARP_TEST_NVIDIA=OFF
cmake --build build-resume -j2
./build-resume/larp-client --view-h264 127.0.0.1 5000 15
```

In another terminal, send synthetic video for about ten seconds:

```sh
./build-resume/larp-host --h264-synthetic 127.0.0.1 5000 300 30 --adaptive 2000
```

For screen capture, start a receiver with enough time for portal selection,
then use:

```sh
./build-resume/larp-host --h264 127.0.0.1 5000 30 30 --adaptive 2000
```

`KBPS` is the initial video bitrate, from 128 through 8,000 decimal kilobits
per second. `--codec software` is optional and can appear before or after
`--adaptive`. NVIDIA adaptive mode is rejected before codec initialization;
its rate control is not verified on this machine. Without `--adaptive`, the
existing fixed-quality encoding path remains active and sends no probes.

The snapshot client `--h264` also returns feedback. Raw and synthetic-byte
receivers do not. Plaintext mode requires a new receiver for each sender session. An
older receiver can still decode the unchanged media packets but will not
answer probes; the adaptive sender then falls toward the minimum bitrate.

## Feedback and control behavior

The sender probes every 250 ms. The receiver replies only after a valid media
packet has pinned the sender endpoint, and no more than once per 100 ms.
Replies contain cumulative transport counters and the receiver's local time.
Lost reports therefore do not discard intervening loss counts. Reports do not
enter the media reassembler or inflate its byte and packet statistics.

The sender accepts replies only from its destination endpoint. It also requires
an exact match to one of eight outstanding probes, including a random session
identifier, sequence, and sender timestamp. Replies older than one second,
duplicate sequences, counter regressions, impossible counter relationships,
and malformed messages are rejected. These checks protect session accounting;
they do not provide authentication in plaintext mode. The optional
[session layer](sessions.md) encrypts and authenticates both video and feedback.

`RateControl` has no sockets, codec calls, or wall-clock reads. Its caller supplies
monotonic time. It compares cumulative counter differences between accepted
reports and applies these rules:

- Reduce the current target by 25% when observed packet loss reaches 2%, frame
  loss reaches 5%, corruption increases, or RTT rises more than 50 ms above
  the session minimum. Reductions are at least 500 ms apart.
- Increase by 64 kbps after four consecutive reports below those thresholds
  with completed frames, at least one second after the last change.
- When feedback is absent for one second, reduce by 25%, at most once per
  second. This also covers total loss before any report arrives.
- Clamp every target to 128 through 8,000 kbps. Never queue missed frames or
  retransmit damaged video.

The first accepted report establishes a counter baseline. It does not infer
an interval loss rate. Empty intervals cannot trigger an increase. A restarted
receiver with smaller counters requires a fresh sender session in plaintext
mode. Authenticated mode negotiates a fresh session automatically after a
receiver restart or heartbeat timeout. The controller keeps its current target
and establishes a new feedback counter baseline for that session.

The sender prints `Feedback`, `RTT us`, interval packet and frame loss,
`Receive Mbps`, `Target kbps`, `Feedback age ms`, and `Rejected feedback`.
RTT uses the sender's clock at probe creation and reply processing. It includes
application scheduling and receiver processing; it is not pure network latency.
The receiver timestamp is used only for differences between receiver reports.
No cross-machine clock synchronization is assumed. Reports printed during an
outage retain the last measured values and show their increasing age. Before
the first report, the numeric measurements are zero and `Feedback` is zero.

The software encoder uses average bitrate control with a VBV maximum equal
to the target and a one-frame VBV buffer. See [bandwidth testing and pacing](bandwidth.md)
for the measurements that motivated the smaller buffer. Rate changes update the existing
FFmpeg encoder context. FFmpeg's [libx264 wrapper](https://ffmpeg.org/doxygen/trunk/libx264_8c_source.html)
reconfigures bitrate and VBV parameters during encoding. The real encoded-byte
test below verifies that this changes output on the installed build.

Video remains independently decodable, all-intra H.264 at preview resolution.
Targets describe compressed video, not total IP bandwidth. Headers, packet
spacing, encoder transients, and content complexity affect actual wire bitrate.
The controller has conservative fixed thresholds, with no bandwidth estimator,
fairness guarantee or latency guarantee. Adaptive frames now use bounded packet
spacing, described in [the pacing guide](bandwidth.md). A sustained bottleneck
below 128 kbps still causes loss.

## Feedback wire format

Control messages use the media socket and have their own magic. Integers are
big-endian. A probe is exactly 32 bytes and a reply is exactly 96 bytes.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic `0x4c414642`, ASCII `LAFB` |
| 4 | 2 | Version, 1 |
| 6 | 2 | Kind, 1 for probe or 2 for reply |
| 8 | 8 | Nonzero sender session identifier |
| 16 | 8 | Nonzero probe sequence |
| 24 | 8 | Sender monotonic timestamp in microseconds, echoed by receiver |
| 32 | 8 | Reply only: receiver monotonic timestamp in microseconds |
| 40 | 8 | Finalized expected media packets |
| 48 | 8 | Missing media packets |
| 56 | 8 | Completed transport frames |
| 64 | 8 | Dropped incomplete frames |
| 72 | 8 | Entirely skipped frames |
| 80 | 8 | Completed frames rejected by the media decoder |
| 88 | 8 | Received media UDP payload bytes, including L.A.R.P. headers |

Each counter is bounded to `2^60` when parsing. Missing packets cannot exceed
expected packets, corrupt frames cannot exceed completed frames, and completed
plus dropped frames cannot exceed expected packets. Accepted interval counters
must also remain consistent. The version-one media header and H.264 envelope
are unchanged.

## Verify without a second machine

```sh
ctest --test-dir build-resume --output-on-failure
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-resume-sanitize --output-on-failure
python3 benchmarks/adaptive.py build-resume
python3 benchmarks/adaptive.py build-resume --desktop
```

The proxy runs the production sender and receiver through actual loopback UDP.
It adds 20 ms of return-path delay, drops one packet in every third frame during
the loss phase, restores delivery, then blocks feedback for about two seconds.
It also injects wrong-session, foreign-endpoint, and replayed replies. The run
fails unless it observes bitrate reduction, subsequent increase, feedback-timeout
fallback, rejected invalid replies, decoded recovery, and the injected RTT delay.
The optional desktop mode requires every decoded frame to be presented.

`rate-control` checks wire parsing, exact controller timing, rate bounds, RTT
congestion, silence fallback, and rejection of stale or regressing reports.
`feedback-client` checks the reply bytes independently in Python, no response
before endpoint pinning, exclusion of control traffic from media counters, and
response rate limiting. `bitrate` changes targets within one live encoder and
decodes every output frame, measuring 60 frames after each 30-frame warmup.
CLI checks reject invalid rates, duplicated options, and unsupported backends.

These checks prove application behavior under the specified impairments. They
do not reproduce Tailscale routing, NAT behavior, or cross-machine scheduling.
The separate [bandwidth benchmark](bandwidth.md) adds a finite-capacity FIFO
model. Two-machine and live-capture acceptance results are recorded below.

## Measurements on 2026-09-24

These initial measurements precede packet pacing and the one-frame VBV change.
See [the bandwidth measurements](bandwidth.md#measurements-on-2026-09-24) for the
updated sender and its regression checks.

All 21 tests passed in Release and under ASan, LSan, and UBSan on the Intel
laptop. After tightening interval-counter validation, the controller and full
adaptive transfer checks passed again in both builds. A capture-disabled,
render-disabled Release build also passed its codec-option, controller,
feedback-client, and adaptive-transfer checks.

The [Release proxy run](../benchmarks/results/milestone-seven-adaptive.log)
sent 240 frames and deliberately lost 20 video packets plus eight feedback
replies. The receiver decoded 220 frames with zero corruption. Targets ranged
from 2,000 down to 391 kbps, with increases during clean periods and reductions
during the feedback outage.

A separate [sanitized desktop run](../benchmarks/results/milestone-seven-adaptive-desktop-sanitize.log)
decoded and presented all 220 surviving frames with zero corruption and no
sanitizer diagnostics. Targets ranged from 510 to 2,000 kbps in that run.
Mean presentation time was 7,670 microseconds, with a maximum of 18,224
microseconds. Scheduling changes feedback interval boundaries, so the two
runs need not take identical bitrate steps.

The [encoder measurement](../benchmarks/results/milestone-seven-bitrate.log)
produced 989,210 bytes at a 4,000 kbps target, 64,779 bytes after changing to
256 kbps, and 777,837 bytes after restoring 4,000 kbps. Each count covers 60
frames after 30 warmup frames. All 270 frames decoded successfully. This
verifies rate changes within one encoder; it does not guarantee exact target
bitrate for arbitrary content.

## Two-machine Tailscale acceptance on 2026-10-03

The adaptive benchmark can run the production sender on a second machine.
The local proxy binds to the local Tailscale IPv4 address. Media and feedback
travel over Tailscale between that proxy and the remote sender. The proxy
forwards accepted media into the local receiver and applies the same controlled
loss, delay, replay, and feedback-outage phases as the loopback check.
Remote runs send 360 frames and leave four seconds for recovery between the
loss and feedback-outage phases, followed by three seconds after the outage.
Loopback runs retain their 240-frame sequence.

```sh
python3 benchmarks/adaptive.py build-release --desktop \
  --bind LOCAL_TAILSCALE_IP --remote user@SENDER_TAILSCALE_IP \
  --remote-build /path/to/sender/build-release
```

The [initial Release network run](../benchmarks/results/milestone-seven-tailscale-release-short.log)
passed. The sender encoded 240 frames. The proxy dropped 20 media packets
and eight feedback replies, and the receiver decoded and presented all 220
surviving frames with zero corruption. Thirty feedback replies reached the
proxy. The sender reduced bitrate during loss and the feedback outage,
increased it during clean periods, and rejected injected invalid replies.
Targets ranged from 391 through 2,000 kbps.

The first [sanitized remote run](../benchmarks/results/milestone-seven-tailscale-sanitize-short.log)
failed the bitrate-increase check. Additional network loss prevented four
consecutive clean feedback intervals within the short recovery windows.
The benchmark now gives remote runs longer recovery windows while retaining
the same required reduction, increase, outage, RTT, invalid-feedback, and
decoded-video checks. The
[extended sanitized run](../benchmarks/results/milestone-seven-tailscale-sanitize.log)
passed with all 340 surviving frames decoded and presented, zero corruption,
20 intentionally dropped media packets, seven dropped feedback replies, and
45 replies received by the proxy. Targets ranged from 463 through 2,000 kbps.
No sanitizer diagnostics were emitted.

The [extended Release run](../benchmarks/results/milestone-seven-tailscale-release.log)
also passed. It decoded and presented all 340 surviving frames with zero
corruption, after the proxy dropped 20 media packets and eight feedback
replies. The proxy received 45 replies, and targets ranged from 320 through
2,000 kbps.

The [source manifest](../benchmarks/results/tailscale-acceptance-source.log)
records the remote source archive hash, toolchain, libraries, and build options.
The remote build used an isolated temporary directory and did not modify the
existing checkout on that machine.

A separate live PipeWire capture sent 120 frames at 15 FPS over eight seconds
to a desktop window on the second machine, starting at 2,000 kbps. All 120
frames decoded and were presented, with zero loss or corruption. Bidirectional
feedback arrived throughout the transfer. The
[capture log](../benchmarks/results/milestone-seven-tailscale-capture.log)
records the encoder, controller, and receiver results without desktop images.

The local Release build passed all 25 tests. The second machine's
capture-disabled Release build passed all 23 tests; see the
[remote test log](../benchmarks/results/tailscale-remote-release-tests.log).
All 25 local tests also passed under ASan, LSan, and UBSan with leak detection
enabled; see the [sanitizer test log](../benchmarks/results/tailscale-local-sanitize-tests.log).
These checks complete milestone 7's network and capture acceptance. Milestone
12 also completes optional session authentication and recovery across process
restarts. See [authenticated sessions](sessions.md) and
[the final acceptance results](session-verification.md).
