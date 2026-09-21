# NVIDIA 615.71.09 CUDA profile and NVENC teardown leaks

Draft for NVIDIA, not submitted. Reproduced on 2026-09-21 from L.A.R.P.
commit `3fd4e0f`. The two small probes do not link L.A.R.P.'s codec code.

## Environment

- Arch Linux, NVIDIA GeForce GTX 1660 Ti.
- NVIDIA kernel module and user libraries: 615.71.09.
- glibc: `2.44+r24+g16be1518495f-1`.
- GCC: `16.2.1+r23+gd564253eb6c8-1`.
- FFmpeg: `2:9.0.1-4`.
- CUDA library build ID: `bc95112d022d2c8bd1a723ed309d4331b21c76bc`.

Full stacks, running kernel version, commands, and exit statuses are in the
[recheck log](../benchmarks/results/nvidia-recheck-2026-09-21.log).

## Build the isolated probes

From the repository or extracted reproduction archive root, run:

```sh
mkdir -p build-repro
c++ -std=c++23 -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
  benchmarks/cuda_init.cpp -ldl -o build-repro/cuda-init
c++ -std=c++23 -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I. benchmarks/nvenc_sessions.cpp $(pkg-config --cflags --libs libavcodec libavutil) \
  -ldl -o build-repro/nvenc-sessions
export ASAN_OPTIONS=detect_leaks=1:protect_shadow_gap=0
```

The CUDA probe requires no CUDA SDK or FFmpeg. The NVENC probe requires
FFmpeg development headers and libraries with NVENC support. Its only local
header, `common/runtime.hpp`, supplies numeric argument parsing.

`protect_shadow_gap=0` allows CUDA initialization on this installation.
Leak detection stays enabled. No leak suppressions or library preloads are used.

## CUDA profile initialization

```sh
./build-repro/cuda-init
```

The probe loads `libcuda.so.1`, calls `cuInit(0)`, and closes the library.
`cuInit` returns 0. LeakSanitizer reports 183 bytes in four allocations and
the process exits with status 1.

With Python 3 and bubblewrap installed, compare profile fixtures:

```sh
python3 benchmarks/nvidia_profiles.py \
  /usr/share/nvidia/nvidia-application-profiles-615.71.09-rc \
  ./build-repro/cuda-init
```

The script overlays each fixture in a private mount namespace. It does not
modify the installed profile file.

| Profile fixture | Leaked bytes | Allocations | Exit |
| --- | ---: | ---: | ---: |
| Installed file | 183 | 4 | 1 |
| Installed file without `CudaNoStablePerfLimit` definition | 0 reported | 0 reported | 0 |
| Only `CudaNoStablePerfLimit`, no rules | 183 | 4 | 1 |
| Same minimal definition renamed `LarpLeakProbe` | 0 reported | 0 reported | 0 |

Every fixture returns success from `cuInit`. The script itself exits 1 because
two cases fail leak detection. The earlier investigation identified the strings
`CudaNoStablePerfLimit` and `0x166c5e` in the allocations. These observations
suggest that replacing a built-in profile loses its previous allocation.
The exact ownership error requires confirmation in the driver source.

## NVENC library teardown

Run each command separately. Each returns status 1 after reporting leaks:

```sh
./build-repro/nvenc-sessions 1
./build-repro/nvenc-sessions 10
./build-repro/nvenc-sessions 10 hold
./build-repro/nvenc-sessions 10 drain
```

The probe opens and frees empty FFmpeg `h264_nvenc` contexts. It submits no
image frames. `hold` retains one library handle until all contexts are freed,
then explicitly closes it. `drain` sends end-of-stream and verifies EOF before
freeing each context.

| Sessions and mode | Leaked bytes | Allocations |
| --- | ---: | ---: |
| 1, normal | 279 | 6 |
| 10, normal | 1,143 | 24 |
| 10, hold | 279 | 6 |
| 10, drain | 1,143 | 24 |

These totals include the independent 183-byte CUDA profile leak. Subtracting
that baseline gives 96 bytes per normal library lifecycle. Holding the library
reduces ten sessions to one 96-byte leak at final unload. Draining does not
change the result. The additional stacks include 48-byte allocations through
`pthread_cond_broadcast`, matching the compatibility-function allocation
investigated in the [earlier notes](nvidia-leak-investigation.md).

A [2025 report on NVIDIA's forum](https://forums.developer.nvidia.com/t/memory-leak-in-nvencopenencodesessionex/254738/4)
shows a similar 48-byte allocation with direct NVENC calls on driver 570.172.08.
That report uses a different GPU and an OpenGL device. It does not establish
that these failures have the same cause. The older 32-byte issue discussed
earlier in that thread was reported fixed in release 495.

## Expected result and request

After the probes release their contexts and close their library handles,
LeakSanitizer should report no leaked allocations. Please investigate profile
replacement and encoder-library teardown, and identify a driver version that
fixes these reproductions or any missing cleanup required by the public APIs.

The application passes functional hardware tests, but these failures block its
no-leaks acceptance requirement. No alternate driver has been tested on this
machine. A clean result requires both the isolated probes and the application's
hardware sanitizer tests to pass.
