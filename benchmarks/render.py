"""Measure a local software H.264 transfer into a live window and sample receiver RSS."""
import argparse
import math
import pathlib
import re
import shlex
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=pathlib.Path)
parser.add_argument('--frames', type=int, default=900)
parser.add_argument('--fps', type=int, default=30)
parser.add_argument('--bind', default='127.0.0.1', help='Receiver IPv4 address; use its Tailscale IP for a remote sender')
parser.add_argument('--remote', help='SSH destination for the software sender')
parser.add_argument('--remote-build', help='Existing build directory on the SSH destination')
args = parser.parse_args()
if bool(args.remote) != bool(args.remote_build):
    parser.error('--remote and --remote-build must be supplied together')
if args.remote and args.bind == '127.0.0.1':
    parser.error('a remote sender requires --bind with a reachable receiver address')
if args.frames < 1 or not 1 <= args.fps <= 30:
    parser.error('frames must be positive and FPS must be between 1 and 30')
build = args.build.resolve()
seconds = math.ceil(args.frames / args.fps) + (10 if args.remote else 3)
if seconds > 86400:
    parser.error('run must fit the client duration limit of 86400 seconds')

with tempfile.TemporaryDirectory(prefix='larp-render-') as temporary:
    path = pathlib.Path(temporary)
    with (path / 'client.log').open('w+') as client_log, (path / 'host.log').open('w+') as host_log:
        client = subprocess.Popen(
            [str(build / 'larp-client'), '--view-h264', args.bind, '0', str(seconds)],
            stdout=client_log, stderr=subprocess.STDOUT)
        host = None
        try:
            deadline = time.monotonic() + 10
            while True:
                text = (path / 'client.log').read_text()
                ready = re.search(r'^Listening: (\d+)$', text, re.MULTILINE)
                if ready:
                    break
                if client.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError(f'Client did not start: {text}')
                time.sleep(0.02)
            print(f'Window process: {client.pid}', flush=True)
            sender_build = pathlib.Path(args.remote_build) if args.remote else build
            command = [str(sender_build / 'larp-host'), '--h264-synthetic', args.bind, ready[1],
                       str(args.frames), str(args.fps)]
            if args.remote:
                command = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                           args.remote, shlex.join(command)]
            host = subprocess.Popen(command, stdout=host_log, stderr=subprocess.STDOUT)
            start = time.monotonic()
            count = 0
            first = last = minimum = maximum = None
            while client.poll() is None:
                if time.monotonic() - start > seconds + 10:
                    raise RuntimeError('Client did not finish')
                if time.monotonic() - start >= 2:
                    try:
                        status = pathlib.Path(f'/proc/{client.pid}/status').read_text()
                    except FileNotFoundError:
                        status = ''
                    rss = re.search(r'^VmRSS:\s+(\d+)', status, re.MULTILINE)
                    if rss:
                        last = int(rss[1])
                        first = last if first is None else first
                        minimum = last if minimum is None else min(minimum, last)
                        maximum = last if maximum is None else max(maximum, last)
                        count += 1
                time.sleep(0.2)
            host.wait(timeout=10)
            output = (path / 'client.log').read_text()
            print((path / 'host.log').read_text(), end='')
            print(output, end='')
            print(f'RSS KiB: samples={count} min={minimum} max={maximum} first={first} last={last}')
            if client.returncode or host.returncode:
                raise RuntimeError(f'Process failure: client={client.returncode}, host={host.returncode}')
            reports = [line for line in output.splitlines() if line.startswith('FPS:')]
            if not reports or not all(token in reports[-1] for token in (
                    f'Validated: {args.frames} ', f'Presented: {args.frames} ', 'Corrupt: 0 ')):
                raise RuntimeError('Transfer did not present every frame cleanly')
        finally:
            for process in (host, client):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
