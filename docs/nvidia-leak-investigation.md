# NVIDIA leak investigation

Investigated on 2026-09-14 with NVIDIA 615.71.09, GTX 1660 Ti, glibc
2.44, GCC 16.2.1 and FFmpeg 9.0.1. The CUDA library build ID is
`bc95112d022d2c8bd1a723ed309d4331b21c76bc`.

Two independent triggers account for the reports. Neither requires L.A.R.P.'s
codec implementation. No production workaround or sanitizer suppression was
added, and milestone 5 remains incomplete.

## CUDA application profile replacement

Calling `cuInit(0)` alone reports 183 bytes: a 56-byte object, a 96-byte
allocation, and strings allocated with sizes 22 and 9. A manual LeakSanitizer
check reports the same allocations while CUDA is loaded, after `dlclose`, and
after a two-second delay. Repeating initialization 100 times does not increase
the total.

Inspection of the objects identified the strings `CudaNoStablePerfLimit` and
`0x166c5e`. Disassembly and debugger breakpoints show the application profile
parser handling that profile name twice during initialization. The installed
NVIDIA profile file contains one definition of it.

The following experiment changes the profile file only inside a private mount
namespace. It requires the optional `bubblewrap` diagnostic tool; L.A.R.P.
does not depend on it. The real profile file remains untouched.

```sh
python3 benchmarks/nvidia_profiles.py \
  /usr/share/nvidia/nvidia-application-profiles-615.71.09-rc \
  ./build-sanitize/larp-cuda-init
```

| Profile visible to the probe | LeakSanitizer result | Process exit |
| --- | --- | --- |
| Original installed file | 183 bytes, 4 allocations | 1 |
| Original with only that profile definition removed | No reported leak | 0 |
| Only that profile definition, no rules | 183 bytes, 4 allocations | 1 |
| Same minimal definition renamed `LarpLeakProbe` | No reported leak | 0 |

Every case returns success from `cuInit`. The experiment runner returns 1 if
any probe fails, preserving the sanitizer failures. These results isolate a
profile-name collision. They strongly suggest that replacing a built-in
profile loses its previous allocation; the driver's closed source prevents
confirming the exact ownership error. Setting `__GL_APPLICATION_PROFILE=0`
does not remove the CUDA leak on this installation.

Removing the profile system-wide is not an application fix. The shipped file
uses it for OBS and Discord, and changing vendor profiles could change their
behavior.

## Encoder library unload

The remaining allocations are 48-byte condition variables allocated through
`pthread_cond_broadcast@GLIBC_2.2.5`. Inspection of the installed glibc confirms
that this compatibility function lazily allocates 48 bytes and its matching
destroy function frees that allocation.

The FFmpeg-only session probe now accepts two experimental modes:

```sh
ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0 ./build-sanitize/larp-nvenc-sessions 10
ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0 ./build-sanitize/larp-nvenc-sessions 10 hold
ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0 ./build-sanitize/larp-nvenc-sessions 10 drain
```

`hold` retains one RAII library handle until every session has been freed,
then closes it. `drain` sends end-of-stream and checks that the empty encoder
returns EOF before freeing each session.

| Mode, original profiles | 1 session | 10 sessions |
| --- | --- | --- |
| Normal open/free | 279 bytes / 6 allocations | 1,143 bytes / 24 allocations |
| Hold library until all sessions finish | 279 bytes / 6 allocations | 279 bytes / 6 allocations |
| Drain before free | 279 bytes / 6 allocations | 1,143 bytes / 24 allocations |

Subtract the independent 183-byte CUDA profile leak: retaining the library
reduces ten sessions from 960 leaked bytes to 96. Repeating the normal and
held-library probes with the filtered profile file gives those totals directly.
The growth therefore follows library load/unload cycles in this probe, rather
than every encoder session while the library stays loaded.

Preloading `libnvidia-encode.so.1` for the process lifetime also removes the
48-byte reports, leaving the 183-byte profile leak. That alone does not prove
cleanup: keeping a library mapped can keep allocations reachable to LSan.
The explicit final `dlclose` in `hold` still exposes 96 leaked bytes. Draining
does not fix them.

NVIDIA's forum has a [report of the same compatibility-function allocation](https://forums.developer.nvidia.com/t/memory-leak-in-nvencopenencodesessionex/254738)
with a different driver and direct NVENC calls. It supports investigating the
vendor lifecycle implementation, but does not establish a fix for 615.71.09.

## Acceptance and next step

These probes narrow the failures to profile replacement and encoder-library
teardown. They do not justify suppressing leak detection, deleting system
profiles, or making NVIDIA libraries permanent globals in L.A.R.P.

The next useful step is to reproduce these cases on another supported driver
version, or submit the isolated reproductions to NVIDIA. No driver change or
external bug report was made. A driver change on this desktop should be a
separate task because it affects the running graphics session.
