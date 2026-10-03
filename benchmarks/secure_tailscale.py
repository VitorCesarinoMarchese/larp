"""Verify encrypted synthetic video and process-restart recovery over two Tailscale nodes."""
import argparse
import os
import pathlib
import re
import shlex
import subprocess
import tempfile
import time


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def count(text, label):
    values = re.findall(rf'\b{label}: (\d+)', text)
    require(bool(values), f'missing {label}: {text}')
    return int(values[-1])


def ssh(remote, command):
    return ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', remote, shlex.join(command)]


def run(args):
    build = args.build.resolve()
    with tempfile.TemporaryDirectory(prefix='larp-secure-tailscale-') as temporary:
        directory = pathlib.Path(temporary)
        key = directory / 'session.key'
        subprocess.run([str(build / 'larp-keygen'), str(key)], check=True, stdout=subprocess.DEVNULL)
        remote_dir = subprocess.run(ssh(args.remote, ['mktemp', '-d', '/tmp/larp-secure-key.XXXXXX']),
                                    check=True, capture_output=True, text=True).stdout.strip()
        require(bool(re.fullmatch(r'/tmp/larp-secure-key\.[A-Za-z0-9]+', remote_dir)),
                'unexpected remote temporary directory')
        processes, files = [], []

        def start(command):
            path = directory / f'process-{len(processes)}.log'
            log = path.open('w')
            files.append(log)
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT,
                                       env=dict(os.environ, SDL_VIDEO_DRIVER='dummy'))
            processes.append(process)
            return process, path

        def ready(process, path):
            deadline = time.monotonic() + 8
            while True:
                text = path.read_text()
                match = re.search(r'^Listening: (\d+)$', text, re.MULTILINE)
                if match:
                    return match[1]
                require(process.poll() is None and time.monotonic() < deadline,
                        f'receiver did not start: {text}')
                time.sleep(0.02)

        def finish(process, path):
            require(process.wait(timeout=20) == 0, path.read_text())
            text = path.read_text()
            print(text, end='')
            require('Sanitizer' not in text and 'runtime error:' not in text, text)
            return text

        def local_receiver(seconds, port='0'):
            return start([str(build / 'larp-client'), '--view-h264', args.local_ip, port,
                          str(seconds), '--key-file', str(key), '--peer', args.remote_ip])

        try:
            subprocess.run(['scp', '-q', str(key), f'{args.remote}:{remote_dir}/session.key'], check=True)
            subprocess.run(ssh(args.remote, ['chmod', '600', f'{remote_dir}/session.key']), check=True)
            first, first_log = local_receiver(4)
            port = ready(first, first_log)
            sender, sender_log = start(ssh(args.remote, [f'{args.remote_build}/larp-host',
                '--h264-synthetic', args.local_ip, port, '300', '30', '--adaptive', '2000',
                '--key-file', f'{remote_dir}/session.key', '--bind', args.remote_ip]))
            first_text = finish(first, first_log)
            second, second_log = local_receiver(9, port)
            ready(second, second_log)
            sender_text = finish(sender, sender_log)
            second_text = finish(second, second_log)
            require(count(first_text, 'Validated') >= 30 and count(second_text, 'Validated') >= 100,
                    'video did not resume after receiver restart')
            for text in (first_text, second_text):
                require(count(text, 'Corrupt') == 0 and count(text, 'Presented') == count(text, 'Validated'), text)
            require(count(sender_text, 'Sessions') >= 2 and
                    len(re.findall(r'Feedback: 1\b', sender_text)) >= 2, sender_text)
            print('PASS: remote sender reconnected after local receiver restart; encrypted video and feedback resumed')

            receiver, receiver_log = start(ssh(args.remote, ['env', 'SDL_VIDEO_DRIVER=dummy',
                f'{args.remote_build}/larp-client', '--view-h264', args.remote_ip, '0', '10',
                '--key-file', f'{remote_dir}/session.key', '--peer', args.local_ip]))
            port = ready(receiver, receiver_log)
            for _ in range(2):
                host, host_log = start([str(build / 'larp-host'), '--h264-synthetic', args.remote_ip,
                    port, '60', '20', '--adaptive', '2000', '--key-file', str(key), '--bind', args.local_ip])
                finish(host, host_log)
            receiver_text = finish(receiver, receiver_log)
            require(count(receiver_text, 'Sessions') == 2 and count(receiver_text, 'Validated') >= 100 and
                    count(receiver_text, 'Corrupt') == 0 and
                    count(receiver_text, 'Presented') == count(receiver_text, 'Validated'), receiver_text)
            print('PASS: two local sender sessions decoded on the remote receiver; sender restart recovered')
        finally:
            for process in processes:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=3)
            for log in files:
                log.close()
            subprocess.run(ssh(args.remote, ['rm', '-rf', '--', remote_dir]), check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--remote', required=True, help='SSH login on the second Linux machine')
    parser.add_argument('--remote-build', required=True)
    parser.add_argument('--local-ip', required=True, help='Local Tailscale IPv4')
    parser.add_argument('--remote-ip', required=True, help='Remote Tailscale IPv4')
    run(parser.parse_args())
