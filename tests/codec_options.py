import pathlib
import subprocess
import sys

build = pathlib.Path(sys.argv[1]).resolve()
commands = [
    ['larp-host', '--h264-synthetic', '127.0.0.1', '5000', '1', '10'],
    ['larp-client', '--h264', '127.0.0.1', '0', '1', '/tmp/unused-larp-codec-options.ppm'],
]
for command in commands:
    result = subprocess.run([str(build / command[0]), *command[1:], '--codec', 'invalid'],
                            capture_output=True, text=True, timeout=3)
    assert result.returncode != 0 and 'codec must be software or nvidia' in result.stderr, result
    assert 'Listening:' not in result.stdout and 'Encoder:' not in result.stdout, result

result = subprocess.run([str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1', '5000',
                         '1', '0', '--codec', 'nvidia'], capture_output=True, text=True, timeout=3)
assert result.returncode != 0 and 'Encoder:' not in result.stdout, result
assert 'cuda' not in result.stderr.lower() and 'nvenc' not in result.stderr.lower(), result

for options in [
    ['--adaptive', '0'], ['--adaptive', '8001'], ['--adaptive'],
    ['--adaptive', '1000', '--adaptive', '2000'],
    ['--adaptive', '1000', '--codec', 'nvidia'],
    ['--codec', 'nvidia', '--adaptive', '1000'],
]:
    result = subprocess.run(
        [str(build / 'larp-host'), '--h264-synthetic', '127.0.0.1', '5000', '1', '10', *options],
        capture_output=True, text=True, timeout=3)
    assert result.returncode != 0 and result.stderr, result
    assert 'Encoder:' not in result.stdout, result
    assert 'cuda' not in result.stderr.lower() and 'nvenc' not in result.stderr.lower(), result
