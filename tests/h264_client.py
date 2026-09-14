import pathlib
import subprocess
import sys
import tempfile

build = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as temporary:
    path = pathlib.Path(temporary) / 'decoded.ppm'
    client = subprocess.Popen(
        [str(build / 'larp-client'), '--h264', '127.0.0.1', '0', '2', str(path)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        line = client.stdout.readline()
        assert line.startswith('Listening: '), (line, client.communicate())
        host = subprocess.run(
            [str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1',
             line.split()[-1], '10', '10'], capture_output=True, text=True, timeout=5)
        assert host.returncode == 0, (host.stdout, host.stderr)
        out, err = client.communicate(timeout=5)
        assert client.returncode == 0, (out, err)
        assert 'Validated: 10 ' in out and 'Corrupt: 0 ' in out, out
        data = path.read_bytes()
        prefix = b'P6\n320 180\n255\n'
        assert data.startswith(prefix) and len(data) == len(prefix) + 320 * 180 * 3
        # Synthetic source is a uniform RGB value of 90 in its first frame.
        assert max(abs(value - 90) for value in data[len(prefix):]) <= 4
    finally:
        if client.poll() is None:
            client.terminate()
            client.wait()
