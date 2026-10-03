---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-01
---

# Screen capture

Linux capture crosses two boundaries. The desktop portal grants permission and identifies a screen stream. PipeWire delivers the pixels for that stream. UDP does not participate in either step.

## Permission before pixels

`PortalSession` uses the session D-Bus connection and the desktop ScreenCast portal. It creates a session, requests a monitor source, starts sharing through the desktop chooser, and opens the PipeWire remote. The resulting node ID and file descriptor connect capture to the selected source.

The chooser is part of capture startup. The host's streaming duration starts after selection. A receiver started earlier can expire while you are choosing a monitor, so allow time for startup.

`open_screen_capture` returns the Linux implementation through the `ScreenCapture` interface. `next(rgb, timeout)` fills caller-owned RGB storage and returns dimensions and a local receipt timestamp. `statistics` reports received, superseded, and malformed captures.

## Latest pixels rather than a backlog

`PipeWireCapture` owns its portal, loop, context, core, stream, and listener. Its process callback dequeues up to four available buffers and returns earlier ones, retaining the latest candidate for conversion. This deliberately discards old capture data when several buffers are ready.

The callback borrows a PipeWire buffer. A local RAII guard returns it to the stream even on an early exit. Retaining its pixel pointer after that return would violate the loan's lifetime.

Accepted input is packed 8-bit BGRx, BGRA, RGBx, or RGBA, at most 4096 by 2160. Negotiation requests mapped memory through MemPtr or MemFd. GPU modifiers and unsupported layouts are rejected. The code enforces at most four buffers and a 64 MiB limit per buffer.

Before conversion, the callback checks data blocks, offsets, sizes, positive stride, corruption flags, and crop metadata. Row stride can exceed visible row bytes, so pixel traversal must use the supplied layout rather than assume tightly packed input.

`capture/pixels.hpp` converts the supported layouts into a bounded RGB preview. The preview fits within 320 by 180 and preserves aspect ratio. Alpha or unused fourth channels do not enter the RGB output.

## Two ways to send the preview

Raw mode adds a 20-byte `LRGB` envelope to the RGB bytes. It includes version and format, width, height, and CRC32. A 320 by 180 RGB picture occupies 172,800 pixel bytes before that header.

H.264 mode passes RGB to the encoder described in [LARP - H264 codec and recovery](LARP%20-%20H264%20codec%20and%20recovery.md). A dimensions change replaces the encoder, so allocation and initialization at that moment differ from steady-state frame work.

Both modes use the same UDP transport. The transport's 4 MiB ceiling still applies even though raw media validation allows dimensions up to 4096 by 2160.

## The PipeWire context leak fix

The recorded capture investigation found leak reports from PipeWire's unused realtime module. The fix sets the context property `module.rt=false` before context creation. LARP drives a non-realtime loop, so the module was unnecessary for this implementation.

This is a context-local setting. It does not disable realtime behavior globally or reconfigure the desktop's PipeWire daemon. `pipewire_context_test.cpp` exercises the construction path without requiring the screen chooser.

Sources: [capture interface](../../capture/capture.hpp), [portal](../../capture/linux/portal.cpp), [PipeWire implementation](../../capture/linux/pipewire_capture.cpp), [pixel conversion](../../capture/pixels.hpp), [context fix](../../capture/linux/context.hpp), [raw envelope](../../media/raw_frame.hpp), [capture evidence](../capture.md). The context fix is commit `cf8cd28`.
