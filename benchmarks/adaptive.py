"""Verify adaptive software video over UDP with loss and feedback outages."""
import argparse
import heapq
import pathlib
import re
import shlex
import socket
import struct
import subprocess
import tempfile
import time

MEDIA = struct.Struct('!IHQIIQI')
CONTROL = struct.Struct('!IHHQQQ')
REPLY = struct.Struct('!IHHQQQQ7Q')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def run(build, desktop, bind='127.0.0.1', remote=None, remote_build=None):
    frames = 360 if remote else 240
    outage_start, outage_end = (210, 270) if remote else (150, 210)
    with tempfile.TemporaryDirectory(prefix='larp-adaptive-') as temporary:
        directory = pathlib.Path(temporary)
        client_path, host_path = directory / 'client.log', directory / 'host.log'
        with client_path.open('w') as client_log, host_path.open('w') as host_log, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as proxy:
            proxy.bind((bind, 0))
            proxy.settimeout(0.005)
            mode = ['--view-h264'] if desktop else ['--h264']
            command = [str(build / 'larp-client'), *mode, '127.0.0.1', '0',
                       '18' if remote else '11']
            if not desktop:
                command.append(str(directory / 'decoded.ppm'))
            client = subprocess.Popen(command, stdout=client_log, stderr=subprocess.STDOUT)
            host = None
            try:
                deadline = time.monotonic() + 5
                while True:
                    output = client_path.read_text()
                    ready = re.search(r'^Listening: (\d+)$', output, re.MULTILINE)
                    if ready:
                        break
                    require(client.poll() is None and time.monotonic() < deadline,
                            f'receiver did not start: {output}')
                    time.sleep(0.01)
                receiver = ('127.0.0.1', int(ready[1]))
                sender_build = pathlib.Path(remote_build) if remote else build
                command = [str(sender_build / 'larp-host'), '--h264-synthetic', bind,
                           str(proxy.getsockname()[1]), str(frames), '30', '--adaptive', '2000']
                if remote:
                    command = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                               remote, shlex.join(command)]
                host = subprocess.Popen(command, stdout=host_log, stderr=subprocess.STDOUT)
                sender = None
                latest_frame = 0
                queue = []
                queued = 0
                injected = False
                missing_packets = lost_reports = responses = 0
                deadline = time.monotonic() + (23 if remote else 15)
                while client.poll() is None:
                    require(time.monotonic() < deadline, 'adaptive transfer timed out')
                    now = time.monotonic()
                    while queue and queue[0][0] <= now:
                        _, _, data, destination = heapq.heappop(queue)
                        proxy.sendto(data, destination)
                    try:
                        data, source = proxy.recvfrom(1201)
                    except socket.timeout:
                        continue
                    if source == receiver:
                        require(sender is not None and len(data) == REPLY.size, 'invalid feedback reply size')
                        values = REPLY.unpack(data)
                        require(values[:3] == (0x4c414642, 1, 2), 'invalid feedback header')
                        responses += 1
                        if outage_start < latest_frame <= outage_end:
                            lost_reports += 1
                            continue
                        if not injected:
                            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as foreign:
                                foreign.sendto(data, sender)
                            bad = bytearray(data)
                            bad[8] ^= 1  # Matching endpoint, wrong session.
                            proxy.sendto(bad, sender)
                            injected = True
                        queued += 1
                        heapq.heappush(queue, (now + 0.020, queued, data, sender))
                        queued += 1
                        heapq.heappush(queue, (now + 0.025, queued, data, sender))  # Replay.
                    else:
                        if sender is None:
                            sender = source
                        require(source == sender, 'unexpected proxy sender')
                        if data[:4] == b'LAFB':
                            require(len(data) == CONTROL.size, 'invalid probe size')
                            proxy.sendto(data, receiver)
                        else:
                            require(len(data) >= MEDIA.size, 'short media packet')
                            magic, version, frame, index, count, timestamp, size = MEDIA.unpack(data[:34])
                            require(magic == 0x4c415250 and version == 1 and len(data) == 34 + size,
                                    'invalid media header')
                            latest_frame = max(latest_frame, frame)
                            if 30 < frame <= 90 and frame % 3 == 0 and index == 0:
                                missing_packets += 1
                                continue
                            proxy.sendto(data, receiver)
                host.wait(timeout=3)
                host_output, client_output = host_path.read_text(), client_path.read_text()
                print(host_output, end='')
                print(client_output, end='')
                require(host.returncode == 0 and client.returncode == 0, 'host or receiver failed')
                reports = re.findall(r'Feedback: (\d+).*?RTT us: (\d+).*?Target kbps: (\d+).*?Feedback age ms: (\d+).*?Rejected feedback: (\d+)', host_output)
                require(len(reports) >= 12, 'too few feedback reports')
                rates = [int(row[2]) for row in reports]
                require(min(rates) < 1500, f'loss did not reduce target: {rates}')
                require(any(b > a for a, b in zip(rates, rates[1:])), f'clean path did not recover: {rates}')
                require(any(int(row[3]) >= 1000 for row in reports), 'feedback outage did not trigger fallback')
                measured_rtt = [int(row[1]) for row in reports if int(row[0]) > 0]
                require(min(measured_rtt) >= 19000, f'RTT omitted injected return delay: {measured_rtt}')
                require(int(reports[-1][4]) >= 3, 'foreign, wrong-session, or replay feedback not rejected')
                validated = int(re.findall(r'Validated: (\d+)', client_output)[-1])
                require(frames // 2 < validated < frames and 'Corrupt: 0 ' in client_output,
                        f'video did not recover cleanly: {validated}')
                if desktop:
                    require(int(re.findall(r'Presented: (\d+)', client_output)[-1]) == validated,
                            'valid frames were not presented')
                else:
                    snapshot = (directory / 'decoded.ppm').read_bytes()
                    prefix = b'P6\n320 180\n255\n'
                    require(snapshot.startswith(prefix) and len(snapshot) == len(prefix) + 320 * 180 * 3,
                            'invalid decoded snapshot')
                require(missing_packets == 20 and lost_reports >= 4, 'impairment phases were not exercised')
                print(f'PASS: dropped media packets={missing_packets}; dropped feedback={lost_reports}; '
                      f'feedback replies={responses}; target range={min(rates)}..{max(rates)} kbps')
            finally:
                stop(host)
                stop(client)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--desktop', action='store_true')
    parser.add_argument('--bind', default='127.0.0.1', help='Proxy IPv4; use the local Tailscale IP for a remote sender')
    parser.add_argument('--remote', help='SSH destination for the software sender')
    parser.add_argument('--remote-build', help='Existing build directory on the SSH destination')
    args = parser.parse_args()
    if bool(args.remote) != bool(args.remote_build):
        parser.error('--remote and --remote-build must be supplied together')
    if args.remote and args.bind == '127.0.0.1':
        parser.error('a remote sender requires --bind with a reachable proxy address')
    run(args.build.resolve(), args.desktop, args.bind, args.remote, args.remote_build)
