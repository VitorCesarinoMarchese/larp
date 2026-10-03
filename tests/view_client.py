import os
import pathlib
import re
import selectors
import socket
import struct
import subprocess
import sys
import zlib

build = pathlib.Path(sys.argv[1]).resolve()
environment = dict(os.environ, SDL_VIDEO_DRIVER='dummy')
clients = []


def start(mode, *options):
    client = subprocess.Popen(
        [str(build / 'larp-client'), mode, '127.0.0.1', '0', '2', *options],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=environment)
    clients.append(client)
    with selectors.DefaultSelector() as ready:
        ready.register(client.stdout, selectors.EVENT_READ)
        assert ready.select(timeout=3), 'client did not announce listening port'
    line = client.stdout.readline()
    assert line.startswith('Listening: '), (line, client.communicate(timeout=3))
    return client, int(line.split()[-1])


def count(output, label):
    values = re.findall(rf'{label}: (\d+)', output)
    assert values, (label, output)
    return int(values[-1])


try:
    raw_client, raw_port = start('--view-raw', '20')
    h264_client, h264_port = start('--view-h264', '--codec', 'software')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        for frame_id, width, height, corrupt in [(1, 3, 2, False), (2, 1, 1, False),
                                                (3, 1, 1, True)]:
            pixels = bytes([20, 80, 190]) * width * height
            prefix = struct.pack('!IHHII', 0x4c524742, 1, 1, width, height)
            body = prefix + struct.pack('!I', zlib.crc32(prefix + pixels)) + pixels
            if corrupt:
                body = body[:-1] + bytes([body[-1] ^ 255])
            packet = struct.pack('!IHQIIQI', 0x4c415250, 1, frame_id, 0, 1, 0, len(body)) + body
            sender.sendto(packet, ('127.0.0.1', raw_port))
    host = subprocess.run(
        [str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1', str(h264_port),
         '10', '10', '--codec', 'software'], capture_output=True, text=True,
        timeout=4, env=environment)
    assert host.returncode == 0, (host.stdout, host.stderr)
    for client, expected_valid, expected_corrupt in [(raw_client, 2, 1), (h264_client, 10, 0)]:
        output, error = client.communicate(timeout=4)
        assert client.returncode == 0, (output, error)
        assert count(output, 'Validated') == expected_valid, output
        assert count(output, 'Presented') == expected_valid, output
        assert count(output, 'Corrupt') == expected_corrupt, output
        assert 'Snapshot:' not in output, output

    for arguments in [
        ['--view-raw'],
        ['--view-raw', '127.0.0.1', '0', '1', '0'],
        ['--view-h264', '127.0.0.1', '0', '1', '--codec', 'invalid'],
        ['--view-raw', '127.0.0.1', '0', '1', 'unwanted.ppm'],
    ]:
        result = subprocess.run([str(build / 'larp-client'), *arguments],
                                capture_output=True, text=True, timeout=2, env=environment)
        assert result.returncode != 0 and result.stderr, result
        assert 'Listening:' not in result.stdout, result.stdout

    missing_display = dict(environment, SDL_VIDEO_DRIVER='larp-nonexistent-video-driver')
    result = subprocess.run(
        [str(build / 'larp-client'), '--view-raw', '127.0.0.1', '0', '1'],
        capture_output=True, text=True, timeout=2, env=missing_display)
    assert result.returncode != 0 and 'SDL:' in result.stderr, result
    assert 'Listening:' not in result.stdout, result.stdout
finally:
    for client in clients:
        if client.poll() is None:
            client.kill()
            client.communicate(timeout=2)
