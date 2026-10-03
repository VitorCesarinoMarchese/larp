"""Exercise production authenticated sessions without desktop capture or a real display."""
import argparse
import os
import pathlib
import re
import socket
import subprocess
import tempfile
import threading
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def count(output, label):
    matches = re.findall(rf'\b{label}: (\d+)', output)
    require(bool(matches), f'missing {label}: {output}')
    return int(matches[-1])


def clean_diagnostics(text):
    require('Sanitizer' not in text and 'runtime error:' not in text, text)


def failed(result, phrase=None):
    text = (result.stdout + result.stderr).decode(errors='replace')
    clean_diagnostics(text)
    require(result.returncode != 0 and (phrase is None or phrase in text), text)


class Run:
    def __init__(self, build, directory, view):
        self.build, self.directory, self.view = build, directory, view
        self.processes = []
        self.files = []
        self.serial = 0
        self.key = directory / 'session.key'
        subprocess.run([str(build / 'larp-keygen'), str(self.key)], check=True,
                       stdout=subprocess.DEVNULL)
        require(self.key.stat().st_mode & 0o777 == 0o600, 'key permissions differ from 0600')

    def start(self, arguments):
        self.serial += 1
        path = self.directory / f'process-{self.serial}.log'
        output = path.open('w')
        self.files.append(output)
        process = subprocess.Popen(arguments, stdout=output, stderr=subprocess.STDOUT,
                                   env=dict(os.environ, SDL_VIDEO_DRIVER='dummy'))
        self.processes.append(process)
        return process, path

    def client(self, seconds, port=0, peer='127.0.0.1', key=None, raw=False):
        mode = [] if raw else (['--view-h264'] if self.view else ['--h264'])
        arguments = [str(self.build / 'larp-client'), *mode, '127.0.0.1', str(port), str(seconds)]
        if not self.view and not raw:
            arguments.append(str(self.directory / f'snapshot-{self.serial}.ppm'))
        arguments += ['--key-file', str(key or self.key), '--peer', peer]
        process, path = self.start(arguments)
        deadline = time.monotonic() + 5
        while True:
            text = path.read_text()
            ready = re.search(r'^Listening: (\d+)$', text, re.MULTILINE)
            if ready:
                return process, path, ('127.0.0.1', int(ready[1]))
            require(process.poll() is None and time.monotonic() < deadline,
                    f'secure receiver did not start: {text}')
            time.sleep(0.01)

    def host(self, destination, frames, fps=30, key=None):
        return self.start([str(self.build / 'larp-host'), '--h264-synthetic', destination[0],
                           str(destination[1]), str(frames), str(fps), '--adaptive', '2000',
                           '--key-file', str(key or self.key)])

    def finish(self, process, path, timeout=15):
        require(process.wait(timeout=timeout) == 0, path.read_text())
        text = path.read_text()
        clean_diagnostics(text)
        return text

    def close(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
        for output in self.files:
            output.close()


class Proxy:
    def __init__(self, receiver, inject=False):
        self.receiver = receiver
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.settimeout(0.01)
        self.address = self.socket.getsockname()
        self.sender = None
        self.inject = inject
        self.blackhole = False
        self.first = {}
        self.held = None
        self.reordered = False
        self.pings = 0
        self.media = threading.Event()
        self.stopped = threading.Event()
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            while not self.stopped.is_set():
                try:
                    data, source = self.socket.recvfrom(1249)
                except socket.timeout:
                    continue
                if self.blackhole:
                    continue
                require(48 <= len(data) <= 1248 and data[:6] == b'LASE\x00\x01',
                        f'plaintext or oversized wire packet: {len(data)} bytes')
                kind, direction = data[6:8]
                if source == self.receiver:
                    require(direction == 1 and self.sender is not None, 'invalid return direction')
                    self.socket.sendto(data, self.sender)
                    if self.inject and kind == 5 and 'feedback' not in self.first:
                        self.first['feedback'] = data
                        self.socket.sendto(data, self.sender)
                else:
                    require(direction == 0, 'invalid sender direction')
                    self.sender = source
                    if kind == 6:
                        self.pings += 1
                    if kind in (1, 3):
                        self.first.setdefault(kind, data)
                    if kind == 5:
                        self.media.set()
                        if self.inject and 'media' not in self.first:
                            self.first['media'] = data
                            changed = bytearray(data)
                            changed[-1] ^= 1
                            self.socket.sendto(changed, self.receiver)
                            changed = bytearray(data)
                            changed[24] ^= 255
                            self.socket.sendto(changed, self.receiver)
                            self.socket.sendto(data, self.receiver)
                            self.socket.sendto(data, self.receiver)
                            self.socket.sendto(b'LARP' + bytes(40), self.receiver)
                            self.socket.sendto(bytes(1249), self.receiver)
                            continue
                        if self.inject and not self.reordered:
                            if self.held is not None:
                                self.socket.sendto(data, self.receiver)
                                self.socket.sendto(self.held, self.receiver)
                                self.held = None
                                self.reordered = True
                                continue
                            if len(data) == 1248:
                                self.held = data
                                continue
                    self.socket.sendto(data, self.receiver)
        except Exception as error:
            if not self.stopped.is_set():
                self.error = error

    def close(self):
        self.stopped.set()
        self.thread.join(timeout=2)
        self.socket.close()
        require(not self.thread.is_alive(), 'proxy did not stop')
        if self.error:
            raise self.error


def sender_restart(run):
    client, client_log, receiver = run.client(8)
    proxy = Proxy(receiver, inject=True)
    try:
        first, first_log = run.host(proxy.address, 20, 10)
        first_text = run.finish(first, first_log)
        second, second_log = run.host(proxy.address, 20, 10)
        second_text = run.finish(second, second_log)
        time.sleep(1.1)
        require(all(key in proxy.first for key in (1, 3, 'media', 'feedback')), 'missing encrypted fixture')
        for key in (1, 3, 'media'):
            proxy.socket.sendto(proxy.first[key], receiver)
        text = run.finish(client, client_log)
        require(count(text, 'Validated') == 40 and count(text, 'Corrupt') == 0, text)
        if run.view:
            require(count(text, 'Presented') == 40, text)
        require(count(text, 'Sessions') == 2 and count(text, 'Security rejected') >= 5, text)
        require(count(first_text, 'Security rejected') >= 1, first_text)
        require(count(second_text, 'Sessions') == 1, second_text)
        require(proxy.reordered and proxy.held is None, 'fragment reordering was not exercised')
        print('PASS: sender restart and fragment reordering; tampering, plaintext, feedback replay, and retired-session replay rejected')
    finally:
        proxy.close()


def receiver_restart(run):
    first, first_log, receiver = run.client(3)
    host, host_log = run.host(receiver, 270)
    first_text = run.finish(first, first_log)
    require(count(first_text, 'Validated') >= 30, first_text)
    second, second_log, _ = run.client(8, receiver[1])
    host_text = run.finish(host, host_log)
    second_text = run.finish(second, second_log)
    require(count(host_text, 'Sessions') >= 2, host_text)
    require(count(second_text, 'Validated') >= 120 and count(second_text, 'Corrupt') == 0, second_text)
    require(len(re.findall(r'Feedback: 1\b', host_text)) >= 2,
            f'controller did not establish a fresh counter baseline: {host_text}')
    if run.view:
        require(count(second_text, 'Presented') == count(second_text, 'Validated'), second_text)
    print('PASS: receiver restart; sender reconnected and adaptive feedback established a fresh baseline')


def network_outage(run):
    client, client_log, receiver = run.client(7)
    proxy = Proxy(receiver)
    try:
        host, host_log = run.host(proxy.address, 150)
        require(proxy.media.wait(timeout=5), 'encrypted media did not start')
        time.sleep(0.8)
        proxy.blackhole = True
        time.sleep(1.5)
        proxy.blackhole = False
        host_text = run.finish(host, host_log)
        text = run.finish(client, client_log)
        require(60 < count(text, 'Validated') < 150 and count(text, 'Corrupt') == 0, text)
        require(count(host_text, 'Sessions') >= 2 and count(text, 'Sessions') >= 2, (host_text, text))
        require(count(host_text, 'Unsent') > 0, 'reconnection did not discard pending media')
        if run.view:
            require(count(text, 'Presented') == count(text, 'Validated'), text)
        print('PASS: complete bidirectional outage; authenticated reconnection and decoded video resumed')
    finally:
        proxy.close()


def reject_keys(run):
    original = run.key.read_bytes()
    result = subprocess.run([str(run.build / 'larp-keygen'), str(run.key)], capture_output=True)
    failed(result)
    require(run.key.read_bytes() == original, 'keygen overwrote an existing key')
    other = run.directory / 'wrong.key'
    subprocess.run([str(run.build / 'larp-keygen'), str(other)], check=True, stdout=subprocess.DEVNULL)
    client, client_log, receiver = run.client(6)
    host, host_log = run.host(receiver, 1, key=other)
    foreign_client, foreign_log, foreign_receiver = run.client(6, peer='127.0.0.2')
    foreign_host, foreign_host_log = run.host(foreign_receiver, 1)
    require(host.wait(timeout=7) != 0 and 'handshake timed out' in host_log.read_text(), host_log.read_text())
    require(foreign_host.wait(timeout=7) != 0 and 'handshake timed out' in foreign_host_log.read_text(),
            foreign_host_log.read_text())
    clean_diagnostics(host_log.read_text())
    clean_diagnostics(foreign_host_log.read_text())
    text = run.finish(client, client_log)
    require(count(text, 'Validated') == 0 and count(text, 'Sessions') == 0 and
            count(text, 'Security rejected') > 0, text)
    foreign_text = run.finish(foreign_client, foreign_log)
    require(count(foreign_text, 'Validated') == 0 and count(foreign_text, 'Sessions') == 0 and
            count(foreign_text, 'Security rejected') > 0, foreign_text)
    for permission, content in [(0o644, original), (0o600, original[:-1])]:
        other.write_bytes(content)
        other.chmod(permission)
        result = subprocess.run([str(run.build / 'larp-client'), '127.0.0.1', '0', '1',
                                 '--key-file', str(other), '--peer', '127.0.0.1'], capture_output=True)
        failed(result, '32-byte regular file')
    for args in [['--key-file', str(run.key)], ['--peer', '127.0.0.1'],
                 ['--key-file', str(run.key), '--peer', '127.0.0.1', '--peer', '127.0.0.1']]:
        result = subprocess.run([str(run.build / 'larp-client'), '127.0.0.1', '0', '1', *args], capture_output=True)
        failed(result)
    pipe = run.directory / 'pipe.key'
    os.mkfifo(pipe)
    alias = run.directory / 'alias.key'
    alias.symlink_to(run.key)
    for path in (pipe, alias):
        result = subprocess.run([str(run.build / 'larp-client'), '127.0.0.1', '0', '1',
                                 '--key-file', str(path), '--peer', '127.0.0.1'],
                                capture_output=True, timeout=2)
        failed(result)
    print('PASS: wrong keys and peers, exposed or malformed key files, and invalid options rejected; existing keys preserved')


def raw_heartbeat(run):
    client, client_log, receiver = run.client(4, raw=True)
    proxy = Proxy(receiver)
    try:
        host, host_log = run.start([str(run.build / 'larp-host'), proxy.address[0],
                                   str(proxy.address[1]), '4', '16384', '1',
                                   '--key-file', str(run.key)])
        host_text = run.finish(host, host_log)
        text = run.finish(client, client_log)
        require(count(text, 'Validated') == 4 and count(text, 'Corrupt') == 0, text)
        require(count(host_text, 'Sessions') == 1 and proxy.pings >= 8,
                f'raw sender did not service heartbeats between one-second frame intervals: {host_text}')
        print('PASS: encrypted synthetic bytes; heartbeats continue between low-FPS frames')
    finally:
        proxy.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=pathlib.Path)
    parser.add_argument('--view', action='store_true', help='verify presentation using SDL dummy display')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='larp-secure-') as temporary:
        run = Run(args.build.resolve(), pathlib.Path(temporary), args.view)
        try:
            sender_restart(run)
            receiver_restart(run)
            network_outage(run)
            reject_keys(run)
            raw_heartbeat(run)
        finally:
            run.close()


if __name__ == '__main__':
    main()
