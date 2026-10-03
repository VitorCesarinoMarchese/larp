"""Verify live H.264 recovery over real loopback UDP; pass --desktop for a window."""
import argparse
import os
import pathlib
import re
import selectors
import socket
import struct
import subprocess
import tempfile
import time

HEADER = struct.Struct('!IHQIIQI')


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def capture_frames(build):
    # Collect a bounded fixture from the production software encoder.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sink, tempfile.TemporaryFile() as log:
        sink.bind(('127.0.0.1', 0))
        sink.settimeout(5)
        host = subprocess.Popen(
            [str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1',
             str(sink.getsockname()[1]), '3', '10'], stdout=log, stderr=log)
        frames = {}
        try:
            deadline = time.monotonic() + 5
            while len(frames) < 3 or any(len(parts) != count for count, parts in frames.values()):
                require(time.monotonic() < deadline, 'encoder fixture timed out')
                packet = sink.recv(1201)
                require(HEADER.size <= len(packet) <= 1200, 'invalid encoder datagram size')
                magic, version, frame, index, count, timestamp, size = HEADER.unpack(packet[:34])
                require(magic == 0x4c415250 and version == 1 and 1 <= frame <= 3
                        and 0 <= index < count <= 225 and len(packet) == 34 + size,
                        'invalid encoder packet header')
                expected, parts = frames.setdefault(frame, (count, {}))
                require(expected == count, 'encoder packet count changed')
                parts[index] = packet[34:]
            require(host.wait(timeout=5) == 0, 'encoder fixture failed')
            return [[parts[index] for index in range(count)]
                    for count, parts in (frames[frame] for frame in range(1, 4))]
        except Exception:
            stop(host)
            log.seek(0)
            print(log.read().decode(errors='replace'))
            raise
        finally:
            stop(host)


def run(build, desktop):
    frames = capture_frames(build)
    require(len(frames[1]) > 1 and len(frames[2]) > 1, 'fixture must use fragmented frames')
    environment = dict(os.environ)
    if not desktop:
        environment['SDL_VIDEO_DRIVER'] = 'dummy'
    with tempfile.TemporaryFile(mode='w+') as errors:
        client = subprocess.Popen(
            [str(build / 'larp-client'), '--view-h264', '127.0.0.1', '0', '3', '50'],
            stdout=subprocess.PIPE, stderr=errors, text=True, env=environment)
        try:
            with selectors.DefaultSelector() as ready:
                ready.register(client.stdout, selectors.EVENT_READ)
                require(bool(ready.select(timeout=5)), 'receiver readiness timed out')
            line = client.stdout.readline()
            require(line.startswith('Listening: '), f'receiver did not start: {line}')
            destination = ('127.0.0.1', int(line.split()[-1]))
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                def packet(frame_id, parts, index, channel=sender):
                    body = parts[index]
                    channel.sendto(HEADER.pack(0x4c415250, 1, frame_id, index,
                                              len(parts), 0, len(body)) + body, destination)

                def frame(frame_id, parts):
                    for index in range(len(parts)):
                        packet(frame_id, parts, index)
                    # Keep independent frames apart even during sanitizer runs.
                    time.sleep(0.05)

                frame(1, frames[0])
                # Reorder within frame 2 and duplicate before its final missing part.
                for index in reversed(range(1, len(frames[1]))):
                    packet(2, frames[1], index)
                packet(2, frames[1], len(frames[1]) - 1)
                packet(2, frames[1], 0)
                time.sleep(0.05)
                # Withhold one fragment until well beyond the receiver's 50 ms deadline.
                for index in range(1, len(frames[2])):
                    packet(3, frames[2], index)
                time.sleep(0.25)
                packet(3, frames[2], 0)
                # Frame 4 is entirely lost. Frame 5 arrives intact but fails its CRC.
                corrupt = list(frames[2])
                corrupt[-1] = corrupt[-1][:-1] + bytes([corrupt[-1][-1] ^ 1])
                frame(5, corrupt)
                frame(6, frames[2])
                # Endpoint pinning must reject even a complete, newer foreign frame.
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
                    packet(1000, frames[0], 0, foreign)
                # Resume the same session after a pause without restarting the client.
                time.sleep(0.25)
                frame(7, frames[1])
            output, _ = client.communicate(timeout=6)
            print(line + output, end='')
            require(client.returncode == 0, f'receiver exited with {client.returncode}')
            expected = {'Validated': 4, 'Presented': 4, 'Corrupt': 1, 'Dropped': 1,
                        'Missing': 1, 'Expired': 1, 'Superseded': 0, 'Shutdown': 0,
                        'Skipped': 1, 'Duplicates': 1, 'Stale': 1, 'Foreign': 1, 'Invalid': 0}
            for label, value in expected.items():
                matches = re.findall(rf'\b{label}: (\d+)', output)
                require(bool(matches) and int(matches[-1]) == value,
                        f'expected final {label}: {value}; got {matches}')
            print('PASS: live preview recovered after loss, expiry, corruption, and sender pause')
        finally:
            stop(client)
            errors.seek(0)
            diagnostics = errors.read()
            if diagnostics:
                print(diagnostics, end='')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--desktop', action='store_true', help='use the actual desktop instead of SDL dummy video')
    args = parser.parse_args()
    run(args.build.resolve(), args.desktop)
