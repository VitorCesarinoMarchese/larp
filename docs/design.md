# Transport decisions

The first milestone uses a single thread per process and direct Linux UDP sockets. There are no worker queues. `platform/udp.hpp` exposes endpoints and spans without Linux socket types. A Windows implementation can replace `platform/udp_linux.cpp` without changing the wire format or reassembler.

The receiver allocates one 4 MiB frame buffer at construction, plus a fixed bitmap of 3,598 entries. Receiving, parsing, and reassembly do not allocate. The host allocates one configured-size frame buffer and reuses a 1,200-byte datagram buffer. Both processes request 256 KiB send and receive socket buffers. Linux may clamp these requests and accounts additional kernel overhead separately from process RSS.

One active frame makes memory independent of sender frame IDs and packet counts. It also means reordered packets across frame boundaries can cause avoidable drops. A bounded multi-frame window could preserve more frames, but it would need evidence that the extra memory and retained frame age help the stream. Within a frame, packets can arrive in any order.

The receiver copies each accepted payload once into its final offset. The host generates a contiguous synthetic frame, then copies each chunk into its outgoing datagram. Scatter-gather I/O could remove the host copy later. At the initial measured rate, that change is not justified yet.

Datagrams contain at most 1,200 bytes of UDP payload. Linux path MTU discovery forbids IPv4 fragmentation. A path with an MTU below the datagram plus IP and UDP headers causes a send error instead of silent fragmentation. This first version has no MTU negotiation.

The host sends each frame as a burst and uses a steady-clock deadline for the next frame. If it falls behind, it does not queue missed deadlines. Large frames can overrun the bounded receive socket buffer, especially on slower machines or links. Packet pacing is a candidate for a measured follow-up, not a claim of this milestone.

The 100 ms timeout uses local receipt time. Comparing remote monotonic timestamps would produce invalid latency measurements. The timeout bounds how long an incomplete frame remains useful, but it cannot distinguish a delayed packet already queued in the network from a fresh packet.

`capture`, `codec`, and `render` reserve the requested directory structure. They have no interfaces or dependencies yet. PipeWire, FFmpeg, hardware acceleration, rendering, control messages, adaptive streaming, and Windows implementations remain outside this milestone.
