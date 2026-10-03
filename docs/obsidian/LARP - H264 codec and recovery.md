---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-03
---

# H.264 codec and recovery

Compression reduces the bytes sent for each preview. It also introduces media dependencies and decoder state. LARP's current profile limits those dependencies so losing a frame does not require waiting for a later keyframe.

## An independently decodable picture

The fixed-quality software encoder uses FFmpeg's `libx264` wrapper with YUV420P, CRF 23, `ultrafast`, `zerolatency`, one thread, no B-frames, and a GOP of one. GOP means group of pictures. Here every picture is an IDR picture, which resets reference dependencies.

Every access unit includes its own SPS and PPS parameter sets. SPS describes sequence properties such as dimensions. PPS describes picture coding parameters. An IDR picture without those headers would not be enough for a receiver joining fresh.

The documented choice favors immediate recovery and a testable baseline. Inter-frame compression could use less bandwidth, but a lost reference picture can damage later pictures until recovery. LARP spends more bandwidth to make each preview independent.

## RGB in, YUV inside, RGB out

The public encoder takes caller-owned RGB input and encoded output spans. Its private implementation owns FFmpeg contexts and conversion objects through RAII.

`libswscale` converts RGB to YUV420P. That format shares chroma samples across pairs of pixels, so dimensions must be even. Odd input dimensions scale down to the nearest even dimensions. The current envelope restricts output to even dimensions from 2 by 2 through 320 by 180.

The decoder converts validated YUV420P back to RGB. H.264 is lossy. A decoded pixel need not match the source byte exactly, even when delivery is perfect.

This API expects exactly one encoded packet immediately per input picture and exactly one decoded image per access unit. It is a constrained preview codec, not a general H.264 file player.

## Validate before calling the decoder

An encoded transport frame begins with a 20-byte envelope. It contains `LH26`, version 1, independent-preview profile 1, width, height, and CRC32, followed by Annex B H.264 bytes. Annex B separates network abstraction layer units with start codes.

CRC32 covers the envelope's first 16 bytes and compressed data, excluding the checksum field. It detects accidental corruption. It does not authenticate the sender.

The decoder validates the envelope and compressed structure, requires SPS and PPS, rejects inter-frame pictures and multiple IDR units, limits the access unit to eight NAL units, and flushes codec state before each picture. It then checks dimensions, pixel format, and decoder corruption reports before returning RGB.

Why both flushing and checking headers? Flushing alone did not prove independence: a regression found that FFmpeg could reuse parameter sets from an earlier frame. Explicitly requiring each frame's own SPS and PPS prevents that hidden dependency. This motivation is recorded in `docs/software-h264.md`.

## Memory limits and recovery

The encoded envelope plus access unit is limited to 256 KiB. The client owns a reusable 172,800-byte RGB output buffer. The decoder owns padded compressed input storage, while transport retains its separate 4 MiB allocation.

FFmpeg owns additional allocations. Application buffer sizes therefore do not specify total process RSS. Encoded packet storage can be allocated and freed per frame even though application buffers are reusable.

Incomplete transport frames never enter decoding. A complete but invalid access unit fails media validation. The next valid independent frame can decode immediately. There is no decoder retry queue or video retransmission.

An authenticated session change also recreates `H264Decoder` after the first generation and resets partial transport state. The reusable RGB output buffer and cumulative statistics remain. This prevents decoder and frame-ID state from crossing process restarts. See [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md).

## Adaptive and NVIDIA paths

Adaptive software mode replaces CRF quality control with a bitrate target, matching VBV maximum, and a one-frame VBV buffer. Updating the target reconfigures the existing encoder rather than rebuilding it for each report. See [LARP - Adaptive bitrate and pacing](LARP%20-%20Adaptive%20bitrate%20and%20pacing.md).

`--codec nvidia` selects hardware backends explicitly. Encoding and decoding can use different backends on different machines while keeping the same media profile. Functional tests exist, but the recorded NVIDIA leak blocker prevents claiming accepted hardware acceleration. Rendering still receives CPU RGB, including after NVIDIA decode.

Sources: [codec API](../../codec/h264.hpp), [codec implementation](../../codec/h264.cpp), [software codec decisions and evidence](../software-h264.md), [NVIDIA status](../nvidia.md), [codec tests](../../tests/h264_test.cpp). Software support arrived in `1ca24c4`; NVIDIA support arrived in `05a53c0`.
