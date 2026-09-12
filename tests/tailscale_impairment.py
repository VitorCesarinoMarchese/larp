"""Acceptance check: python3 tests/tailscale_impairment.py SSH_TARGET REMOTE_BUILD TAILSCALE_IP.

Run against an already built remote client. Uses one socket and a fixed packet
sequence. External network loss can fail this check; exact timing is unit-tested.
"""
import pathlib
import shlex
import socket
import struct
import subprocess
import sys
import time

if len(sys.argv) != 4:
    raise SystemExit(__doc__)
target, build, address = sys.argv[1:]
command = shlex.join([str(pathlib.PurePosixPath(build) / 'larp-client'), address, '0', '3'])
client = subprocess.Popen(['timeout', '10', 'tailscale', 'ssh', target, command],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    for line in client.stdout:
        if line.startswith('Listening: '):
            port = int(line.split()[-1])
            break
    else:
        raise RuntimeError('remote client did not report readiness')

    def packet(frame, index):
        length = 1166 if index == 0 else 3
        payload = bytes((frame + index * 1166 + i) % 256 for i in range(length))
        return struct.pack('!IHQIIQI', 0x4c415250, 1, frame, index, 2, 77, length) + payload

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
        for frame, index in [(1, 1), (1, 1), (1, 0), (2, 0), (3, 1), (3, 0), (4, 0)]:
            sender.sendto(packet(frame, index), (address, port))
        time.sleep(0.3)
        for frame, index in [(4, 1), (6, 1), (6, 0)]:
            sender.sendto(packet(frame, index), (address, port))
    output, error = client.communicate(timeout=10)
    assert client.returncode == 0, error
    final = output.splitlines()[-1]
    print(final)
    for expected in ['Validated: 3 ', 'Corrupt: 0 ', 'Dropped: 2 ', 'Missing: 2 ',
                     'Skipped: 1 ', 'Duplicates: 1 ', 'Stale: 1 ', 'Expired: 1 ',
                     'Superseded: 1 ', 'Shutdown: 0 ', 'Frame loss: 50.00%',
                     'Observed packet loss: 20.00%']:
        assert expected in final, (expected, final)
    print('Tailscale loss, expiry, and recovery checks passed')
finally:
    if client.poll() is None:
        client.terminate()
        client.wait(timeout=3)
