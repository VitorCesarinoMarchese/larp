"""Run a bounded transfer and sample Linux RSS. Usage: transfer.py BUILD [IP] [SECONDS] [BYTES] [FPS]."""
import pathlib
import subprocess
import sys
import tempfile
import time

build = pathlib.Path(sys.argv[1]).resolve()
ip = sys.argv[2] if len(sys.argv) > 2 else '127.0.0.1'
seconds = int(sys.argv[3]) if len(sys.argv) > 3 else 10
size = int(sys.argv[4]) if len(sys.argv) > 4 else 16384
fps = int(sys.argv[5]) if len(sys.argv) > 5 else 60
assert 1 <= seconds <= 3600

def rss(pid):
    try:
        for line in pathlib.Path(f'/proc/{pid}/status').read_text().splitlines():
            if line.startswith('VmRSS:'):
                return int(line.split()[1])
    except FileNotFoundError:
        return None

with tempfile.TemporaryFile(mode='w+') as output:
    client = subprocess.Popen([str(build / 'larp-client'), ip, '0', str(seconds + 1)], stdout=output)
    host = None
    try:
        deadline = time.monotonic() + 5
        while True:
            output.seek(0)
            line = output.readline()
            if line.startswith('Listening: '):
                break
            if client.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError('client did not start')
            time.sleep(0.01)
        host = subprocess.Popen([str(build / 'larp-host'), ip, line.split()[-1],
                                 str(seconds * fps), str(size), str(fps)])
        samples = {'host': [], 'client': []}
        start = time.monotonic()
        while client.poll() is None:
            if time.monotonic() - start > seconds + 10:
                raise RuntimeError('transfer timed out')
            for name, process in [('host', host), ('client', client)]:
                value = rss(process.pid)
                if value is not None and time.monotonic() - start >= 1:
                    samples[name].append(value)
            time.sleep(0.1)
        assert host.wait(timeout=3) == 0
        assert client.returncode == 0
        output.seek(0)
        print(output.read(), end='')
        for name, values in samples.items():
            if values:
                print(f'{name} RSS after warmup: min={min(values)} max={max(values)} '
                      f'first={values[0]} last={values[-1]} KiB')
    finally:
        for process in [host, client]:
            if process is not None and process.poll() is None:
                process.terminate()
                process.wait(timeout=3)
