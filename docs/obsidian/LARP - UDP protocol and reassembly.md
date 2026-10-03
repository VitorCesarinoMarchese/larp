---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-03
---

# UDP protocol and reassembly

UDP delivers datagrams, not frames. A large frame must be split, and its pieces can arrive out of order, arrive twice, or never arrive. LARP defines its own framing and accepts a frame only when every required packet is present.

## Two layers of framing

The 34-byte transport header describes one fragment. The reconstructed payload can contain a separate raw RGB or H.264 media envelope. Media metadata belongs to the whole frame, not to every packet.

All transport integers are unsigned and big-endian. `encode` and `decode` serialize fields explicitly. They never transmit native C++ struct memory, which could contain padding or machine-specific byte order.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | Magic `LARP` |
| 4 | 2 | Version 1 |
| 6 | 8 | Frame ID |
| 14 | 4 | Packet index |
| 18 | 4 | Packet count |
| 22 | 8 | Sender monotonic timestamp in microseconds |
| 30 | 4 | Payload size |

The maximum application datagram is 1,200 bytes. In plaintext mode this is also the maximum UDP payload. Each packet therefore carries at most `1200 - 34 = 1166` frame bytes. Every nonfinal packet must use that full capacity. A frame has at most 4,194,304 bytes and 3,598 packets.

For a 3,000-byte frame, the payloads are 1,166, 1,166, and 668 bytes. Their offsets are 0, 1,166, and 2,332. The plaintext wire UDP payload sizes are 1,200, 1,200, and 702 bytes. Secure mode adds 48 bytes to each datagram, making those sizes 1,248, 1,248, and 750 bytes. The inner header and fragment offsets are unchanged.

## The receiver remembers one frame

`Reassembler` owns one 4 MiB byte buffer and a fixed seen-index bitmap. It does not allocate per incoming packet.

1. `accept` first checks expiry using local time.
2. `decode` rejects invalid sizes, magic, version, counts, indexes, and payload layouts.
3. An older frame ID is stale. The current ID is also stale after completion or expiry.
4. A newer ID drops any incomplete current frame, records intervening skipped IDs, clears the bitmap, and starts a new frame.
5. A packet for the active frame must match its count and timestamp.
6. The first accepted payload for each index is copied to its final offset. Duplicates do not advance completion.
7. Once all indexes arrive, the receiver returns a span over the assembled bytes.

A newer frame wins even if an older frame was almost complete. Packets within one frame can reorder freely. Reordering across frame boundaries can cause avoidable loss. This is the documented cost of a single active buffer.

A returned span borrows the receiver's buffer. Consume it before the next `accept` call can overwrite that storage.

## Deadlines and session boundaries

The default deadline is 100 ms after the first packet's local receipt. Duplicates do not extend it. Expiry is checked between receives, so scheduling and synchronous processing can delay the actual discard.

The transport timestamp comes from the sender's monotonic clock. It cannot be subtracted from the receiver's unrelated monotonic clock to measure network delay.

Plaintext mode pins the first structurally valid media source IP and port. A host restart usually creates a different ephemeral port and resets its frame IDs, so that mode still needs a new client for each host session. Pinning is accounting and filtering, not authentication.

Secure mode requires a shared key and an expected sender IP. A completed handshake selects the authenticated source endpoint. Fresh session generations allow new frame IDs and source ports without restarting the surviving process. `Reassembler::reset_session` discards any partial frame as superseded, clears the previous ID boundary, and preserves cumulative counters. See [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md).

## Loss counters answer different questions

Observed packet loss is `missing / expected`. Only completed or discarded observed frames enter that denominator. An entirely unseen frame has an unknown packet count.

Frame loss is `(dropped + skipped) / (completed + dropped + skipped)`. ID gaps contribute here even when none of their packets arrived. The first observed ID does not establish how many frames were sent before it.

Example: frame 10 completes with three packets. Frame 11 receives two of three packets and expires. Frame 12 is unseen. Frame 13 completes with three packets. Observed packet loss is `1 / 9`, about 11.1%. Frame loss is `2 / 4`, or 50%.

`Dropped = Expired + Superseded + Shutdown`. A complete but corrupt media frame contributes to `completed` and is reported separately as `Corrupt`.

## Socket policy

`UdpSocket` owns a nonblocking Linux socket and closes it through RAII. It requests 256 KiB kernel send and receive buffers. These requests are not process-RSS limits or guarantees of the exact kernel allocation.

`MSG_TRUNC` detects oversized incoming datagrams. Send errors propagate rather than entering a retry queue. IPv4 path MTU discovery forbids fragmentation. A path that cannot carry the packet produces an error; there is no MTU negotiation.

Sources: [packet layout](../../protocol/packet.hpp), [reassembler](../../transport/reassembly.hpp), [Linux socket implementation](../../platform/udp_linux.cpp), [protocol reference](../protocol.md), [documented buffering tradeoff](../design.md).
