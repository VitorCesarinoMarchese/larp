---
tags:
  - larp
  - technical-study
project: ../..
reviewed: 2026-10-03
---

# Rendering and ownership

The receiver displays raw RGB and decoded H.264 through the same `VideoWindow` interface. The current implementation uses SDL3 CPU window surfaces, with X11 or Xwayland as the documented desktop path.

## Copy before the source changes

`RawFrameView` contains dimensions and a borrowed pixel span. In raw mode the pixels point into the reassembler's storage. In H.264 mode they point into the client's reusable decode buffer.

`VideoWindow::present` copies every row into its owned RGB24 surface before returning. Later receive or decode operations can overwrite the source without changing the retained picture.

The row copy respects SDL's surface pitch. Pitch is the number of bytes between row starts, which can include padding. A flat copy that assumes identical source and destination layout could misplace rows.

The renderer retains only the latest copied picture. If traffic stops or a corrupt frame arrives, the last valid picture stays visible.

## What a redraw does

The window begins at 960 by 540 and supports resizing and high pixel density. `redraw` reacquires the window surface, fills it black, computes an aspect-preserving destination rectangle, scales with nearest-neighbor sampling, and updates the surface.

Reacquiring matters because resizing can invalidate SDL's window surface. The source RGB surface changes only when received frame dimensions change.

`poll` handles close, Escape, exposure, and pixel-size changes. Close and Escape stop reception. Exposure and resize redraw the retained picture without counting a newly received frame.

## Why presentation is synchronous

The documented design comparison chose a synchronous receiver window over a receive worker plus latest-frame mailbox. Keeping receipt, decoding, borrowed-buffer consumption, and shutdown on one thread avoids another ownership handoff.

Direct Wayland shared-memory buffers were considered, but would add configure, release, and resize handling. SDL supplied the needed window behavior through X11. CPU surfaces were selected to avoid initializing a GPU renderer for small CPU previews.

The process disables SDL framebuffer acceleration and defaults to X11 unless an explicit SDL driver selection is present. It does not change system drivers or desktop settings. Selecting another driver does not establish that the documented rendering path supports native Wayland.

The cost is that surface copying, scaling, and updating block the next receive. A larger stream or slower renderer could fill the socket queue. A worker would need measured benefit to justify its additional state.

## What the measurements mean

`Presented` counts successful presentation calls for received frames. `Present mean us` and `Present max us` include the RGB copy, scaling, and SDL update call. They do not include monitor scanout and do not measure capture-to-display latency.

Window buffers depend on window pixel size. The retained RGB surface is bounded by accepted input dimensions. Transport's 4 MiB limit is tighter than the renderer's maximum raw dimensions, and current H.264 is limited to 320 by 180.

Release and sanitizer results must be interpreted separately. Milestone-six two-machine Tailscale acceptance is complete. The October 3 Release run decoded and presented all 900 frames at 30 FPS, with zero loss or corruption and receiver RSS fixed at 40,984 KiB across 190 samples. Two additional 300-frame sanitizer runs presented 277 and 294 frames respectively; their losses remain recorded rather than being described as perfect delivery.

The later encrypted restart checks use SDL dummy displays and synthetic video, so they can run with the screen locked. They verify presentation calls and recovery, not physical window appearance or monitor scanout. See [LARP - Session security and recovery](LARP%20-%20Session%20security%20and%20recovery.md).

Sources: [window API](../../render/window.hpp), [SDL implementation](../../render/window_sdl.cpp), [receiver ownership](../../client/main.cpp), [rendering comparison and measurements](../rendering.md), [recovery tests](../../tests/view_recovery.py).
