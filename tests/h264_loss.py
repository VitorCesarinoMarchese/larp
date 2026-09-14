"""Exercise real encoded access units through reorder, loss, corruption, and recovery."""
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import zlib

build = pathlib.Path(sys.argv[1]).resolve()
header = struct.Struct('!IHQIIQI')
frames = {}
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sink:
    sink.bind(('127.0.0.1', 0))
    sink.settimeout(5)
    host = subprocess.Popen([str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1',
                             str(sink.getsockname()[1]), '3', '10'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        while len(frames) < 3 or any(len(parts) != count for count, parts in frames.values()):
            packet = sink.recv(1200)
            magic, version, frame, index, count, timestamp, size = header.unpack(packet[:34])
            assert magic == 0x4c415250 and version == 1 and len(packet) == 34 + size
            frames.setdefault(frame, (count, {}))[1][index] = packet[34:]
        assert host.wait(timeout=5) == 0, host.communicate()
    finally:
        if host.poll() is None:
            host.terminate()
            host.wait()

first = b''.join(frames[1][1][i] for i in range(frames[1][0]))
assert struct.unpack('!IHHII', first[:16]) == (0x4c483236, 1, 1, 320, 180)
assert zlib.crc32(first[:16] + first[20:]) == struct.unpack('!I', first[16:20])[0]
if len(sys.argv) > 2:
    independent = subprocess.run([sys.argv[2], '-v', 'error', '-f', 'h264', '-i', 'pipe:0',
                                  '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'],
                                 input=first[20:], capture_output=True, timeout=5, check=True)
    assert len(independent.stdout) == 320 * 180 * 3
    assert max(abs(value - 90) for value in independent.stdout) <= 4

with tempfile.TemporaryDirectory() as temporary:
    client = subprocess.Popen([str(build / 'larp-client'), '--h264', '127.0.0.1', '0', '2',
                               str(pathlib.Path(temporary) / 'decoded.ppm')],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        line = client.stdout.readline()
        assert line.startswith('Listening: '), (line, client.communicate())
        destination = ('127.0.0.1', int(line.split()[-1]))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            for frame, source in [(1, 1), (2, 2), (3, 3), (4, 1), (5, 3)]:
                count, parts = frames[source]
                if frame == 2:
                    assert count > 1
                for index in reversed(range(count)):
                    if frame == 2 and index == 0:
                        continue
                    body = parts[index]
                    if frame == 4:
                        body = body[:-1] + bytes([body[-1] ^ 1])
                    sender.sendto(header.pack(0x4c415250, 1, frame, index, count, 0, len(body)) + body,
                                  destination)
        out, err = client.communicate(timeout=5)
        assert client.returncode == 0, (out, err)
        assert 'Validated: 3 ' in out and 'Corrupt: 1 ' in out, out
        assert 'Dropped: 1 frames Missing: 1 ' in out, out
    finally:
        if client.poll() is None:
            client.terminate()
            client.wait()
