# Protocol version 1

All integers use unsigned big-endian encoding. The header occupies exactly 34 bytes. No padding or native C++ object representation appears on the wire.

| Offset | Bytes | Field | Value or meaning |
| --- | --- | --- | --- |
| 0 | 4 | magic | `0x4c415250`, ASCII `LARP` |
| 4 | 2 | version | 1 |
| 6 | 8 | frame_id | Increasing within one sender session |
| 14 | 4 | packet_index | Zero-based index within the frame |
| 18 | 4 | packet_count | Number of packets in this frame |
| 22 | 8 | timestamp_us | Sender monotonic time at frame generation |
| 30 | 4 | payload_size | Exact number of bytes following the header |

The UDP payload is at most 1,200 bytes, leaving at most 1,166 bytes for frame data. Every nonfinal packet has exactly 1,166 payload bytes. The final packet has 1 through 1,166 bytes. Empty frames are unsupported. Frames are at most 4,194,304 bytes, or 3,598 packets. The last packet's offset plus length must fit that limit.

The receiver rejects wrong magic or version, short or oversized datagrams, length mismatches, zero or excessive counts, invalid indexes, and inconsistent frame timestamps or counts. Duplicate indexes never increase the received count. The first accepted payload for an index wins. Completed synthetic frames receive a separate byte-content check.

The synthetic byte at offset `i` is `(frame_id + i) mod 256`. This catches ordinary corruption and reassembly errors. It is not a cryptographic checksum. The pattern repeats every 256 frame IDs.

Frame IDs do not wrap within a session. Sender and receiver timestamps have unrelated origins. The protocol does not estimate one-way network latency or RTT.

## Receiver behavior

The receiver keeps one frame. A greater frame ID replaces an incomplete frame. Lower IDs and packets for completed or expired frames are stale. Expiry occurs 100,000 microseconds after the first packet's local receipt and does not extend on duplicates. The application checks expiry between receives, with a 10 ms poll timeout during silence. Scheduling delays and frame processing can delay expiry beyond the deadline.

A completed frame is a borrowed span into the receiver's buffer. The caller consumes it before the next `accept` call. Shutdown counts any active incomplete frame as dropped.

The client pins the source endpoint on the first structurally valid datagram and ignores other endpoints afterward. Restarting the host requires restarting the client. No authentication or session reset is implemented.

## Statistics

| Label | Meaning |
| --- | --- |
| FPS | Validated frames per second since the previous report |
| Packets | Datagrams from the selected endpoint, including malformed packets; before selection, all datagrams |
| Packet loss | Cumulative missing packets divided by advertised packet counts in observed frames |
| Dropped | Observed incomplete frames discarded on replacement, expiry, or shutdown |
| Missing | Unreceived packet indexes in discarded frames |
| Skipped | Frame-ID gaps after the first observed frame |
| Throughput | Received UDP payload bytes, including LARP headers, per reporting interval in decimal Mbps |
| Validated | Completed frames with the expected synthetic contents |
| Corrupt | Completed frames whose synthetic contents differ |
| Invalid | Malformed datagrams or conflicting frame metadata |
| Duplicates | Repeated indexes in the active frame |
| Stale | Packets rejected because their frame is older, completed, or expired |
| Foreign | Datagrams from another endpoint after source selection |
| RTT | N/A |

Loss excludes wholly unseen frames, loss before the first observed frame, and loss after the final observed frame. Advertised counts include the active frame, so loss may be understated until that frame completes or expires. Skipped and corrupt frames are separate from dropped frames. A sequence number is the pair of frame ID and packet index.

## Command arguments

`larp-host IPv4 PORT FRAMES BYTES FPS`

| Argument | Range |
| --- | --- |
| PORT | 1 through 65535 |
| FRAMES | 1 through 1000000000 |
| BYTES | 1 through 4194304 |
| FPS | 1 through 1000 |

`larp-client BIND_IPv4 PORT SECONDS`

| Argument | Range |
| --- | --- |
| PORT | 0 through 65535, with 0 selecting an ephemeral port |
| SECONDS | 1 through 86400 |

Both commands accept IPv4 literals only and return a nonzero exit status on argument or socket errors. The host exits on send failure, including socket pressure or an MTU error. There is no send retry queue.
