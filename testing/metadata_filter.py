"""Exercise the bundled generic rules against a local ICY stream."""
from pathlib import Path
import http.server
import datetime
import re
import subprocess
import tempfile
import threading
import time
import wave

repo = Path(__file__).resolve().parents[1]
audio = (repo / 'build' / 'sample-5s.mp3').read_bytes()
titles = ['ADVERT - Žluťoučký', 'Artist - One mp3pro', 'advert - Break',
          'ADVERT - Another break', 'Artist - Two', ' - ',
          'Artist - Three', '', 'Ignore - Tab\tBack\\slash\nNext', 'advert - Closing']


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        for key, value in {'Content-Type': 'audio/mpeg', 'icy-name': 'Test stream',
                           'icy-metaint': '16000', 'icy-br': '128'}.items():
            self.send_header(key, value)
        self.end_headers()
        try:
            for i in range(1000):
                title = titles[min(i // 24, len(titles) - 1)]
                # Zero-length blocks must retain the previous track decision.
                meta = ("StreamTitle='" + title + "';").encode() if i % 24 == 0 else b''
                meta += bytes(-len(meta) % 16)
                offset = (i * 16000) % len(audio)
                self.wfile.write((audio * 2)[offset:offset + 16000]
                                 + bytes([len(meta) // 16]) + meta)
                time.sleep(0.01)
        except OSError:
            pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
try:
    with tempfile.TemporaryDirectory(prefix='metadata-filter-', dir=repo / 'build') as tmp:
        root = Path(tmp)
        rules = root / 'pravidla-žluťoučký.txt'
        example = (repo / 'advert_rules.txt').read_text(encoding='utf-8')
        rules.write_text('m/^Ignore/e\n' + example.replace(r'# m/^[\s-]*$/x',
                                         r'm/^[\s-]*$/x'), encoding='utf-8')
        for wav in (False, True):
            dest = root / 'žluťoučký' / ('wav' if wav else 'mp3')
            args = [str(repo / 'dist' / 'streamripper.exe'),
                    f'http://localhost:{server.server_port}/stream', '-c', '-l', '4',
                    '-k', '0', '-s', '-d', str(dest), '-q', '1', '--xs-none',
                    '-w', str(rules), '--codeset-metadata=AUTO']
            if wav:
                args.append('--wav')
                args.append('--metadata-log')
                args.append('--quiet')
                log_path = dest / 'incomplete' / 'metadata.log'
                log_path.parent.mkdir(parents=True)
                log_path.write_text('Previous session\n', encoding='utf-8')
            result = subprocess.run(args, capture_output=True, timeout=15)
            assert result.returncode == 0, result.stdout + result.stderr
            extension = '.wav' if wav else '.mp3'
            files = sorted(dest.rglob('*' + extension))
            expected = [f'{n:04d}_Artist - {title}{extension}'
                        for n, title in enumerate(('One', 'Two', 'Three'), 1)]
            assert [p.name for p in files] == expected, [p.relative_to(dest) for p in files]
            if wav:
                entries = log_path.read_text(encoding='utf-8').splitlines()
                assert entries.pop(0) == 'Previous session', entries
                logged = []
                for entry in entries:
                    timestamp, title = entry.split('\t', 1)
                    assert datetime.datetime.fromisoformat(timestamp).tzinfo is not None, timestamp
                    logged.append(title)
                expected_titles = [title.strip().replace('\\', '\\\\').replace('\t', '\\t')
                                   .replace('\n', '\\n') for title in titles]
                assert logged == expected_titles, logged
                for path in files:
                    with wave.open(str(path), 'rb') as recorded:
                        assert recorded.getnframes() > 0
            else:
                assert not list(dest.rglob('metadata.log'))
                statuses = re.findall(rb'\[\s*(filtered|incomplete|ripping)\.\.\.\s*\] ([^\r\n]*)', result.stdout)
                assert any(state == b'filtered' and b'ADVERT -' in title for state, title in statuses), statuses
                assert not any(state == b'ripping' and b'ADVERT -' in title for state, title in statuses), statuses
                assert not any(state == b'incomplete' and b'ADVERT -' in title for state, title in statuses), statuses
                assert any(state == b'ripping' and b'Artist - One' in title for state, title in statuses), statuses
            print('PASS:', extension, 'exclusion, Unicode rules path, empty/hyphen titles, consecutive numbering')
        for blocked in (False, True):
            dest = root / ('blocked-log' if blocked else 'show-only')
            log_path = dest / 'incomplete' / 'metadata.log'
            if blocked:
                log_path.mkdir(parents=True)
            result = subprocess.run([str(repo / 'dist' / 'streamripper.exe'),
                        f'http://localhost:{server.server_port}/stream', '-c', '-l', '1',
                        '-s', '-d', str(dest), '-A', '-a', 'show', '--metadata-log',
                        '--codeset-metadata=AUTO'], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stdout + result.stderr
            assert (dest / 'show.mp3').stat().st_size > 0
            if blocked:
                assert b'Could not append ICY metadata' in result.stderr, result.stderr
            else:
                assert 'ADVERT - Žluťoučký' in log_path.read_text(encoding='utf-8')
            print('PASS: metadata log', 'write error preserves recording' if blocked else 'show-only recording')
finally:
    server.shutdown()
    server.server_close()
