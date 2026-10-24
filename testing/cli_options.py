"""Check descriptive option aliases and LF/CRLF metadata rule files."""
from pathlib import Path
import http.server
import subprocess
import tempfile
import threading

repo = Path(__file__).resolve().parents[1]
exe = repo / 'dist' / 'streamripper.exe'
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()
stop = threading.Event()
seen_agents = []


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        seen_agents.append(self.headers.get('User-Agent'))
        self.send_response(200)
        self.send_header('Content-Type', 'audio/mpeg')
        self.send_header('icy-name', 'CLI test')
        self.send_header('icy-metaint', '16384')
        self.send_header('icy-br', '128')
        self.end_headers()
        if self.path == '/stall':
            stop.wait(20)
            return
        offset = 0
        number = 0
        try:
            while not stop.is_set():
                block = (audio * (16384 // len(audio) + 2))[offset:offset + 16384]
                offset = (offset + 16384) % len(audio)
                title = 'Artist - Song' + (str(number // 24) if self.path == '/changing' else '')
                meta = f"StreamTitle='{title}';".encode()
                meta += bytes(-len(meta) % 16)
                self.wfile.write(block + bytes([len(meta) // 16]) + meta)
                number += 1
                stop.wait(0.01)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='cli-options-', dir=repo / 'build') as temporary:
        root = Path(temporary)
        help_text = subprocess.check_output([str(exe), '--help'])
        required = {
            'output-directory': str(root / 'output with spaces'),
            'output-pattern': str(root / '%A - %T'),
            'metadata-rules': str(root / 'rules.txt'),
            'external-metadata': 'metadata-helper --some-option',
            'interface': '127.0.0.1', 'skip-initial-tracks': '0',
            'record-duration': '2', 'relay-playlist': str(root / 'relay.pls'),
            'timeout': '2', 'max-megabytes': '100', 'overwrite': 'never',
            'proxy': 'http://127.0.0.1:8080', 'max-relay-clients': '2',
            'user-agent': 'CLI Test/1.0',
        }
        for name, value in required.items():
            for options in ([f'--{name}', value], [f'--{name}={value}']):
                result = subprocess.run([str(exe), 'http://localhost/'] + options + ['--help'],
                                        capture_output=True, timeout=5)
                assert result.returncode == 0 and result.stdout == help_text, (options, result.stderr)
            result = subprocess.run([str(exe), 'http://localhost/', f'--{name}'], capture_output=True)
            assert result.returncode != 0 and b'requires an argument' in result.stderr, name
        for name, value, short in (('continuous-file', 'a file with spaces', '-a'),
                                   ('sequence', '7', '-q'), ('relay', '127.0.0.1:8000', '-r')):
            for options in ([f'--{name}'], [f'--{name}', value], [f'--{name}={value}'], [short]):
                result = subprocess.run([str(exe), 'http://localhost/'] + options + ['--help'],
                                        capture_output=True, timeout=5)
                assert result.returncode == 0 and result.stdout == help_text, (options, result.stderr)
        for name in ('help', 'version', 'no-reconnect', 'no-stream-directory',
                     'no-individual-tracks', 'keep-incomplete', 'truncate-duplicates',
                     'no-id3', 'no-port-scan'):
            result = subprocess.run([str(exe), 'http://localhost/', f'--{name}=unexpected', '--help'],
                                    capture_output=True)
            assert result.returncode != 0 and b'does not take an argument' in result.stderr, name
        print('PASS: all long aliases, spaced/equals values, required values, optional values, and flag arity')
        for number, options in enumerate((['-m', '1'], ['--timeout', '1'], ['--timeout=1'])):
            # If the alias were ignored, this server would stall beyond the test deadline.
            result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/stall',
                                     '-c', '-d', str(root / str(number))] + options,
                                    capture_output=True, timeout=5)
            assert b'error' in result.stderr.lower(), result.stdout + result.stderr
        for options in (['-m', 'invalid'], ['--timeout', 'invalid'], ['--timeout=invalid'],
                        ['--timeout=2147483648'], ['--timeout=-1']):
            result = subprocess.run([str(exe), 'http://localhost/'] + options + ['-h'],
                                    capture_output=True)
            assert result.returncode != 0 and b'Invalid timeout' in result.stderr, options
        print('PASS: short/spaced/equals timeout forms enforce the connection timeout and reject invalid values')

        for number, options in enumerate((['-t', '-i'], ['--keep-incomplete', '--no-id3'])):
            dest = root / ('tags' + str(number))
            incomplete = dest / 'incomplete'
            incomplete.mkdir(parents=True)
            old = b'Preserve the existing incomplete recording.'
            (incomplete / 'Artist - Song.mp3').write_bytes(old)
            result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/audio',
                                     '-c', '-l', '2', '-s', '-d', str(dest), '--xs-none',
                                     '--with-id3v1'] + options, capture_output=True, timeout=8)
            assert result.returncode == 0, result.stdout + result.stderr
            files = list(incomplete.glob('*.mp3'))
            assert len(files) == 2, files
            bodies = [path.read_bytes() for path in files]
            assert old in bodies
            recorded = next(data for data in bodies if data != old)
            assert not recorded.startswith(b'ID3') and recorded[-128:-125] != b'TAG'
        print('PASS: short and long keep-incomplete/no-id3 forms preserve the backup and disable both tag versions')

        for number, newline in enumerate((b'\n', b'\r\n')):
            rules = root / ('rules' + str(number) + '.txt')
            rules.write_bytes(newline.join((b'# Filter before extracting fields',
                              b'm/^Artist - Song$/x', b'm/^(.*?) - (.*)$/A1T2', b'')))
            dest = root / ('rules-out' + str(number))
            result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/audio',
                                     '-c', '-l', '2', '-s', '-d', str(dest), '--xs-none',
                                     '-w', str(rules)], capture_output=True, timeout=8)
            assert result.returncode == 0, result.stdout + result.stderr
            assert b'malformed' not in result.stdout.lower() + result.stderr.lower()
            assert not list(dest.rglob('*.mp3'))
        print('PASS: LF and CRLF metadata rule files apply the same exclusion')

        dest = root / 'all long options'
        rules = root / 'long-rules.txt'
        rules.write_bytes(b's/^Artist/Parsed/\nm/^(.*?) - (.*)$/A1T2\n')
        result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/changing',
                    '--no-reconnect', '--record-duration=2', '--output-directory', str(dest),
                    '--output-pattern=' + str(dest / 'finished' / '%1q_%A_%T'),
                    '--no-stream-directory', '--sequence=1', '--skip-initial-tracks', '0',
                    '--overwrite=never', '--truncate-duplicates', '--max-megabytes=100',
                    '--user-agent=Long Options Test', '--metadata-rules', str(rules),
                    '--continuous-file=' + str(dest / 'continuous'), '--timeout=2',
                    '--no-id3', '--xs-none'], capture_output=True, timeout=8)
        assert result.returncode == 0, result.stdout + result.stderr
        tracks = sorted((dest / 'finished').glob('*.mp3'))
        assert len(tracks) >= 2 and tracks[0].name == '0001_Parsed_Song0.mp3', tracks
        assert (dest / 'continuous.mp3').stat().st_size > 0
        assert (dest / 'continuous.cue').is_file()
        assert 'Long Options Test' in seen_agents
        print('PASS: recording through long options, argument ownership, paths with spaces, rules, pattern, sequence, and show output')

        dest = root / 'show-only'
        result = subprocess.run([str(exe), f'http://localhost:{server.server_port}/audio',
                    '--no-reconnect', '--record-duration=2', '--output-directory', str(dest),
                    '--no-stream-directory', '--continuous-file=only-show', '--no-individual-tracks'],
                    capture_output=True, timeout=8)
        assert result.returncode == 0, result.stdout + result.stderr
        assert [path.name for path in dest.rglob('*.mp3')] == ['only-show.mp3']
        print('PASS: --no-individual-tracks with --continuous-file')
finally:
    stop.set()
    server.shutdown()
    server.server_close()
