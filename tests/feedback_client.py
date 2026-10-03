"""Check receiver feedback wire fields, endpoint pinning, and response rate limiting."""
import pathlib
import selectors
import socket
import struct
import subprocess
import sys
import tempfile
import time

build = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as temporary:
    client = subprocess.Popen(
        [str(build / 'larp-client'), '--h264', '127.0.0.1', '0', '2',
         str(pathlib.Path(temporary) / 'unused.ppm')],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(client.stdout, selectors.EVENT_READ)
            assert ready.select(timeout=3), 'receiver readiness timeout'
        line = client.stdout.readline()
        assert line.startswith('Listening:'), line
        destination = ('127.0.0.1', int(line.split()[-1]))
        probe = struct.pack('!IHHQQQ', 0x4c414642, 1, 1, 12345, 1, 9000)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
            sender.settimeout(0.15)
            foreign.settimeout(0.15)
            def quiet(channel):
                try:
                    packet = channel.recv(1200)
                except socket.timeout:
                    return
                raise AssertionError(f'unexpected feedback: {packet}')
            sender.sendto(probe, destination)
            quiet(sender)  # A probe must not pin a peer or elicit a pre-session response.
            packet = struct.pack('!IHQIIQI', 0x4c415250, 1, 1, 0, 1, 0, 1) + b'x'
            sender.sendto(packet, destination)
            sender.sendto(probe, destination)
            data = sender.recv(1200)
            fields = struct.unpack('!IHHQQQQ7Q', data)
            assert fields[:6] == (0x4c414642, 1, 2, 12345, 1, 9000), fields
            assert fields[6] > 0, fields
            assert fields[7:] == (1, 0, 1, 0, 0, 1, 35), fields
            sender.sendto(probe, destination)
            quiet(sender)  # Back-to-back probes cannot amplify replies.
            foreign.sendto(probe, destination)
            quiet(foreign)
            sender.sendto(probe[:-1], destination)
            quiet(sender)
            sender.sendto(probe, destination)
            assert len(sender.recv(1200)) == 96
        out, err = client.communicate(timeout=4)
        assert client.returncode == 0, (out, err)
        assert 'Packets: 1 ' in out and 'Corrupt: 1 ' in out, out
    finally:
        if client.poll() is None:
            client.kill()
            client.communicate(timeout=3)
