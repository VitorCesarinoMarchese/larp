"""A low-FPS adaptive frame must not be paced beyond the receiver expiry time."""
import pathlib
import selectors
import subprocess
import sys
import tempfile

build = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as temporary:
    client = subprocess.Popen(
        [str(build / 'larp-client'), '--h264', '127.0.0.1', '0', '3',
         str(pathlib.Path(temporary) / 'frame.ppm')],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        with selectors.DefaultSelector() as ready:
            ready.register(client.stdout, selectors.EVENT_READ)
            assert ready.select(timeout=3), 'receiver readiness timeout'
        line = client.stdout.readline()
        assert line.startswith('Listening: '), line
        host = subprocess.run(
            [str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1', line.split()[-1],
             '2', '1', '--adaptive', '2000'], capture_output=True, text=True, timeout=4)
        assert host.returncode == 0, (host.stdout, host.stderr)
        output, error = client.communicate(timeout=5)
        assert client.returncode == 0, (output, error)
        assert 'Validated: 2 ' in output and 'Corrupt: 0 ' in output, output
        assert 'Expired: 0 ' in output, output
    finally:
        if client.poll() is None:
            client.kill()
            client.communicate(timeout=3)
