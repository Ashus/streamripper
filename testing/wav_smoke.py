"""Verify PCM WAV output, using public fixtures and an optional LAME encoder.
Usage: python testing/wav_smoke.py dist/streamripper.exe build/core_regression.exe
       toolchain/bin/lame.exe build/sample-3s.wav build/sample-5s.mp3
The input samples are downloaded separately; this test never needs the network.
"""
from pathlib import Path
import concurrent.futures
import http.server
import os
import subprocess
import sys
import tempfile
import threading
import time
import wave

exe, decoder, lame, source_wav, source_mp3 = map(lambda value: Path(value).resolve(), sys.argv[1:6])
os.environ['PATH'] = str(lame.parent) + os.pathsep + os.environ['PATH']

def run(command, expected=0):
    result = subprocess.run(list(map(str, command)), capture_output=True, timeout=20)
    assert result.returncode == expected, (command, result.stdout[-2000:], result.stderr[-2000:])
    return result

with tempfile.TemporaryDirectory(prefix='streamripper-wav-') as directory:
    root = Path(directory)
    samples = {'stereo': source_mp3}
    mono = root / 'mono.mp3'
    run([lame, '--silent', '-m', 'm', source_wav, mono])
    samples['mono'] = mono
    for name, sample in samples.items():
        outputs = []
        for block in (1, 997, 16384, 65536):
            output = root / f'{name}-{block}.wav'
            run([decoder, '--decode', sample, output, block])
            data = output.read_bytes()
            assert data[:4] == b'RIFF' and int.from_bytes(data[4:8], 'little') + 8 == len(data)
            with wave.open(str(output), 'rb') as audio:
                assert audio.getnchannels() == (1 if name == 'mono' else 2)
                assert audio.getsampwidth() == 2 and audio.getframerate() == 44100
                seconds = audio.getnframes() / audio.getframerate()
                assert 2.5 < seconds < 6, seconds
                assert any(audio.readframes(audio.getnframes())), 'Output is silent'
            outputs.append(data)
        assert all(data == outputs[0] for data in outputs), 'MP3 block boundaries changed decoded PCM'
        print('OK: WAV', name, 'PCM identical for 1/997/16384/65536-byte writes', flush=True)
        stress = run([decoder, '--stress-decode', sample, root / f'{name}-stress.wav'])
        print(stress.stdout.decode().strip(), flush=True)
    garbage = root / 'garbage.mp3'
    garbage.write_bytes(b'not an MP3' * 4000)
    run([decoder, '--decode', garbage, root / 'garbage.wav', 997], expected=1)
    print('OK: malformed MP3 rejected', flush=True)

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_GET(self):
            name = 'mono' if self.path.startswith('/mono') else 'stereo'
            data = samples[name].read_bytes()
            self.send_response(200)
            self.send_header('Content-Type', 'audio/mpeg')
            self.send_header('icy-name', 'WAV test')
            self.send_header('icy-br', '128')
            self.send_header('icy-metaint', '16000')
            self.end_headers()
            offset = 0
            try:
                for index in range(400):
                    block = (data * 2)[offset:offset + 16000]
                    offset = (offset + 16000) % len(data)
                    title = f"StreamTitle='Artist - Track {index // 32}';".encode()
                    title += bytes(-len(title) % 16)
                    self.wfile.write(block + bytes([len(title) // 16]) + title)
                    time.sleep(0.02)
            except OSError:
                pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()

    def record(name):
        destination = root / name
        result = run([exe, f'http://localhost:{server.server_port}/{name}', '--wav',
                      '-c', '-l', '3', '-k', '0', '--xs-none', '-d', destination,
                      '-a', 'show', '-D', '%A - %T'])
        files = list(destination.rglob('*.wav'))
        assert len(files) >= 3, (files, result.stdout[-2000:], result.stderr[-2000:])
        assert not list(destination.rglob('*.mp3'))
        assert any(file.parent.name != 'incomplete' and file.name != 'show.wav' for file in files)
        for path in files:
            with wave.open(str(path), 'rb') as audio:
                assert audio.getnchannels() == (1 if name == 'mono' else 2), path
                assert audio.getsampwidth() == 2
                assert audio.getnframes() > 0, path
                assert len(audio.readframes(audio.getnframes())) == audio.getnframes() * audio.getnchannels() * 2, path
        cue = next(destination.rglob('*.cue')).read_text()
        assert 'WAVE' in cue and '\0' not in cue
        print('OK: WAV CLI', name, 'completed/incomplete tracks, show file, and cue', flush=True)

    try:
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(record, samples))
    finally:
        server.shutdown()
print('All WAV tests passed.')
