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

Frame IDs do not wrap within a session. Sender and receiver timestamps have unrelated origins. Media timestamps do not estimate one-way network latency. Opt-in H.264 adaptive
mode measures application RTT through separate probe replies; see
[the feedback protocol](adaptive.md#feedback-wire-format).

## Receiver behavior

The receiver keeps one frame. A greater frame ID replaces an incomplete frame. Lower IDs and packets for completed or expired frames are stale. By default, expiry occurs 100,000 microseconds after the first packet's local receipt and does not extend on duplicates. The client accepts a timeout from 1 through 1,000 ms. The application checks expiry between receives, with a poll timeout of the smaller of 10 ms and the configured frame timeout during silence. Scheduling delays and frame processing can delay expiry beyond the deadline.

A completed frame is a borrowed span into the receiver's buffer. The caller consumes it before the next `accept` call. Shutdown counts any active incomplete frame as dropped.

The client pins the source endpoint on the first structurally valid media datagram and ignores other endpoints afterward. Restarting the host requires restarting the client. No authentication or session reset is implemented.

## Statistics

| Label | Meaning |
| --- | --- |
| FPS | Validated frames per second since the previous report |
| Packets | Media datagrams from the selected endpoint, including malformed packets; before selection, all media datagrams. Recognized control messages are excluded |
| Observed packet loss | Cumulative missing packets divided by packet counts in completed or discarded observed frames; N/A until one finishes |
| Frame loss | Dropped plus skipped frames divided by completed, dropped, and skipped frames; N/A until a frame finishes or a gap appears |
| Dropped | Observed incomplete frames discarded on replacement, expiry, or shutdown |
| Missing | Unreceived packet indexes in discarded frames |
| Expired | Incomplete frames discarded at their receipt deadline |
| Superseded | Incomplete frames replaced by a newer frame before expiry |
| Shutdown | Incomplete frames discarded when the client exits |
| Skipped | Frame-ID gaps after the first observed frame |
| Throughput | Received media UDP payload bytes, including LARP headers, per reporting interval in decimal Mbps |
| Validated | Completed frames with expected synthetic contents or valid media |
| Corrupt | Completed frames that fail content validation or decoding |
| Invalid | Malformed datagrams or conflicting frame metadata |
| Duplicates | Repeated indexes in the active frame |
| Stale | Packets rejected because their frame is older, completed, or expired |
| Foreign | Datagrams from another endpoint after source selection |
| RTT | N/A in the receiver report; adaptive senders report probe RTT separately |

Loss excludes wholly unseen frames, loss before the first observed frame, and loss after the final observed frame. Active frames do not enter either denominator until they finish. Whole-frame gaps contribute to frame loss, but their unknown packet counts cannot contribute to observed packet loss. Corrupt frames count as completed for transport statistics and appear separately in Corrupt. Dropped equals Expired plus Superseded plus Shutdown. A sequence number is the pair of frame ID and packet index.

## Command arguments

`larp-host IPv4 PORT FRAMES BYTES FPS`

| Argument | Range |
| --- | --- |
| PORT | 1 through 65535 |
| FRAMES | 1 through 1000000000 |
| BYTES | 1 through 4194304 |
| FPS | 1 through 1000 |

`larp-client BIND_IPv4 PORT SECONDS [FRAME_TIMEOUT_MS]`

| Argument | Range |
| --- | --- |
| PORT | 0 through 65535, with 0 selecting an ephemeral port |
| SECONDS | 1 through 86400 |
| FRAME_TIMEOUT_MS | Optional, 1 through 1000; default 100 |

Both commands accept IPv4 literals only and return a nonzero exit status on argument or socket errors. The host exits on send failure, including socket pressure or an MTU error. There is no send retry queue.

## Authenticated session envelope

With `--key-file`, a session envelope wraps each original media or feedback
datagram. The original application formats remain unchanged. There is no
plaintext downgrade within a secure session. The sender accepts `--bind IPv4`;
the receiver requires `--peer IPv4` alongside its key path.

All multibyte integers are big-endian. The envelope uses libsodium's
[XChaCha20-Poly1305 AEAD](https://doc.libsodium.org/secret-key_cryptography/aead/chacha20-poly1305).
The first eight bytes are authenticated associated data.

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic `0x4c415345`, ASCII `LASE` |
| 4 | 2 | Version, 1 |
| 6 | 1 | Kind: hello 1, challenge 2, confirm 3, ready 4, application 5, ping 6, pong 7 |
| 7 | 1 | Sending role: sender 0, receiver 1 |
| 8 | 24 | Nonce |
| 32 | Variable | Encrypted body followed by a 16-byte authentication tag |

An application body is at most 1,200 bytes. The envelope adds 48 bytes, for a
maximum UDP payload of 1,248 bytes. IPv4 and UDP headers bring the total to
1,276 bytes, within a 1,280-byte Tailscale MTU.

Handshake and heartbeat messages use the shared 32-byte key and a fresh random
24-byte nonce on each transmission. The hello body contains a random 16-byte
sender challenge. The receiver adds its own random 16-byte challenge. Challenge,
confirm, and ready bodies contain both challenges, in that order. Only a
matching confirm from the expected source address and exact candidate endpoint
can establish a receiver session. Repeated confirms receive ready replies
without resetting an established session.

Keyed BLAKE2b hashes the two challenges into a 32-byte session base key.
Libsodium's [key derivation API](https://libsodium.gitbook.io/doc/key_derivation)
derives direction-specific keys with context `LARPv001`: subkey 1 for sender
to receiver, subkey 2 for receiver to sender. Application nonces contain the
sending peer's 16-byte challenge followed by a nonzero 64-bit counter.
Counters never repeat within a session. Exhaustion initiates a fresh sender
session or fails a receiver send before reuse.

A 1,024-counter replay window allows bounded packet reordering and rejects
duplicates, zero counters, and old counters. The window advances only after
successful authentication. Exact source endpoints and nonce prefixes must
match the active session. Invalid or oversized envelopes never reach media
reassembly, decoding, endpoint selection, or feedback parsing.

The sender retries handshake messages every 250 ms. Candidates expire after
three seconds, and sender retries then use a new challenge. Initial connection
has a five-second deadline. Established senders request a heartbeat every
250 ms after the previous reply. Each ping contains both session challenges
and a fresh 16-byte request challenge. A matching pong echoes the 48-byte body
plus one byte indicating whether the receiver still has that session. Missing
replies for one second, or a valid negative reply, start a fresh handshake.
Recorded pongs cannot satisfy a new request challenge.

The receiver permits a replacement handshake only after one second without
authenticated application traffic. New generations clear partial reassembly
and decoder state, while retaining aggregate statistics. Adaptive feedback
starts a fresh counter baseline at the current bitrate. No video or feedback
queue grows during reconnection: the sender retains at most the latest
decrypted feedback datagram, and media attempts without an active session are
discarded and counted as `Unsent`.

This protocol provides shared-key authentication, encryption, and replay
rejection. It has no forward secrecy. Possession of the pair's shared key
allows either role to be impersonated and recorded sessions to be decrypted.
The implementation has automated negative tests but no independent security
audit. Key setup and operational commands are in [the session guide](sessions.md).
