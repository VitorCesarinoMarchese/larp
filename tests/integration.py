import pathlib
import socket
import struct
import subprocess
import sys

build = pathlib.Path(sys.argv[1]).resolve()

def client():
    process = subprocess.Popen([str(build / 'larp-client'), '127.0.0.1', '0', '2'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    line = process.stdout.readline()
    assert line.startswith('Listening: '), (line, process.communicate())
    return process, int(line.split()[-1])

def finish(process):
    out, err = process.communicate(timeout=5)
    assert process.returncode == 0, (out, err)
    return out

process, port = client()
subprocess.run([str(build / 'larp-host'), '127.0.0.1', str(port), '20', '16384', '60'], check=True)
out = finish(process)
assert 'Validated: 20 ' in out, out
assert 'Corrupt: 0 ' in out, out

process, port = client()
sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
def packet(frame, index, count=2, corrupt=False):
    length = 1166 if index + 1 < count else 3
    payload = bytes((frame + index * 1166 + i) % 256 for i in range(length))
    if corrupt:
        payload = b'\xff' * length
    return struct.pack('!IHQIIQI', 0x4c415250, 1, frame, index, count, 77, length) + payload
for data in [b'bad', b'x' * 1201, packet(1, 1), packet(1, 1), packet(1, 0),
             packet(2, 0), packet(3, 1), packet(3, 0, corrupt=True), packet(4, 0)]:
    sender.sendto(data, ('127.0.0.1', port))
foreign = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
foreign.sendto(packet(999, 0), ('127.0.0.1', port))
out = finish(process)
for expected in ['Validated: 1 ', 'Corrupt: 1 ', 'Dropped: 2 ', 'Missing: 2 ', 'Invalid: 2 ', 'Duplicates: 1 ', 'Foreign: 1 ']:
    assert expected in out, (expected, out)
for executable, args in [('larp-host', ['127.0.0.1', '9999', '1', '0', '60']),
                         ('larp-client', ['127.0.0.1', '9999', '-1'])]:
    assert subprocess.run([str(build / executable), *args], capture_output=True).returncode != 0
print('localhost and injected impairment checks passed')
