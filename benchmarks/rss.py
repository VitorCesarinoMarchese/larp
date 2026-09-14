"""Sample one command's RSS every 200 ms after two seconds, without retaining samples."""
import pathlib
import subprocess
import sys
import time

process = subprocess.Popen(sys.argv[1:])
started = time.monotonic()
count = 0
minimum = maximum = first = last = None
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
        time.sleep(0.2)
finally:
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=5)
print(f'RSS KiB: samples={count} min={minimum} max={maximum} first={first} last={last}', flush=True)
sys.exit(process.returncode)
