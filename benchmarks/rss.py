"""Sample one command's RSS every 200 ms after two seconds, without retaining samples."""
import pathlib
import subprocess
import sys
import time

command = sys.argv[1:]
gpu = bool(command and command[0] == '--gpu')
if gpu:
    command = command[1:]
if not command:
    raise SystemExit('Usage: rss.py [--gpu] COMMAND [ARGS...]')
process = subprocess.Popen(command)
started = time.monotonic()
count = 0
minimum = maximum = first = last = None
gpu_count = 0
gpu_minimum = gpu_maximum = gpu_first = gpu_last = None
gpu_query_time = started
try:
    while process.poll() is None:
        if time.monotonic() - started >= 2:
            try:
                for line in pathlib.Path(f'/proc/{process.pid}/status').read_text().splitlines():
                    if line.startswith('VmRSS:'):
                        last = int(line.split()[1])
                        first = last if first is None else first
                        minimum = last if minimum is None else min(minimum, last)
                        maximum = last if maximum is None else max(maximum, last)
                        count += 1
            except FileNotFoundError:
                pass
            if gpu and time.monotonic() - gpu_query_time >= 1:
                result = subprocess.run(
                    ['nvidia-smi', '--query-compute-apps=pid,used_gpu_memory', '--format=csv,noheader,nounits'],
                    capture_output=True, text=True, timeout=5, check=True)
                for line in result.stdout.splitlines():
                    pid, memory = line.split(',')
                    if int(pid) == process.pid:
                        gpu_last = int(memory)
                        gpu_first = gpu_last if gpu_first is None else gpu_first
                        gpu_minimum = gpu_last if gpu_minimum is None else min(gpu_minimum, gpu_last)
                        gpu_maximum = gpu_last if gpu_maximum is None else max(gpu_maximum, gpu_last)
                        gpu_count += 1
                gpu_query_time = time.monotonic()
        time.sleep(0.2)
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5)
print(f'RSS KiB: samples={count} min={minimum} max={maximum} first={first} last={last}', flush=True)
if gpu:
    print(f'GPU MiB: samples={gpu_count} min={gpu_minimum} max={gpu_maximum} first={gpu_first} last={gpu_last}', flush=True)
sys.exit(process.returncode)
