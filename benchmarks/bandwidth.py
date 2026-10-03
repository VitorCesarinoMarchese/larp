"""Run real adaptive video through a bounded, variable-capacity loopback link."""
import argparse
from collections import deque
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time

from adaptive import require, stop, REPLY


class BandwidthLink:
    """FIFO serialization with tail drop; capacity counts IPv4 + UDP bytes."""
    def __init__(self, phases, queue_bytes):
        self.phases = phases
        self.queue_limit = queue_bytes
        self.queue = deque()
        self.queued = 0.0
        self.time = 0.0
        self.dropped = 0
        self.delivered_bytes = 0
        self.maximum_queue = 0.0

    def phase(self, now):
        for end, bps in self.phases:
            if now < end:
                return end, bps
        return float('inf'), self.phases[-1][1]

    def advance(self, now):
        require(now >= self.time, 'link clock moved backwards')
        completed = []
        while self.time < now:
            end, bps = self.phase(self.time)
            until = min(now, end)
            budget = (until - self.time) * bps / 8
            while self.queue and budget >= self.queue[0][1]:
                data, remaining = self.queue.popleft()
                budget -= remaining
                self.queued -= remaining
                self.delivered_bytes += len(data) + 28
                completed.append(data)
            if self.queue:
                self.queue[0][1] -= budget
                self.queued -= budget
            else:
                self.queued = 0.0  # Idle time never becomes future burst credit.
            self.time = until
        return completed

    def enqueue(self, data):
        size = len(data) + 28
        if self.queued + size > self.queue_limit:
            self.dropped += 1
            return False
        self.queue.append([data, float(size)])
        self.queued += size
        self.maximum_queue = max(self.maximum_queue, self.queued)
        return True


def rss(pid):
    try:
        status = pathlib.Path(f'/proc/{pid}/status').read_text()
    except (FileNotFoundError, ProcessLookupError):
        return None
    value = re.search(r'^VmRSS:\s+(\d+)', status, re.MULTILINE)
    return int(value[1]) if value else None


def cpu_seconds(pid):
    try:
        fields = pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
    except (FileNotFoundError, ProcessLookupError):
        return None
    return (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')


def memory_summary(samples):
    result = {}
    for name in ('host', 'client'):
        values = [sample[f'{name}_rss_kib'] for sample in samples
                  if sample['seconds'] >= 5 and sample[f'{name}_rss_kib'] is not None]
        require(len(values) >= 5, f'too few {name} RSS samples after warmup')
        result[name] = dict(samples=len(values), first_kib=values[0], last_kib=values[-1],
                            min_kib=min(values), max_kib=max(values),
                            growth_kib=values[-1] - values[0])
    return result


def verify_cycles(samples, cycles):
    for cycle in range(cycles):
        base = cycle * 16
        startup = [s for s in samples if base <= s['seconds'] < base + 3]
        require(startup and sum(s['drops'] for s in startup) == 0,
                f'cycle {cycle + 1}: startup burst exceeded available link capacity')
        low = [s for s in samples if base + 7 <= s['seconds'] < base + 10]
        recovered = [s for s in samples if base + 13 <= s['seconds'] < base + 16]
        require(len(low) >= 2 and len(recovered) >= 2, 'missing phase samples')
        require(sum(s['target_kbps'] for s in low) / len(low) <= 880,
                f'cycle {cycle + 1}: target did not settle near low capacity')
        require(max(s['delivered_kbps'] for s in low) <= 820,
                f'cycle {cycle + 1}: proxy exceeded its capacity budget')
        require(sum(s['drops'] for s in low) <= 3,
                f'cycle {cycle + 1}: continued drops after low-capacity settling')
        recovery_start = next(s for s in samples if s['seconds'] >= base + 10)
        require(recovered[-1]['target_kbps'] >= recovery_start['target_kbps'] + 128,
                f'cycle {cycle + 1}: target did not recover after capacity returned')
        require(recovered[-1]['decoded'] - recovered[0]['decoded'] >= 50,
                f'cycle {cycle + 1}: decoding did not recover with capacity')


def run(build, queue_bytes, output, cycles=1, cpu_load=False, max_rss_growth=None):
    phases = [(cycle * 16 + end, rate) for cycle in range(cycles)
              for end, rate in [(3.0, 4000000), (10.0, 800000), (16.0, 4000000)]]
    duration = cycles * 16
    frames = duration * 30
    link = BandwidthLink(phases, queue_bytes)
    samples = []
    with tempfile.TemporaryDirectory(prefix='larp-bandwidth-') as temporary:
        directory = pathlib.Path(temporary)
        host_path, client_path = directory / 'host.log', directory / 'client.log'
        with host_path.open('w') as host_log, client_path.open('w') as client_log, \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as proxy:
            proxy.bind(('127.0.0.1', 0))
            proxy.settimeout(0.001)
            client = subprocess.Popen([str(build / 'larp-client'), '--h264', '127.0.0.1', '0',
                                       str(duration + 5), str(directory / 'frame.ppm')],
                                      stdout=client_log, stderr=subprocess.STDOUT)
            host = load = None
            load_cpu = None
            try:
                deadline = time.monotonic() + 5
                while True:
                    match = re.search(r'^Listening: (\d+)$', client_path.read_text(), re.MULTILINE)
                    if match:
                        break
                    require(client.poll() is None and time.monotonic() < deadline,
                            'client did not become ready')
                    time.sleep(0.01)
                receiver = ('127.0.0.1', int(match[1]))
                started = time.monotonic()
                host = subprocess.Popen([str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1',
                                         str(proxy.getsockname()[1]), str(frames), '30', '--adaptive', '2000'],
                                        stdout=host_log, stderr=subprocess.STDOUT)
                if cpu_load:
                    load_cpu = min(os.sched_getaffinity(0))
                    os.sched_setaffinity(host.pid, {load_cpu})
                    worker = (f'import os; os.sched_setaffinity(0, {{{load_cpu}}}); os.nice(10)\n'
                              'value = 1\nwhile True: value = (value * 1664525 + 1013904223) & 0xffffffff\n')
                    load = subprocess.Popen([sys.executable, '-c', worker],
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                sender = None
                next_sample = 1
                delivered = drops = 0
                sample_time = 0.0
                received_frames = corrupt = 0
                while client.poll() is None:
                    elapsed = time.monotonic() - started
                    require(elapsed < duration + 10, 'bandwidth run timed out')
                    for data in link.advance(elapsed):
                        proxy.sendto(data, receiver)
                    if elapsed >= next_sample:
                        rates = re.findall(r'Target kbps: (\d+)', host_path.read_text())
                        sample = dict(seconds=round(elapsed, 3), capacity_kbps=link.phase(elapsed)[1] // 1000,
                                      target_kbps=int(rates[-1]) if rates else 2000,
                                      delivered_kbps=round((link.delivered_bytes - delivered) * 8 / (elapsed - sample_time) / 1000, 1),
                                      drops=link.dropped - drops, decoded=received_frames, corrupt=corrupt,
                                      host_rss_kib=rss(host.pid), client_rss_kib=rss(client.pid),
                                      load_cpu_seconds=cpu_seconds(load.pid) if load else None)
                        samples.append(sample)
                        print(json.dumps(sample), flush=True)
                        delivered, drops = link.delivered_bytes, link.dropped
                        sample_time = elapsed
                        next_sample += 1
                    try:
                        data, source = proxy.recvfrom(1201)
                    except socket.timeout:
                        continue
                    if source == receiver:
                        require(sender is not None and len(data) == REPLY.size, 'invalid feedback')
                        fields = REPLY.unpack(data)
                        received_frames, corrupt = fields[9] - fields[12], fields[12]
                        proxy.sendto(data, sender)  # Reverse link is unconstrained.
                    else:
                        if sender is None:
                            sender = source
                        require(source == sender, 'unexpected sender')
                        # Probes share the constrained FIFO with media.
                        link.enqueue(data)
                sender_finished = host.poll() is not None
                host.wait(timeout=4)
                host_text, client_text = host_path.read_text(), client_path.read_text()
                memory = memory_summary(samples)
                result = dict(cycles=cycles, cpu_load=cpu_load, load_cpu=load_cpu, load_nice=10 if load else None,
                              asan_options=os.environ.get('ASAN_OPTIONS'),
                              sender_finished_before_receiver=sender_finished,
                              memory=memory, queue_bytes=queue_bytes, max_queue_bytes=round(link.maximum_queue, 1),
                              dropped_packets=link.dropped, samples=samples,
                              host=host_text, client=client_text)
                if output:
                    output.write_text(json.dumps(result, indent=2) + '\n')
                print(host_text, end='')
                print(client_text, end='')
                require(host.returncode == 0 and client.returncode == 0, 'stream process failed')
                require(sender_finished, 'sender outlasted the receiver under load')
                verify_cycles(samples, cycles)
                if load:
                    require(load.poll() is None, 'CPU load worker exited unexpectedly')
                    load_samples = [s for s in samples if s['load_cpu_seconds'] is not None]
                    active = load_samples[-1]['load_cpu_seconds'] - load_samples[0]['load_cpu_seconds']
                    wall = load_samples[-1]['seconds'] - load_samples[0]['seconds']
                    require(active / wall >= 0.5, 'CPU load worker did not sustain half a logical CPU')
                if max_rss_growth is not None:
                    for name, summary in memory.items():
                        require(summary['growth_kib'] <= max_rss_growth,
                                f'{name} RSS growth exceeded {max_rss_growth} KiB')
                final_corrupt = re.findall(r'Corrupt: (\d+)', client_text)
                require(final_corrupt and int(final_corrupt[-1]) == 0 and
                        all(s['corrupt'] == 0 for s in samples), 'corrupt decoded video')
                require(link.maximum_queue <= queue_bytes, 'unbounded link queue')
                require(f'Encoded: {frames} ' in host_text, 'sender did not finish every requested frame')
                print('RSS after 5-second warmup: ' + json.dumps(memory), flush=True)
                print(f'PASS: bitrate recovered through {cycles} capacity cycles; CPU load={cpu_load}')
            finally:
                stop(load)
                stop(host)
                stop(client)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--queue-bytes', type=int, default=8192)
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--cycles', type=int, default=1, help='repeat the 16-second capacity schedule, 1..30')
    parser.add_argument('--cpu-load', action='store_true', help='pin sender and a nice-10 CPU worker to one logical CPU')
    parser.add_argument('--max-rss-growth-kib', type=int, help='fail if either process grows beyond this RSS delta after warmup')
    args = parser.parse_args()
    if args.queue_bytes < 1228:
        parser.error('queue must hold at least one full datagram')
    if not 1 <= args.cycles <= 30:
        parser.error('cycles must be between 1 and 30')
    if args.max_rss_growth_kib is not None and args.max_rss_growth_kib < 0:
        parser.error('RSS growth limit must be nonnegative')
    run(args.build.resolve(), args.queue_bytes, args.output, args.cycles, args.cpu_load, args.max_rss_growth_kib)
